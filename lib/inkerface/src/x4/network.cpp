//
// Implements the X4 network link on the Arduino-ESP32 Wi-Fi station and
// soft access point. Credentials are not persisted (no flash writes,
// nothing stored), and disconnect() and stop_access_point() switch the
// radio off, its lowest-power state.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/network.h>

#include "../net/net_platform.h"
#include "../net/net_time.h"

#include <Arduino.h>
#include <WiFi.h>

#include <algorithm>
#include <string>

#include <lwip/inet.h>

namespace inkerface {

namespace {

// Interval between link checks while joining.
constexpr unsigned long join_poll_ms = 50;

// WPA2 password limits; the radio refuses anything outside them.
constexpr std::size_t shortest_password = 8;
constexpr std::size_t longest_password = 63;
constexpr std::size_t longest_name = 32;

void radio_off() {
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// An Arduino IPAddress as an IPv4 address in network byte order.
std::uint32_t network_order(const IPAddress &address) {
    return htonl((static_cast<std::uint32_t>(address[0]) << 24U) |
                 (static_cast<std::uint32_t>(address[1]) << 16U) |
                 (static_cast<std::uint32_t>(address[2]) << 8U) |
                 static_cast<std::uint32_t>(address[3]));
}

} // namespace

namespace detail {

std::vector<std::uint32_t> broadcast_addresses(const network &link) {
    if (link.hosting()) {
        return {network_order(WiFi.softAPBroadcastIP())};
    }
    if (link.connected()) {
        return {network_order(WiFi.broadcastIP())};
    }
    return {};
}

} // namespace detail

network::network() = default;

network::~network() {
    if (joined_ || hosting_) {
        radio_off();
    }
}

bool network::connect(std::string_view ssid,
                      std::string_view password,
                      std::chrono::milliseconds timeout) {
    if (connected()) {
        return true;
    }
    if (hosting_) {
        WiFi.softAPdisconnect(true);
        hosting_ = false;
    }
    const std::string name(ssid);
    const std::string key(password);
    // Do not write the credentials to NVS: saves flash wear, keeps them out
    // of persistent storage.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(name.c_str(), key.empty() ? nullptr : key.c_str());
    const detail::deadline until(timeout);
    while (WiFi.status() != WL_CONNECTED) {
        if (until.passed()) {
            radio_off();
            joined_ = false;
            return false;
        }
        delay(join_poll_ms);
    }
    joined_ = true;
    return true;
}

void network::disconnect() {
    radio_off();
    joined_ = false;
    hosting_ = false;
}

bool network::connected() const {
    return joined_ && WiFi.status() == WL_CONNECTED;
}

std::vector<wifi_network> network::scan() {
    // The radio has to be on to listen. Scanning in AP+STA mode keeps an
    // access point, and any phone on it, alive during the scan.
    const bool was_off = !joined_ && !hosting_;
    if (was_off) {
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
    } else if (hosting_) {
        WiFi.mode(WIFI_AP_STA);
    }

    std::vector<wifi_network> found;
    const std::int16_t count = WiFi.scanNetworks();
    for (std::int16_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::uint8_t>(i);
        wifi_network entry;
        entry.ssid = WiFi.SSID(index).c_str();
        entry.rssi = static_cast<int>(WiFi.RSSI(index));
        entry.secured = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
        found.push_back(std::move(entry));
    }
    WiFi.scanDelete();

    if (was_off) {
        radio_off();
    } else if (hosting_) {
        WiFi.mode(WIFI_AP);
    }
    std::sort(found.begin(), found.end(),
              [](const wifi_network &a, const wifi_network &b) {
                  return a.rssi > b.rssi;
              });
    return found;
}

std::optional<access_point>
network::start_access_point(std::string_view name, std::string_view password) {
    if (name.empty() || name.size() > longest_name ||
        (!password.empty() && (password.size() < shortest_password ||
                               password.size() > longest_password))) {
        return std::nullopt;
    }
    const std::string ssid(name);
    const std::string key(password);

    if (joined_) {
        WiFi.disconnect(true);
        joined_ = false;
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid.c_str(), key.empty() ? nullptr : key.c_str())) {
        radio_off();
        hosting_ = false;
        return std::nullopt;
    }
    hosting_ = true;
    return access_point{std::string(WiFi.softAPIP().toString().c_str()), false};
}

void network::stop_access_point() {
    if (hosting_) {
        radio_off();
        hosting_ = false;
    }
}

bool network::hosting() const {
    return hosting_;
}

} // namespace inkerface
