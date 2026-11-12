//
// Provides the Arduino entry points for the device and forwards to the
// portable program(). setup() and loop() are names mandated by the
// Arduino framework. setup() is the outermost exception boundary; if the
// program ever returns or fails, the device idles in loop().
//
// Before program() runs, setup() sets the defaults for every standard
// C++ thread (std::thread, std::jthread), so applications never touch
// ESP-IDF: ESP-IDF's own defaults are a 2 KB stack, too small for most
// work, and priority 5, above the UI's loop task (1), where a busy worker
// would freeze the UI.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/program.h>

#include <Arduino.h>
#include <esp_pthread.h>

#include <cstddef>
#include <exception>

// The UI runs on Arduino's loop task. FreeInk's rendering needs more than
// the core's 8 KB default; FreeInkUI only offers a weak 16 KB default,
// which loses to the core's weak default once libraries are archived, so
// set it here as a strong definition (Arduino's documented macro).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

// Serial console speed, matching the FreeInk sample configuration.
constexpr unsigned long serial_baud = 115200;

// Idle period once the program has ended.
constexpr unsigned long idle_ms = 1000;

// Stack of every standard thread, allocated from the heap when the thread
// starts. Enough for formatting, storage and plain-HTTP work.
constexpr std::size_t thread_stack_bytes = 8 * 1024;

// Equal to Arduino's loop task, so workers and the UI take turns.
constexpr std::size_t thread_priority = 1;

// Apply the thread defaults to threads started from the loop task, and
// (inherit_cfg) to threads those threads start.
void configure_threads() {
    esp_pthread_cfg_t config = esp_pthread_get_default_config();
    config.stack_size = thread_stack_bytes;
    config.prio = thread_priority;
    config.inherit_cfg = true;
    config.thread_name = "inkerface";
    if (esp_pthread_set_cfg(&config) != ESP_OK) {
        Serial.println("inkerface: thread defaults not applied");
    }
}

} // namespace

void setup() {
    Serial.begin(serial_baud);
    configure_threads();
    char *argv[] = {nullptr};
    try {
        static_cast<void>(program(0, argv));
    } catch (const std::exception &error) {
        Serial.print("inkerface: fatal error: ");
        Serial.println(error.what());
    } catch (...) {
        Serial.println("inkerface: fatal error");
    }
}

void loop() {
    delay(idle_ms);
}
