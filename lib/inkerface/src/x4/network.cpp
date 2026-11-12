//
// Implements the X4 network link on the Arduino-ESP32 Wi-Fi station.
// Credentials are not persisted (no flash writes, nothing stored), and
// disconnect() switches the radio off, its lowest-power state.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/network.h>

#include "../net/net_time.h"

#include <Arduino.h>
#include <WiFi.h>

#include <string>

namespace inkerface {

namespace {

// Interval between link checks while joining.
constexpr unsigned long join_poll_ms = 50;

void radio_off() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

} // namespace

network::network() = default;

network::~network() {
    if (joined_) {
        radio_off();
    }
}

bool network::connect(std::string_view ssid,
                      std::string_view password,
                      std::chrono::milliseconds timeout) {
    if (connected()) {
        return true;
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
}

bool network::connected() const {
    return joined_ && WiFi.status() == WL_CONNECTED;
}

} // namespace inkerface
