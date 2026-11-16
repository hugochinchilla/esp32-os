// The ESP32's Wi-Fi radio and settings area, behind the core's ports
// (core/net/wifi.hpp).
#pragma once

#include "core/net/wifi.hpp"

// Starts the radio as a station (not connected to anything yet). Returns
// false if it did not start; the port then reports Link::Off.
bool wifi_begin();
net::WifiPort &wifi_port();
// Small settings in the flash (NVS), outside the drives.
net::KeyStore &settings_store();
