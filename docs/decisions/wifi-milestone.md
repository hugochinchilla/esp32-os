---
title: "Chosen in the Wi-Fi milestone"
date: 2026-11-16
by: agent
status: to-confirm
area: network
summary: "Passwords in NVS, remembered networks, the Network panel, time zone, security modes, power saving off"
---

# Chosen in the Wi-Fi milestone

Heading in the single-file log: "Chosen in the Wi-Fi milestone (asked 2026-11-16, to confirm)".


Put to the owner as options; built on the recommended one until he answers.

- Where Wi-Fi passwords are kept: the board's settings area in the flash
  (NVS), not a file on C:, so the File manager, `tools/dbg.py get` and a
  remote viewer cannot read them. This is an exception to "settings are INI
  files in `C:\.config`" and needs the owner's yes; the alternative is
  `C:\.config\network.ini` with the passwords in it. The names of the
  remembered networks and the time zone are meant to move to
  `C:\.config\network.ini` once the INI helper is on `main`; today they
  sit with the passwords behind one small port (`net::KeyStore`).
- Remembered networks: up to 8, most recently used first. At start-up and
  after a lost link the board joins the strongest remembered network in
  range, tries the next one if that fails, and looks again after 10, 20, 40
  and then every 80 seconds. Disconnect in the panel stays disconnected.
  A network is remembered only after it has connected.
- The Network panel opens from the desktop's Network icon, from Start >
  Settings > Network and from the signal icon in the tray (one click). The
  tray icon shows 0 to 4 bars, or a red cross when not connected.
- Time zone: Spanish mainland time with EU summer time (a guess from the
  Spanish sample text), set in the code until there is a Date/Time panel.
  The clock is set when the network time first arrives and every hour.
  Time server: `pool.ntp.org`.
- Security accepted: open networks, and WPA, WPA2 and WPA3 with a password.
  WEP is refused; networks that ask for a user name (enterprise) are listed
  but cannot be joined. Hidden networks: through "Other...".
- Wi-Fi power saving is off (a remote screen would lag by 100 to 300 ms).
- The debug link can scan, connect, disconnect and forget
  (`tools/dbg.py wifi`); it never returns a password.
- Firmware size with Wi-Fi: 1.10 MB (556 KB before), over ESP-IDF's
  default 1 MB app partition; it fits the 3.94 MB app area of the LittleFS
  partition table, which also keeps `nvs` (24 KB) and `phy_init`.
- ESP-IDF v5.5 does install in the cloud container (the firmware of this
  milestone was compiled and linked there, not run): `tools/setup-cloud-idf.sh`.
