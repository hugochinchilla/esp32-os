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
// Makes the board findable by name: it answers to <name>.local (mDNS) and
// announces the remote screen's VNC port and web page (0: not offered), so
// viewers that browse the network list it. Call after wifi_begin().
bool wifi_announce(const char *name, int vnc_port, int web_port);
