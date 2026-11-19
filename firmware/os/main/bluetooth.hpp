// The ESP32-S3's Bluetooth LE radio as the host of keyboards and mice,
// behind the core's port (core/input/bluetooth.hpp).
#pragma once

#include "core/input/bluetooth.hpp"
#include "core/ports.hpp"

// Starts the Bluetooth stack (NimBLE). The keys and mouse movements of the
// devices that connect go to `sink`, as messages to the UI task, so `sink`
// is only ever called there. Returns false if the stack did not start; the
// port then reports that it is off. Not called at all when Bluetooth is
// turned off in bluetooth.ini: the stack then takes no memory.
bool bluetooth_begin(ports::InputSink &sink);
bt::Port &bluetooth_port();
// Keys and button changes that found the UI task's queue full for too long.
uint32_t bluetooth_lost_events();
