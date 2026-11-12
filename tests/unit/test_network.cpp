//
// Tests network, tcp_socket and http_client on the host against a local
// scripted server, so the real code path (FreeInk's HTTP client, the
// Arduino shim, POSIX sockets) runs without internet access.
//
// The server runs on one background thread that handles a fixed list of
// connections in order and is joined before its recorded requests are
// read, so the test thread and the server never share data concurrently.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <inkerface/http.h>
#include <inkerface/network.h>
#include <inkerface/socket.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tests {

namespace {

using namespace std::chrono_literals;
using inkerface::http_client;
using inkerface::http_request;
using inkerface::http_response;
using inkerface::net_error;
using inkerface::network;
using inkerface::tcp_socket;

// Handles one accepted connection; returns the text the client sent.
using connection_handler = std::function<std::string(int descriptor)>;

// Longest a server waits for a client before giving up, so a failing
// test can never hang.
constexpr int server_wait_ms = 5000;

// Receive more bytes into text; false when the client stops sending.
bool receive_more(int descriptor, std::string &text) {
    std::array<char, 512> buffer{};
    pollfd watched{descriptor, POLLIN, 0};
    if (::poll(&watched, 1, server_wait_ms) <= 0) {
        return false;
    }
    const ssize_t got = ::recv(descriptor, buffer.data(), buffer.size(), 0);
    if (got <= 0) {
        return false;
    }
    text.append(buffer.data(), static_cast<std::size_t>(got));
    return true;
}

// Read until text contains marker or the client stops sending.
std::string read_until(int descriptor, std::string_view marker) {
    std::string text;
    while (text.find(marker) == std::string::npos &&
           receive_more(descriptor, text)) {
    }
    return text;
}

// Read one HTTP request: headers, then a Content-Length body if any.
std::string read_request(int descriptor) {
    std::string text = read_until(descriptor, "\r\n\r\n");
    const std::size_t end = text.find("\r\n\r\n");
    const std::size_t field = text.find("Content-Length: ");
    if (end == std::string::npos || field == std::string::npos) {
        return text;
    }
    const std::size_t length = std::stoul(
        text.substr(field + std::string_view("Content-Length: ").size()));
    while (text.size() < end + 4 + length && receive_more(descriptor, text)) {
    }
    return text;
}

void send_text(int descriptor, std::string_view text) {
    static_cast<void>(
        ::send(descriptor, text.data(), text.size(), MSG_NOSIGNAL));
}

// Answer one HTTP request with a fixed response.
connection_handler reply(std::string response) {
    return [response = std::move(response)](int descriptor) {
        std::string request = read_request(descriptor);
        send_text(descriptor, response);
        return request;
    };
}

// A loopback server that serves a fixed list of connections in order.
class local_server {
public:
    explicit local_server(std::vector<connection_handler> handlers)
        : listener_(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(address);
        auto *raw = reinterpret_cast<sockaddr *>(&address);
        if (::bind(listener_, raw, length) != 0 ||
            ::listen(listener_, 4) != 0 ||
            ::getsockname(listener_, raw, &length) != 0) {
            return;
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this, handlers = std::move(handlers)]() {
            for (const connection_handler &handle : handlers) {
                pollfd watched{listener_, POLLIN, 0};
                if (::poll(&watched, 1, server_wait_ms) <= 0) {
                    return;
                }
                const int client = ::accept(listener_, nullptr, nullptr);
                if (client < 0) {
                    return;
                }
                requests_.push_back(handle(client));
                ::close(client);
            }
        });
    }

    ~local_server() {
        finish();
        ::close(listener_);
    }

    local_server(const local_server &) = delete;
    local_server &operator=(const local_server &) = delete;
    local_server(local_server &&) = delete;
    local_server &operator=(local_server &&) = delete;

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] std::string url(std::string_view path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
    }

    // Wait for the server to finish; the requests are then safe to read.
    const std::vector<std::string> &finish() {
        if (worker_.joinable()) {
            worker_.join();
        }
        return requests_;
    }

private:
    int listener_;
    std::uint16_t port_ = 0;
    std::vector<std::string> requests_;
    std::thread worker_;
};

// Return a loopback port with nothing listening on it.
std::uint16_t unused_port() {
    local_server probe({});
    return probe.port(); // closed again when probe is destroyed
}

std::string text_of(std::span<const std::byte> bytes) {
    return std::string(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size());
}

void links(test_context &context, network &link) {
    network offline;
    context.check(!offline.connected(), "a new network starts disconnected");
    tcp_socket early(offline, "127.0.0.1", 80);
    context.check(early.error() == net_error::not_connected,
                  "sockets need a connected network");
    http_client client(offline);
    context.check(client.send(http_request{.url = "http://127.0.0.1/"}).error ==
                      net_error::not_connected,
                  "requests need a connected network");
    context.check(link.connect("ssid", "password"), "the host link connects");
    context.check(link.connected(), "and reports connected");
}

void sockets(test_context &context, network &link) {
    tcp_socket no_port(link, "127.0.0.1", 0);
    context.check(no_port.error() == net_error::invalid_address,
                  "port 0 is rejected");
    tcp_socket no_host(link, "", 80);
    context.check(no_host.error() == net_error::invalid_address,
                  "an empty host is rejected");
    tcp_socket refused(link, "127.0.0.1", unused_port());
    context.check(!refused && refused.error() == net_error::connect,
                  "a refused connection reports connect");

    local_server echo({[](int descriptor) {
        std::string line = read_until(descriptor, "\n");
        send_text(descriptor, line);
        return line;
    }});
    tcp_socket socket(link, "127.0.0.1", echo.port());
    context.check(static_cast<bool>(socket), "connect to a local server");
    context.check(socket.write("ping\n") == 5, "write a line");
    std::array<std::byte, 16> buffer{};
    std::string answer;
    while (answer.size() < 5) {
        const std::size_t got = socket.read(buffer);
        if (got == 0) {
            break;
        }
        answer += text_of(std::span(buffer).first(got));
    }
    context.check(answer == "ping\n", "read the echoed line");
    context.check(socket.read(buffer) == 0 &&
                      socket.error() == net_error::closed && !socket.is_open(),
                  "reading after the peer closes reports closed");

    local_server slow({[](int descriptor) {
        std::this_thread::sleep_for(300ms);
        send_text(descriptor, "late");
        return std::string();
    }});
    inkerface::socket_options quick;
    quick.timeout = 50ms;
    tcp_socket waiting(link, "127.0.0.1", slow.port(), quick);
    context.check(waiting.read(buffer) == 0 &&
                      waiting.error() == net_error::none && waiting.is_open(),
                  "a read timeout is not an error");
    tcp_socket moved = std::move(waiting);
    context.check(!waiting.is_open() && moved.is_open(),
                  "moving a socket transfers the connection");
    std::string late;
    for (int attempt = 0; attempt < 20 && late.size() < 4; ++attempt) {
        const std::size_t got = moved.read(buffer);
        late += text_of(std::span(buffer).first(got));
    }
    context.check(late == "late", "data arriving after a timeout is read");

    local_server burst({[](int descriptor) {
        send_text(descriptor, "now");
        std::this_thread::sleep_for(200ms);
        return std::string();
    }});
    tcp_socket polled(link, "127.0.0.1", burst.port());
    std::string seen;
    for (int attempt = 0; attempt < 100 && seen.size() < 3; ++attempt) {
        const std::size_t got = polled.read(buffer, 0ms);
        seen += text_of(std::span(buffer).first(got));
        std::this_thread::sleep_for(5ms);
    }
    const auto before = std::chrono::steady_clock::now();
    const std::size_t nothing = polled.read(buffer, 0ms);
    const auto spent = std::chrono::steady_clock::now() - before;
    context.check(seen == "now" && nothing == 0 && spent < 20ms &&
                      polled.is_open(),
                  "read with no wait returns at once");
    bool closed = false;
    for (int attempt = 0; attempt < 100 && !closed; ++attempt) {
        static_cast<void>(polled.read(buffer, 0ms));
        closed = polled.error() == net_error::closed;
        std::this_thread::sleep_for(10ms);
    }
    context.check(closed, "read with no wait notices the peer closing");

    local_server plain({[](int descriptor) {
        return read_until(descriptor, "\r\n\r\n"); // never answers TLS
    }});
    inkerface::socket_options secure;
    secure.tls = true;
    secure.timeout = 500ms;
    tcp_socket tls(link, "127.0.0.1", plain.port(), secure);
    context.check(tls.error() == net_error::connect ||
                      tls.error() == net_error::tls_unavailable,
                  "a TLS handshake with a plain server fails");
}

void requests(test_context &context, network &link) {
    http_client client(link);

    local_server basic({reply("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n"
                              "X-Test: Yes\r\nConnection: close\r\n"
                              "\r\nhello")});
    const http_response hello =
        client.send({.url = basic.url("/hello")});
    context.check(hello.ok() && hello.status == 200, "a GET succeeds");
    context.check(hello.body == "hello", "the body is collected");
    context.check(hello.header("x-test") == "Yes" &&
                      hello.header("X-TEST") == "Yes",
                  "headers are found without case");
    context.check(hello.header("missing").empty(), "missing headers are empty");
    context.check(basic.finish().at(0).starts_with("GET /hello HTTP/1.1\r\n"),
                  "the request line is sent");

    local_server chunked({reply("HTTP/1.1 200 OK\r\n"
                                "Transfer-Encoding: chunked\r\n"
                                "Connection: close\r\n\r\n"
                                "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n")});
    context.check(client.send({.url = chunked.url("/")}).body == "Wikipedia",
                  "chunked bodies are decoded");

    local_server large({reply("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n"
                              "Connection: close\r\n\r\nhello")});
    http_request limited{.url = large.url("/")};
    limited.max_body_bytes = 3;
    context.check(client.send(limited).error == net_error::too_large,
                  "bodies over the limit report too_large");

    local_server streamed({reply("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n"
                                 "Connection: close\r\n\r\nstream")});
    std::string sunk;
    const http_response flow = client.send(
        {.url = streamed.url("/")}, [&sunk](std::span<const std::byte> chunk) {
            sunk += text_of(chunk);
            return true;
        });
    context.check(flow.ok() && flow.body.empty() && sunk == "stream",
                  "a sink receives the streamed body");

    local_server stopped({reply("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n"
                                "Connection: close\r\n\r\nstream")});
    const http_response refused_sink =
        client.send({.url = stopped.url("/")},
                    [](std::span<const std::byte>) { return false; });
    context.check(refused_sink.error == net_error::cancelled,
                  "a sink returning false cancels");

    local_server hops({reply("HTTP/1.1 302 Found\r\nLocation: /next\r\n"
                             "Content-Length: 0\r\nConnection: close\r\n\r\n"),
                       reply("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n"
                             "Connection: close\r\n\r\ndone")});
    const http_response followed = client.send(
        {.url = "http://127.0.0.1:" + std::to_string(hops.port()) + "/start"});
    context.check(followed.ok() && followed.body == "done",
                  "redirects are followed");
    context.check(hops.finish().at(1).starts_with("GET /next HTTP/1.1\r\n"),
                  "the redirect target is requested");

    local_server posted({reply("HTTP/1.1 201 Created\r\nContent-Length: 0\r\n"
                               "Connection: close\r\n\r\n")});
    http_request form{.method = inkerface::http_method::post,
                      .url = posted.url("/submit"),
                      .headers = {{"Content-Type",
                                   "application/x-www-form-urlencoded"}},
                      .body = "a=1"};
    const http_response created = client.send(form);
    const std::string &sent = posted.finish().at(0);
    context.check(created.ok() && created.status == 201, "a POST succeeds");
    context.check(sent.starts_with("POST /submit HTTP/1.1\r\n") &&
                      sent.find("Content-Length: 3\r\n") != std::string::npos &&
                      sent.find("Content-Type: application/x-www-form-"
                                "urlencoded\r\n") != std::string::npos &&
                      sent.ends_with("\r\n\r\na=1"),
                  "the POST carries its headers and body");

    local_server missing({reply("HTTP/1.1 404 Not Found\r\n"
                                "Content-Length: 0\r\n"
                                "Connection: close\r\n\r\n")});
    const http_response not_found = client.send({.url = missing.url("/x")});
    context.check(not_found.error == net_error::none &&
                      not_found.status == 404 && !not_found.ok(),
                  "an error status is a response, not a failure");

    local_server reused({[](int descriptor) {
        std::string both = read_request(descriptor);
        send_text(descriptor,
                  "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n"
                  "Connection: keep-alive\r\n\r\n1");
        both += read_request(descriptor);
        send_text(descriptor,
                  "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n"
                  "Connection: close\r\n\r\n2");
        return both;
    }});
    const http_response first = client.send({.url = reused.url("/a")});
    const http_response second = client.send({.url = reused.url("/b")});
    context.check(first.body == "1" && second.body == "2" &&
                      reused.finish().size() == 1,
                  "requests to one server share a connection");

    local_server slow({[](int descriptor) {
        std::string request = read_request(descriptor);
        std::this_thread::sleep_for(300ms);
        send_text(descriptor,
                  "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        return request;
    }});
    // Cancel while the server is still preparing its answer.
    const auto started = std::chrono::steady_clock::now();
    const http_response cancelled =
        client.send({.url = slow.url("/")}, [started]() {
            return std::chrono::steady_clock::now() - started > 100ms;
        });
    context.check(cancelled.error == net_error::cancelled,
                  "a cancel check stops a request in flight");
    context.check(slow.finish().size() == 1,
                  "the cancelled request had reached the server");

    context.check(client.send({.url = "not a url"}).error ==
                      net_error::invalid_address,
                  "a malformed URL is rejected");
    const http_request injected{.url = "http://127.0.0.1/",
                                .headers = {{"X-A", "1\r\nEvil: 1"}}};
    context.check(client.send(injected).error == net_error::invalid_address,
                  "header values with line breaks are rejected");
    client.close();
}

} // namespace

void run_network_tests(test_context &context) {
    network link;
    links(context, link);
    sockets(context, link);
    requests(context, link);
}

} // namespace tests
