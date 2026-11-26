//
// Tests the server side of networking on the host: tcp_listener handing
// out tcp_socket connections, udp_socket datagrams and broadcasts, and
// the emulated access point and scan. Everything runs over loopback, so
// no network or radio is needed.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <inkerface/network.h>
#include <inkerface/socket.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include <unistd.h>

namespace tests {

namespace {

using namespace std::chrono_literals;
using inkerface::datagram_source;
using inkerface::net_error;
using inkerface::network;
using inkerface::tcp_listener;
using inkerface::tcp_socket;
using inkerface::udp_socket;

// A port the system chose and has just released.
std::uint16_t spare_port(network &link) {
    const udp_socket probe(link);
    return probe.port();
}

// Return true when this process may not take port 1: it is not root and
// the system reserves the ports below 1024, as Linux does by default.
// Containers usually lift that (Docker starts unprivileged ports at 0).
bool low_ports_reserved() {
    if (::geteuid() == 0) {
        return false;
    }
    std::ifstream limit("/proc/sys/net/ipv4/ip_unprivileged_port_start");
    int first_unprivileged = 1024;
    if (!(limit >> first_unprivileged)) {
        first_unprivileged = 1024;
    }
    return first_unprivileged > 1;
}

std::span<const std::byte> bytes_of(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

// Read from a socket until it holds want bytes or a second passes.
std::string read_text(tcp_socket &from, std::size_t want) {
    std::string text;
    std::array<std::byte, 64> buffer{};
    const auto give_up = std::chrono::steady_clock::now() + 1s;
    while (text.size() < want && std::chrono::steady_clock::now() < give_up) {
        const std::size_t got = from.read(buffer, 50ms);
        text.append(reinterpret_cast<const char *>(buffer.data()), got);
    }
    return text;
}

void links(test_context &context) {
    network link;
    const tcp_listener refused(link, 8080);
    context.check(refused.error() == net_error::not_connected,
                  "a listener needs a connected or hosting network");
    const udp_socket also_refused(link);
    context.check(also_refused.error() == net_error::not_connected,
                  "a UDP socket needs a connected or hosting network");

    context.check(!link.scan().empty(), "the host scan lists networks");
    context.check(!link.start_access_point("x", "short").has_value(),
                  "a WPA2 password under 8 characters is refused");
    context.check(!link.start_access_point("", "").has_value(),
                  "an access point needs a name");

    const auto hosted = link.start_access_point("sheep_0123abcd", "password");
    context.check(hosted.has_value() && hosted->emulated &&
                      !hosted->address.empty(),
                  "the host emulates an access point at its own address");
    context.check(link.hosting() && !link.connected(),
                  "hosting is not being joined");
    const tcp_listener served(link, spare_port(link));
    context.check(static_cast<bool>(served),
                  "a listener works while hosting");

    context.check(link.connect("Home", "password"),
                  "the host joins at once");
    context.check(!link.hosting(), "joining ends hosting");
    link.stop_access_point();
    context.check(link.connected(), "stopping no access point is harmless");
}

void listeners(test_context &context) {
    network link;
    static_cast<void>(link.connect("", ""));
    const std::uint16_t port = spare_port(link);

    tcp_listener server(link, port, 1s);
    context.check(static_cast<bool>(server), "a listener opens");

    tcp_socket idle = server.accept();
    context.check(!idle.is_open() && idle.error() == net_error::none,
                  "accept without a client returns a closed socket at once");

    tcp_listener second(link, port);
    context.check(second.error() == net_error::bind,
                  "a port in use is reported as bind");

    if (low_ports_reserved()) {
        const tcp_listener privileged(link, 1);
        context.check(privileged.error() == net_error::bind,
                      "a privileged port is reported as bind");
    }

    tcp_socket client(link, "127.0.0.1", port);
    tcp_socket peer = server.accept(1s);
    context.check(static_cast<bool>(peer), "a waiting client is accepted");

    context.check(client.write("hello") == 5, "the client sends");
    context.check(read_text(peer, 5) == "hello",
                  "the accepted socket reads what was sent");
    context.check(peer.write("world") == 5, "the accepted socket sends");
    context.check(read_text(client, 5) == "world",
                  "the client reads the reply");

    client.close();
    std::array<std::byte, 8> buffer{};
    std::size_t got = 1;
    const auto give_up = std::chrono::steady_clock::now() + 1s;
    while (peer.is_open() && std::chrono::steady_clock::now() < give_up) {
        got = peer.read(buffer, 50ms);
    }
    context.check(got == 0 && peer.error() == net_error::closed,
                  "the accepted socket notices the client leave");

    tcp_listener moved(std::move(server));
    context.check(static_cast<bool>(moved) && !server.is_open(),
                  "a moved-from listener is closed");
}

void datagrams(test_context &context) {
    network link;
    static_cast<void>(link.connect("", ""));

    udp_socket listener(link, spare_port(link));
    udp_socket asker(link);
    context.check(static_cast<bool>(listener) && static_cast<bool>(asker),
                  "UDP sockets open");
    context.check(listener.port() != 0 && asker.port() != 0,
                  "a UDP socket knows its port");

    std::array<std::byte, 64> buffer{};
    datagram_source from;
    context.check(listener.receive(buffer, from) == 0,
                  "receive with nothing waiting returns at once");

    context.check(asker.send_to("127.0.0.1", listener.port(),
                                bytes_of("ping")),
                  "a datagram is sent");
    const std::size_t got = listener.receive(buffer, from, 1s);
    context.check(got == 4 && std::string_view(
                                  reinterpret_cast<const char *>(buffer.data()),
                                  got) == "ping",
                  "the datagram arrives whole");
    context.check(from.address == "127.0.0.1" && from.port == asker.port(),
                  "the sender is reported");

    context.check(listener.send_to(from.address, from.port, bytes_of("pong")),
                  "a reply goes back to the sender");
    datagram_source back;
    context.check(asker.receive(buffer, back, 1s) == 4 &&
                      back.port == listener.port(),
                  "the reply arrives");

    context.check(!asker.send_to("not an address", 9, bytes_of("x")),
                  "a malformed address is refused");

    context.check(asker.broadcast(listener.port(), bytes_of("anyone?")),
                  "a broadcast goes out");
    context.check(listener.receive(buffer, from, 1s) == 7,
                  "a broadcast reaches a listener on this machine");
}

} // namespace

void run_network_server_tests(test_context &context) {
    links(context);
    listeners(context);
    datagrams(context);
}

} // namespace tests
