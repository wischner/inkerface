//
// Implements the host Arduino stand-in: the clock, base64 and WiFiClient
// over a non-blocking POSIX TCP socket. Connecting waits with poll() for
// at most the connection timeout; writes wait for buffer space for at
// most the stream timeout. SIGPIPE is avoided with MSG_NOSIGNAL.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "WiFiClient.h"
#include "base64.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <utility>
#include <chrono>
#include <climits>
#include <string>
#include <thread>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

const auto program_start = std::chrono::steady_clock::now();

// poll() takes an int timeout.
int poll_timeout(unsigned long ms) {
    return ms > static_cast<unsigned long>(INT_MAX) ? INT_MAX
                                                    : static_cast<int>(ms);
}

// Wait until descriptor is ready for events; false on timeout or error.
bool wait_for(int descriptor, short events, unsigned long ms) {
    pollfd watched{descriptor, events, 0};
    while (true) {
        const int ready = ::poll(&watched, 1, poll_timeout(ms));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        return ready > 0;
    }
}

// Connect one resolved address, waiting at most ms. Returns the
// descriptor or -1.
int connect_address(const addrinfo &address, unsigned long ms) {
    const int descriptor = ::socket(address.ai_family,
                                    address.ai_socktype | SOCK_CLOEXEC |
                                        SOCK_NONBLOCK,
                                    address.ai_protocol);
    if (descriptor < 0) {
        return -1;
    }
    if (::connect(descriptor, address.ai_addr, address.ai_addrlen) != 0) {
        int failure = 0;
        socklen_t length = sizeof(failure);
        if (errno != EINPROGRESS || !wait_for(descriptor, POLLOUT, ms) ||
            ::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &failure, &length) !=
                0 ||
            failure != 0) {
            ::close(descriptor);
            return -1;
        }
    }
    // Requests are written whole; don't delay small segments.
    const int on = 1;
    static_cast<void>(
        ::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)));
    return descriptor;
}

} // namespace

unsigned long millis() {
    const auto elapsed = std::chrono::steady_clock::now() - program_start;
    return static_cast<unsigned long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

String base64::encode(const char *text) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const std::string input(text);
    std::string output;
    output.reserve((input.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < input.size(); i += 3) {
        const std::size_t count = std::min<std::size_t>(3, input.size() - i);
        std::uint32_t group = 0;
        for (std::size_t j = 0; j < 3; ++j) {
            const auto byte =
                j < count ? static_cast<unsigned char>(input[i + j]) : 0U;
            group = (group << 8U) | byte;
        }
        for (std::size_t j = 0; j < 4; ++j) {
            const auto index = (group >> (18U - 6U * j)) & 0x3FU;
            output += j <= count ? alphabet[index] : '=';
        }
    }
    return String(std::move(output));
}

WiFiClient::WiFiClient(int descriptor)
    : descriptor_(descriptor) {
    if (descriptor_ >= 0) {
        const int flags = ::fcntl(descriptor_, F_GETFL, 0);
        if (flags >= 0) {
            static_cast<void>(::fcntl(descriptor_, F_SETFL, flags | O_NONBLOCK));
        }
    }
}

WiFiClient::~WiFiClient() {
    stop();
}

int WiFiClient::connect(IPAddress /*ip*/, uint16_t /*port*/) {
    return 0; // the host connects by name only
}

int WiFiClient::connect(const char *host, uint16_t port) {
    stop();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host, service.c_str(), &hints, &found) != 0) {
        return 0;
    }
    for (const addrinfo *address = found; address != nullptr;
         address = address->ai_next) {
        descriptor_ = connect_address(*address, connect_timeout_ms_);
        if (descriptor_ >= 0) {
            break;
        }
    }
    ::freeaddrinfo(found);
    return descriptor_ >= 0 ? 1 : 0;
}

size_t WiFiClient::write(uint8_t byte) {
    return write(&byte, 1);
}

size_t WiFiClient::write(const uint8_t *data, size_t size) {
    size_t sent = 0;
    while (descriptor_ >= 0 && sent < size) {
        const ssize_t put =
            ::send(descriptor_, data + sent, size - sent, MSG_NOSIGNAL);
        if (put > 0) {
            sent += static_cast<size_t>(put);
        } else if (put < 0 && errno == EINTR) {
            continue;
        } else if (put < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
                   wait_for(descriptor_, POLLOUT, getTimeout())) {
            continue;
        } else {
            break;
        }
    }
    return sent;
}

int WiFiClient::available() {
    int pending = 0;
    if (descriptor_ < 0 || ::ioctl(descriptor_, FIONREAD, &pending) != 0) {
        return 0;
    }
    return pending;
}

int WiFiClient::read() {
    uint8_t byte = 0;
    return read(&byte, 1) == 1 ? byte : -1;
}

int WiFiClient::read(uint8_t *data, size_t size) {
    if (descriptor_ < 0) {
        return -1;
    }
    const ssize_t got = ::recv(descriptor_, data, size, 0);
    if (got <= 0) {
        return -1; // nothing ready, closed, or failed: see connected()
    }
    return static_cast<int>(got);
}

int WiFiClient::peek() {
    uint8_t byte = 0;
    if (descriptor_ < 0 || ::recv(descriptor_, &byte, 1, MSG_PEEK) != 1) {
        return -1;
    }
    return byte;
}

void WiFiClient::flush() {}

void WiFiClient::stop() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
}

uint8_t WiFiClient::connected() {
    if (descriptor_ < 0) {
        return 0;
    }
    uint8_t byte = 0;
    const ssize_t got = ::recv(descriptor_, &byte, 1, MSG_PEEK);
    if (got > 0) {
        return 1; // unread data
    }
    if (got == 0) {
        return 0; // closed by the peer
    }
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 1 : 0;
}

WiFiClient::operator bool() {
    return connected() != 0;
}

void WiFiClient::setConnectionTimeout(uint32_t ms) {
    connect_timeout_ms_ = ms;
}

int WiFiClient::fd() const {
    return descriptor_;
}
