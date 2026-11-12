//
// Implements the host network link. The operating system's network is
// always used, so connect() succeeds at once and ignores the credentials;
// disconnect() only marks the link down, which lets programs exercise
// their not-connected paths on the host.
//
// SIGPIPE is ignored for the process once a network exists: OpenSSL
// writes to sockets with write(), and a peer that closed mid-write would
// otherwise terminate the program instead of reporting an error.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/network.h>

#include <csignal>

namespace inkerface {

network::network() {
    static_cast<void>(std::signal(SIGPIPE, SIG_IGN));
}

network::~network() = default;

bool network::connect(std::string_view /*ssid*/,
                      std::string_view /*password*/,
                      std::chrono::milliseconds /*timeout*/) {
    joined_ = true;
    return true;
}

void network::disconnect() {
    joined_ = false;
}

bool network::connected() const {
    return joined_;
}

} // namespace inkerface
