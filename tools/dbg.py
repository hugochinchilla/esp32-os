#!/usr/bin/env python3
"""Debug tool for the board: screenshots, keyboard and mouse events, status.

The protocol is described in docs/debug-protocol.md.

  tools/dbg.py info
  tools/dbg.py screenshot shot.png
  tools/dbg.py mouse move 100 50          # to a screen position
  tools/dbg.py mouse move --rel 10 0      # by an amount
  tools/dbg.py mouse click                # left click where the pointer is
  tools/dbg.py key tap enter
  tools/dbg.py type "hello"
  tools/dbg.py stats
  tools/dbg.py tasks                      # the kernel's tasks and their processor time
  tools/dbg.py frames                     # did the desktop keep up with the monitor?
  tools/dbg.py monitor                    # show the board's log text

The port comes from --port, else the DBG_PORT variable, else /dev/ttyACM0 (the board's UART bridge on the bench).
Needs pyserial, which the ESP-IDF environment (get_idf) already has.
"""
import argparse
import binascii
import getpass
import json
import os
import queue
import re
import struct
import sys
import threading
import time
import zlib

PROTOCOL_VERSION = 1

PING, INFO, SCREENSHOT, KEY, MOUSE, STATS, REBOOT = 1, 2, 3, 4, 5, 6, 7
WINDOWS, REPAINT, TEXT, TIME, ANIMATIONS = 8, 9, 10, 11, 12
FILE_LIST, FILE_READ, FILE_WRITE, FILE_DELETE, FILE_MKDIR = 13, 14, 15, 16, 17
BOOT_SCREEN = 18
MEMORY_REPORT = 19
TASKS = 20
FAULT = 21
LOG_READ = 22
FRAMES = 23
PAINT_BENCH = 24
VIDEO_MODE = 25
WIFI = 0x20
BLUETOOTH = 0x21
MAX_REQUEST = 1024  # largest request payload the board takes
LOG, HELLO, SHOT_DATA, SHOT_END = 0x70, 0x71, 0x72, 0x73
REPLY = 0x80

STATUS = {0: "ok", 1: "unknown command", 2: "bad arguments"}


class DebugError(Exception):
    pass


class PortClosed(DebugError):
    pass


# ---- Framing ----------------------------------------------------------------

def cobs_encode(data):
    out = bytearray()
    for i, block in enumerate(data.split(b"\x00")):
        while len(block) >= 254:
            out.append(0xFF)
            out += block[:254]
            block = block[254:]
        out.append(len(block) + 1)
        out += block
    return bytes(out)


def cobs_decode(data):
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        if code == 0 or i + code > len(data):
            raise ValueError("bad COBS data")
        out += data[i + 1:i + code]
        i += code
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def crc16(data):
    return binascii.crc_hqx(data, 0xFFFF)


def make_frame(kind, seq, payload=b""):
    raw = bytes([kind, seq]) + payload
    raw += struct.pack("<H", crc16(raw))
    return b"\x00" + cobs_encode(raw) + b"\x00"


# ---- Keys -------------------------------------------------------------------

# USB HID keyboard usage codes.
KEYS = {
    "enter": 0x28, "esc": 0x29, "backspace": 0x2A, "tab": 0x2B, "space": 0x2C,
    "minus": 0x2D, "equals": 0x2E, "comma": 0x36, "period": 0x37, "slash": 0x38,
    "capslock": 0x39, "insert": 0x49, "home": 0x4A, "pageup": 0x4B,
    "delete": 0x4C, "end": 0x4D, "pagedown": 0x4E,
    "right": 0x4F, "left": 0x50, "down": 0x51, "up": 0x52,
    "ctrl": 0xE0, "shift": 0xE1, "alt": 0xE2, "gui": 0xE3,
}
KEYS.update({chr(ord("a") + i): 0x04 + i for i in range(26)})
KEYS.update({str(d): 0x1E + (d - 1) % 10 for d in range(10)})
KEYS.update({"f%d" % n: 0x39 + n for n in range(1, 13)})

TYPED = {" ": "space", "\n": "enter", "\t": "tab", "-": "minus", "=": "equals",
         ",": "comma", ".": "period", "/": "slash"}

BUTTONS = {"left": 1, "right": 2, "middle": 4}


def key_code(name):
    name = name.lower()
    if name in KEYS:
        return KEYS[name]
    try:
        return int(name, 0)
    except ValueError:
        raise DebugError("unknown key %r. Known: %s" % (name, " ".join(sorted(KEYS))))


# ---- Link -------------------------------------------------------------------

class Board:
    """A connection to the board (or to the simulated one)."""

    def __init__(self, port, baud=921600, timeout=2.0, show_logs=False, reset=False):
        import serial  # pyserial

        self.timeout = timeout
        self.show_logs = show_logs
        self.seq = 0
        self.buffer = bytearray()
        self.hello = None
        self.pending = []  # frames read but not yet asked for
        # The serial console (the board's `console:once`): with a function
        # here, what the board sends outside a frame is given to it as it
        # arrives, instead of waiting for the next frame's zero.
        self.console = None
        self.in_frame = False

        # The board's reset and boot pins hang off the port's DTR and RTS
        # lines. Linux raises both when the port opens, which leaves the board
        # alone; dsrdtr=True stops pyserial from then dropping DTR on its own,
        # which would reset the board.
        self.serial = serial.Serial()
        self.serial.port = port
        self.serial.baudrate = baud
        self.serial.timeout = 0.05
        self.serial.dsrdtr = True
        self.serial.open()
        # The port is read on a thread of its own, all the time. The link has
        # no flow control: while this program was busy with something else
        # (decoding a screenshot, waiting on a file) and did not read, the
        # PC's buffer filled and console text was lost on the way (backlog
        # item 14b). What is read waits in a queue of any length; a byte in
        # the pipe for each piece lets select() wait for it with the keyboard.
        self.arrived = queue.Queue()
        self.wake_read, self.wake_write = os.pipe()
        self.closing = False
        self.gone = False
        self.reader = threading.Thread(target=self._pump, daemon=True)
        self.reader.start()
        if reset:
            self.reset()

    def close(self):
        self.closing = True
        self.reader.join(1.0)
        self.gone = True
        self.serial.close()
        for end in (self.wake_read, self.wake_write):
            try:
                os.close(end)
            except OSError:
                pass

    # The reader thread: bytes from the port to the queue, nothing else.
    def _pump(self):
        try:
            while not self.closing:
                data = self.serial.read(1)
                waiting = self.serial.in_waiting
                if waiting:
                    data += self.serial.read(waiting)
                if data:
                    self.arrived.put(data)
                    os.write(self.wake_write, b".")
        except Exception:
            pass
        # The port went away (or the tool is closing): say so, once.
        self.arrived.put(None)
        try:
            os.write(self.wake_write, b".")
        except OSError:
            pass

    def fileno(self):
        """Readable when something has arrived: for select() beside the keyboard."""
        return self.wake_read

    # What has arrived: waits up to `wait` seconds for the first piece, then
    # takes every other piece that is already there.
    def _arrived(self, wait=0.05):
        if self.gone:
            raise PortClosed("the serial port went away")
        data = bytearray()
        try:
            piece = self.arrived.get(timeout=wait)
            while True:
                os.read(self.wake_read, 1)
                if piece is None:
                    self.gone = True
                    break
                data += piece
                piece = self.arrived.get_nowait()
        except queue.Empty:
            pass
        if self.gone and not data:
            raise PortClosed("the serial port went away")
        return bytes(data)

    def reset(self):
        """Restarts the board through the port's control lines."""
        self.serial.dtr = False
        self.serial.rts = True   # holds the board in reset
        time.sleep(0.1)
        self.serial.dtr = True   # both raised: released, normal start
        self.wait_hello(5.0)

    def wait_hello(self, timeout, fresh=True):
        """Waits for the board's hello; fresh=False accepts one already received."""
        if fresh:
            self.hello = None
        end = time.monotonic() + timeout
        while self.hello is None:
            if time.monotonic() > end:
                raise DebugError("the board did not announce itself")
            self._read()
        return self.hello

    # Reads what has arrived. Log frames and text outside frames are printed
    # if asked for; other frames are queued for _wait.
    def _read(self):
        data = self._arrived()
        shown = bytearray()
        for byte in data:
            if self.console is not None and not self.in_frame:
                if byte != 0:
                    shown.append(byte)
                else:
                    self.in_frame = True
                continue
            if byte != 0:
                self.buffer.append(byte)
                continue
            # A zero ends whatever came before it. Frames have a zero on both
            # sides; anything else between zeros is text from the board's
            # start-up, which is not part of the protocol.
            if self.buffer:
                frame = self._decode(bytes(self.buffer))
                if frame is None:
                    self._show_text(bytes(self.buffer))
                else:
                    self.in_frame = False
                    kind, seq, payload = frame
                    if kind == LOG:
                        if self.show_logs:
                            sys.stderr.write(payload.decode("utf-8", "replace"))
                            sys.stderr.flush()
                    elif kind == HELLO:
                        self.hello = payload
                    else:
                        self.pending.append(frame)
            self.buffer.clear()
        if shown:
            self.console(bytes(shown))

    def console_send(self, data):
        """Typed bytes for the board's serial console, as they are."""
        self.serial.write(data)

    def _show_text(self, text):
        if self.show_logs:
            sys.stderr.write(text.decode("utf-8", "replace"))
            sys.stderr.flush()

    @staticmethod
    def _decode(wire):
        try:
            raw = cobs_decode(wire)
        except ValueError:
            return None
        if len(raw) < 4 or crc16(raw[:-2]) != struct.unpack("<H", raw[-2:])[0]:
            return None
        return raw[0], raw[1], raw[2:-2]

    def _wait(self, wanted, timeout=None):
        end = time.monotonic() + (timeout or self.timeout)
        while True:
            # Frames nobody is waiting for (late answers to an earlier try)
            # are dropped; the first wanted one is returned.
            while self.pending:
                frame = self.pending.pop(0)
                if wanted(*frame):
                    return frame
            if time.monotonic() > end:
                return None
            self._read()

    def request(self, kind, payload=b"", tries=3):
        """Sends a command and returns the body of its reply."""
        for _ in range(tries):
            self.seq = (self.seq + 1) % 256
            seq = self.seq
            self.serial.write(make_frame(kind, seq, payload))
            frame = self._wait(lambda k, s, p: k == (kind | REPLY) and s == seq)
            if frame is not None:
                body = frame[2]
                if body and body[0] == 3:  # a file error, with its text
                    raise DebugError("the board answered: %s" % body[2:].decode("utf-8", "replace"))
                if not body or body[0] != 0:
                    raise DebugError("the board answered: %s" %
                                     STATUS.get(body[0] if body else -1, "error"))
                return body[1:]
        raise DebugError("no answer from the board on %s" % self.serial.port)

    # ---- Commands ----

    def ping(self):
        start = time.monotonic()
        if self.request(PING, b"ping") != b"ping":
            raise DebugError("ping came back changed")
        return time.monotonic() - start

    def info(self):
        body = self.request(INFO)
        version, width, height, fmt = struct.unpack_from("<BHHB", body)
        return {"protocol": version, "width": width, "height": height,
                "pixel_format": fmt, "firmware": body[6:].decode("utf-8", "replace")}

    def stats(self):
        names = ("uptime_ms", "free_internal_ram", "free_external_ram", "frames_shown",
                 "frames_received", "receive_errors", "keys", "mouse_events", "scanout_us",
                 "picture_decode_us", "picture_total_us", "picture_longest_slice_us")
        body = self.request(STATS)
        # Older firmware sends fewer numbers.
        return dict(zip(names, struct.unpack("<%dI" % (len(body) // 4), body[:len(body) // 4 * 4])))

    def key(self, name, pressed):
        self.request(KEY, struct.pack("<HB", key_code(name), 1 if pressed else 0))

    def tap(self, name, hold=0.03):
        self.key(name, True)
        time.sleep(hold)
        self.key(name, False)

    def type_text(self, text):
        for ch in text:
            shift = ch.isupper()
            name = TYPED.get(ch, ch.lower())
            if name not in KEYS:
                raise DebugError("cannot type %r yet" % ch)
            if shift:
                self.key("shift", True)
            self.tap(name)
            if shift:
                self.key("shift", False)

    def mouse(self, x=0, y=0, buttons=0, absolute=False, wheel=0, wheel_x=0):
        self.request(MOUSE, struct.pack("<BhhBbb", 1 if absolute else 0, x, y, buttons, wheel, wheel_x))

    def text(self, s):
        """Types text as characters (any Latin-1 letter, such as ñ or é)."""
        data = s.encode("utf-8")
        for i in range(0, len(data), 200):
            self.request(TEXT, data[i:i + 200])

    def doubleclick(self, x, y):
        for _ in range(2):
            self.mouse(x, y, 1, absolute=True)
            self.mouse(x, y, 0, absolute=True)

    def time(self, hms=None):
        """Reads the board's clock, or sets it to (hours, minutes, seconds)."""
        body = self.request(TIME, bytes(hms) if hms else b"")
        return tuple(body[:3])

    def animations(self, on):
        self.request(ANIMATIONS, b"\x01" if on else b"\x00")

    def memory_report(self):
        """Where the board's memory goes: heaps and video timing, as text
        (task stacks are in tasks())."""
        return self.request(MEMORY_REPORT).decode("utf-8", "replace")

    def paint_bench(self):
        """How long each kind of drawing takes on the screen, and each open
        window painted alone (Wm::paint_bench), as text. The board draws
        over its screen for about a second and paints everything again."""
        patience, self.timeout = self.timeout, 15.0
        try:
            return self.request(PAINT_BENCH, tries=1).decode("utf-8", "replace")
        finally:
            self.timeout = patience

    TASK_STATES = ("running", "ready", "blocked", "suspended", "deleted", "unknown")

    def tasks(self):
        """The kernel's tasks. Returns (totals, tasks): totals has clock_us
        (the clock the run times are counted on), cores and left_out; each
        task has id, name, state, priority, core (None: either), run_us,
        stack_free (bytes never used) and stack ("internal" or "psram")."""
        body = self.request(TASKS)
        clock_us, cores, count, left_out = struct.unpack_from("<QBBB", body)
        tasks, at = [], 11
        for _ in range(count):
            ident, state, priority, core, flags, run_us, stack_free, size = struct.unpack_from("<HBBbBQIB", body, at)
            at += 19
            tasks.append({"id": ident, "name": body[at:at + size].decode("utf-8", "replace"),
                          "state": self.TASK_STATES[min(state, 5)], "priority": priority,
                          "core": None if core < 0 else core, "run_us": run_us,
                          "stack_free": stack_free, "stack": "psram" if flags & 1 else "internal"})
            at += size
        return {"clock_us": clock_us, "cores": cores, "left_out": left_out}, tasks

    def log_text(self):
        """The system log the board keeps (what `dmesg` prints), oldest first."""
        out, at = bytearray(), 0
        while True:
            body = self.request(LOG_READ, struct.pack("<Q", at))
            nxt = struct.unpack_from("<Q", body)[0]
            out += body[8:]
            if len(body) <= 8 or nxt == at:
                break
            at = nxt
        return out.decode("utf-8", "replace")

    MISS_LINE = re.compile(r"video: (\d+) refills? missed( \((all|\d+) during a debug task list\))?")

    def missed_refills(self):
        """The missed-refill lines in the log, as (line, misses not caused by
        the debug link's task list)."""
        found = []
        for line in self.log_text().splitlines():
            m = self.MISS_LINE.search(line)
            if m:
                total = int(m.group(1))
                debug = total if m.group(3) == "all" else int(m.group(3) or 0)
                found.append((line, total - debug))
        return found

    def boot_screen(self, which):
        """Shows a start-up screen again ("selftest" runs the checks again);
        repaint() brings the desktop back."""
        self.request(BOOT_SCREEN, bytes([{"selftest": 0, "fast-next": 2, "crash": 3}[which]]))

    def fault(self, kind, pid=0):
        """Makes the board fail on purpose: "hang" (the desktop's loop spins
        until the watchdog restarts the board), "pointer" (a write through a
        null pointer), "abort", "slow" (busy for five seconds, then it
        carries on: nothing fails), or "wait" (the desktop's loop sleeps for
        ever: a hang whose record says where it waited). With a PID, as if
        that program had done it.
        The board replies first; but for "slow", it is then gone until it
        restarts."""
        args = bytes([{"hang": 0, "pointer": 1, "abort": 2, "slow": 3, "wait": 4}[kind]])
        self.request(FAULT, args + (struct.pack("<H", pid) if pid else b""))

    # ---- Wi-Fi

    LINKS = ("no radio", "not connected", "connecting", "connected")
    FAILURES = ("", "the password is not right", "the network was not found",
                "it did not answer", "the network refused")
    SECURITY = ("open", "password", "user name and password (not supported)")

    @staticmethod
    def _name(text):
        data = text if isinstance(text, bytes) else text.encode("utf-8")
        return bytes([len(data)]) + data

    def wifi_status(self):
        body = self.request(WIFI, b"\x00")
        link, failure, scanning, clock, rssi, channel = struct.unpack_from("<BBBBbB", body)
        return {"link": self.LINKS[link], "failure": self.FAILURES[failure], "scanning": bool(scanning),
                "clock_from_network": bool(clock), "rssi": rssi, "channel": channel,
                "address": ".".join(str(b) for b in body[6:10]),
                "router": ".".join(str(b) for b in body[10:14]),
                "mac": ":".join("%02X" % b for b in body[14:20]),
                "remembered": body[20],
                "ssid": body[22:22 + body[21]].decode("utf-8", "replace")}

    def wifi_list(self):
        """(scanning, networks seen, remembered names)."""
        body = self.request(WIFI, b"\x02")
        seen, i = [], 2
        for _ in range(body[1]):
            rssi, channel, security, flags, length = struct.unpack_from("<bBBBB", body, i)
            seen.append({"ssid": body[i + 5:i + 5 + length].decode("utf-8", "replace"), "rssi": rssi,
                         "channel": channel, "security": self.SECURITY[security],
                         "remembered": bool(flags & 1), "connected": bool(flags & 2)})
            i += 5 + length
        saved = []
        for _ in range(body[i]):
            length = body[i + 1]
            saved.append(body[i + 2:i + 2 + length].decode("utf-8", "replace"))
            i += 1 + length
        return bool(body[0]), seen, saved

    def wifi_scan(self, timeout=15.0):
        """Looks for networks and returns wifi_list() when the scan is done."""
        self.request(WIFI, b"\x01")
        end = time.monotonic() + timeout
        while True:
            time.sleep(0.3)
            scanning, seen, saved = self.wifi_list()
            if not scanning or time.monotonic() > end:
                return scanning, seen, saved

    def wifi_connect(self, ssid, password=None, remember=True, timeout=30.0):
        """Joins a network (password None: the remembered one) and waits for
        the result: wifi_status()."""
        payload = b"\x03" + self._name(ssid) + bytes([1 if remember else 0])
        payload += b"\x00" if password is None else b"\x01" + self._name(password)
        self.request(WIFI, payload)
        end = time.monotonic() + timeout
        while True:
            time.sleep(0.3)
            status = self.wifi_status()
            if status["link"] != "connecting" or time.monotonic() > end:
                return status

    def wifi_disconnect(self):
        self.request(WIFI, b"\x04")

    def wifi_forget(self, ssid):
        self.request(WIFI, b"\x05" + self._name(ssid))

    # ---- Bluetooth keyboards and mice. An address is written
    # "c4:9d:ed:01:02:03 random" (or "... public"), as bt_list() gives it.

    BT_STATES = ["connecting", "pairing", "reading its reports", "ready"]
    BT_KINDS = ["unknown", "keyboard", "mouse", "keyboard+mouse"]

    @staticmethod
    def _bt_address(text):
        words = text.split()
        parts = words[0].split(":") if words else []
        if len(parts) != 6 or (len(words) > 1 and words[1] not in ("random", "public")) or len(words) > 2:
            raise DebugError("not a Bluetooth address: %r" % text)
        try:
            return bytes(int(p, 16) for p in parts) + bytes([1 if words[1:] == ["random"] else 0])
        except ValueError:
            raise DebugError("not a Bluetooth address: %r" % text)

    def bt_list(self):
        """What is connected, what is heard advertising and what is
        remembered: a dict with "on", "listening", "auto_pair", "has"
        (the kinds that are ready), "links", "found" and "remembered"."""
        body = self.request(BLUETOOTH, b"\x00")
        i = 2

        def address():
            nonlocal i
            text = ":".join("%02x" % b for b in body[i:i + 6]) + (" random" if body[i + 6] else " public")
            i += 7
            return text

        def name():
            nonlocal i
            text = body[i + 1:i + 1 + body[i]].decode("utf-8", "replace")
            i += 1 + body[i]
            return text

        out = {"on": bool(body[0] & 1), "listening": bool(body[0] & 2), "auto_pair": bool(body[0] & 4),
               "has": self.BT_KINDS[body[1] & 3] if body[1] & 3 else "nothing",
               "links": [], "found": [], "remembered": []}
        count = body[i]
        i += 1
        for _ in range(count):
            entry = {"address": address(), "state": self.BT_STATES[body[i]], "kind": self.BT_KINDS[body[i + 1] & 3]}
            i += 2
            entry["name"] = name()
            out["links"].append(entry)
        count = body[i]
        i += 1
        for _ in range(count):
            entry = {"address": address(), "rssi": struct.unpack_from("<b", body, i)[0],
                     "kind": self.BT_KINDS[body[i + 1] & 3], "hid": bool(body[i + 2] & 1),
                     "pairing": bool(body[i + 2] & 2), "remembered": bool(body[i + 2] & 4)}
            i += 3
            entry["name"] = name()
            out["found"].append(entry)
        count = body[i]
        i += 1
        for _ in range(count):
            entry = {"address": address(), "kind": self.BT_KINDS[body[i] & 3], "connected": bool(body[i + 1])}
            i += 2
            entry["name"] = name()
            out["remembered"].append(entry)
        return out

    def bt_pair(self, address, timeout=40.0):
        """Connects and pairs a device that bt_list() found, and waits for
        the result: its entry in "links" once ready, None if it failed."""
        if self.request(BLUETOOTH, b"\x01" + self._bt_address(address)) != b"\x01":
            return None
        key = address.split()[0].lower()
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            time.sleep(0.3)
            mine = [l for l in self.bt_list()["links"] if l["address"].split()[0] == key]
            if not mine:
                return None
            if mine[0]["state"] == "ready":
                return mine[0]
        return None

    def bt_forget(self, address):
        self.request(BLUETOOTH, b"\x02" + self._bt_address(address))

    def bt_auto_pair(self, on):
        self.request(BLUETOOTH, bytes([3, 1 if on else 0]))

    def bt_listen(self, on):
        """Keeps the board listening for devices, as while a list is on show."""
        self.request(BLUETOOTH, bytes([4, 1 if on else 0]))

    # ---- Files. Paths are written C:\Documents\notes.txt ("/" works too).

    @staticmethod
    def _path(p):
        p = p.replace("/", "\\")
        if len(p) < 2 or p[1] != ":":
            p = "C:\\" + p.lstrip("\\")
        return p.encode("utf-8")

    def ls(self, folder):
        """[(name, is_folder, size)] of a folder."""
        out, start = [], 0
        while True:
            body = self.request(FILE_LIST, struct.pack("<H", start) + self._path(folder))
            count, more = body[0], body[1]
            i = 2
            for _ in range(count):
                is_folder, size, n = body[i], struct.unpack_from("<I", body, i + 1)[0], body[i + 5]
                out.append((body[i + 6:i + 6 + n].decode("utf-8", "replace"), bool(is_folder), size))
                i += 6 + n
            start += count
            if not more:
                return out

    def get(self, path):
        """The contents of a file on the board."""
        data = bytearray()
        while True:
            body = self.request(FILE_READ, struct.pack("<I", len(data)) + self._path(path))
            size = struct.unpack_from("<I", body)[0]
            data += body[4:]
            if len(data) >= size or len(body) <= 4:
                return bytes(data[:size])

    def put(self, path, data, progress=None):
        """Writes a file on the board (replacing it). `progress`, if given,
        is called with (bytes sent, bytes in all) after each piece."""
        p = self._path(path)
        chunk = MAX_REQUEST - 8 - len(p)
        offset = 0
        while True:
            piece = data[offset:offset + chunk]
            flags = 1 if offset == 0 else 0
            self.request(FILE_WRITE, struct.pack("<IBB", offset, flags, len(p)) + p + piece)
            offset += len(piece)
            if progress is not None:
                progress(offset, len(data))
            if offset >= len(data):
                return

    def rm(self, path):
        self.request(FILE_DELETE, self._path(path))

    def mkdir(self, path):
        self.request(FILE_MKDIR, self._path(path))

    def windows(self):
        """The windows, bottom to top, as a list of dicts."""
        return json.loads(self.request(WINDOWS).decode("utf-8"))["windows"]

    FRAME_WORK = ("key", "mouse event", "typed text", "tick", "message")
    FRAME_TOP = (None, "timer", "window painting", "panel painting")
    FRAME_BUCKETS = ("under 1 ms", "1 to 2 ms", "2 to 4 ms", "4 to 8 ms", "8 ms to the budget",
                     "1 to 2 frames", "2 to 4 frames", "4 frames or more")

    def frames(self, reset=False):
        """What the desktop counted against one video frame
        (core/sys/frames.hpp): a dict with budget_us, pieces (of work),
        painted, over (pieces longer than a frame), late (pieces with a timer
        a frame or more late), worst_us, worst_late_us, busy_us, buckets (8
        counts by length) and slow (the last pieces that were over or late,
        oldest first). With reset the board starts counting again."""
        body = self.request(FRAMES, b"\x01" if reset else b"")
        names = ("budget_us", "pieces", "painted", "over", "late", "worst_us", "worst_late_us", "busy_us")
        out = dict(zip(names, struct.unpack_from("<7IQ", body)))
        out["buckets"] = list(struct.unpack_from("<8I", body, 36))
        out["slow"] = []
        for i in range(body[68]):
            at_ms, total, paint, pixels, timers, top_us, late, pid, work, top = \
                struct.unpack_from("<7IHBB", body, 69 + 32 * i)
            out["slow"].append({"at_ms": at_ms, "total_us": total, "paint_us": paint, "pixels": pixels,
                                "timers_us": timers, "top_us": top_us, "late_us": late, "top_pid": pid,
                                "work": self.FRAME_WORK[min(work, 4)], "top": self.FRAME_TOP[min(top, 3)]})
        return out

    def video_modes(self, change_to=None):
        """The screen's modes. Returns (current, modes), each mode a dict
        with width, height, pixel_clock_hz and frame_us. With change_to the
        board changes to that mode just after it replies: nothing is saved
        and nothing asks to keep it."""
        body = self.request(VIDEO_MODE, b"" if change_to is None else bytes([change_to]))
        modes = []
        for i in range(body[0]):
            width, height, clock, frame = struct.unpack_from("<HHII", body, 2 + 12 * i)
            modes.append({"width": width, "height": height, "pixel_clock_hz": clock, "frame_us": frame})
        return body[1], modes

    def repaint(self):
        """Paints the whole screen again; returns the time it took in µs."""
        return struct.unpack("<I", self.request(REPAINT))[0]

    def reboot(self, fast=False):
        try:
            self.request(REBOOT, bytes([1]) if fast else b"")
        except PortClosed:
            pass  # the simulated board closes its port as it exits

    def screenshot(self, pointer=True):
        """Returns (width, height, pixels) with one RRRGGGBB byte per pixel.

        With pointer=False the mouse pointer is left out: the picture in memory."""
        for _ in range(2):
            flags = 2 | (0 if pointer else 1)  # bit 1: encoding 2 understood
            head = self.request(SCREENSHOT, bytes([flags]))
            width, height, fmt, encoding, total = struct.unpack("<HHBBI", head)
            if fmt != 1 or encoding not in (1, 2):
                raise DebugError("this tool does not know picture format %d/%d" % (fmt, encoding))
            seq = self.seq
            packed = bytearray()
            expected = 0
            complete = False
            while True:
                frame = self._wait(lambda k, s, p: k in (SHOT_DATA, SHOT_END) and s == seq)
                if frame is None:
                    break
                kind, _, payload = frame
                if kind == SHOT_END:
                    count, crc = struct.unpack("<HI", payload)
                    complete = count == expected
                    break
                if struct.unpack_from("<H", payload)[0] != expected:
                    break  # a chunk went missing
                packed += payload[2:]
                expected += 1
            if complete:
                pixels = (run_length_decode(packed) if encoding == 1
                          else back_reference_decode(packed, width))
                if len(pixels) == total and zlib.crc32(pixels) == crc:
                    return width, height, pixels
            time.sleep(0.3)  # let the rest of a broken transfer drain, then retry
            self._read()
            self.pending.clear()
        raise DebugError("the screenshot arrived damaged twice")


def run_console(board):
    """This terminal as the board's serial console, until Ctrl+]. Keys go to
    the board as they are typed; the debug link's frames are told apart from
    the shell's text on the way back."""
    import select
    import termios
    import tty

    out = sys.stdout.buffer

    def show(text):
        out.write(text)
        out.flush()

    board.console = show
    keyboard = sys.stdin.fileno()
    before = termios.tcgetattr(keyboard) if os.isatty(keyboard) else None
    sys.stderr.write("The board's console. Ctrl+] leaves.\r\n")
    try:
        if before is not None:
            tty.setraw(keyboard)
        board.console_send(b"\r")  # a new prompt
        while True:
            ready = select.select([keyboard, board.fileno()], [], [], 0.2)[0]
            if keyboard in ready:
                keys = os.read(keyboard, 256)
                if not keys or b"\x1d" in keys:
                    break
                board.console_send(keys)
            board._read()
    finally:
        if before is not None:
            termios.tcsetattr(keyboard, termios.TCSADRAIN, before)
        board.console = None
        sys.stderr.write("\r\n")


def frames_report(f):
    """The lines `dbg.py frames` prints for Board.frames()."""
    hz = 1e6 / f["budget_us"]
    lines = ["budget: %d us a frame (%.2f frames a second)" % (f["budget_us"], hz),
             "pieces of work: %d, %d of them painted; busy %.1f ms in all" %
             (f["pieces"], f["painted"], f["busy_us"] / 1000),
             "over budget: %d; longest piece %d us" % (f["over"], f["worst_us"]),
             "timers a frame or more late: %d; latest %d us" % (f["late"], f["worst_late_us"])]
    for name, count in zip(Board.FRAME_BUCKETS, f["buckets"]):
        if count:
            lines.append("  %-20s %d" % (name, count))
    for s in f["slow"]:
        top = ""
        if s["top"]:
            top = "; longest %s %d us, PID %d" % (s["top"], s["top_us"], s["top_pid"])
        late = "; timer %d us late" % s["late_us"] if s["late_us"] else ""
        lines.append("  at %d ms: a %s took %d us (painting %d us for %d pixels, timers %d us%s%s)" %
                     (s["at_ms"], s["work"], s["total_us"], s["paint_us"], s["pixels"], s["timers_us"], top, late))
    return lines


def wait_for_desktop(board, timeout=15):
    """Waits until the start-up screens are over (the desktop has windows)."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if board.windows():
            return
        time.sleep(0.1)
    raise DebugError("the desktop did not start")


def back_reference_decode(data, width):
    """Encoding 2: literals, and copies from a few fixed distances back."""
    distances = (1, 2, 4, width, 2 * width, 4 * width)
    out = bytearray()
    i = 0
    while i < len(data):
        control = data[i]
        i += 1
        if control < 128:
            out += data[i:i + control + 1]
            i += control + 1
        else:
            d = distances[data[i]]
            i += 1
            n = control - 125
            while n > 0:  # the source may overlap what is being written
                piece = out[len(out) - d:len(out) - d + min(n, d)]
                out += piece
                n -= len(piece)
    return bytes(out)


def run_length_decode(data):
    out = bytearray()
    i = 0
    while i < len(data):
        control = data[i]
        i += 1
        if control < 128:
            out += data[i:i + control + 1]
            i += control + 1
        else:
            out += bytes([data[i]]) * (control - 125)
            i += 1
    return bytes(out)


# ---- Pictures ---------------------------------------------------------------

# What each pixel value looks like through the board's resistors. Blue has two
# bits; its third resistor repeats the top bit, giving the levels 0, 2, 5, 7.
_LEVEL3 = [round(n * 255 / 7) for n in range(8)]
_BLUE = [_LEVEL3[n] for n in (0, 2, 5, 7)]
PALETTE = [(_LEVEL3[v >> 5], _LEVEL3[(v >> 2) & 7], _BLUE[v & 3]) for v in range(256)]


def write_png(path, width, height, pixels):
    """Writes the picture as a 256-colour PNG, using only the standard library."""
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    rows = b"".join(b"\x00" + pixels[y * width:(y + 1) * width] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b"PLTE", bytes(c for rgb in PALETTE for c in rgb)))
        f.write(chunk(b"IDAT", zlib.compress(rows, 9)))
        f.write(chunk(b"IEND", b""))


# ---- Command line -----------------------------------------------------------

def show_wifi_status(status):
    if status["link"] == "connected":
        print("connected to %(ssid)s, %(rssi)d dBm, channel %(channel)d" % status)
        print("address %(address)s, router %(router)s" % status)
        print("clock: %s" % ("set from the network" if status["clock_from_network"] else "not set yet"))
    else:
        print(status["link"] + (": " + status["failure"] if status["failure"] else ""))
    print("board %(mac)s, %(remembered)d network(s) remembered" % status)


def wifi_command(board, args):
    """The wifi command. A password is typed at a prompt that does not show
    it, and is never an argument (arguments end up in the shell's history)."""
    if args.action == "status":
        show_wifi_status(board.wifi_status())
    elif args.action in ("scan", "list"):
        _, seen, saved = board.wifi_scan() if args.action == "scan" else board.wifi_list()
        for n in seen:
            marks = ", ".join(m for m in ("connected", "remembered") if n[m])
            print("%4d dBm  ch %-2d  %-8s  %s%s" % (n["rssi"], n["channel"], n["security"].split(" ")[0],
                                                   n["ssid"], "  (%s)" % marks if marks else ""))
        missing = [s for s in saved if s not in [n["ssid"] for n in seen]]
        if missing:
            print("remembered, not in range: " + ", ".join(missing))
    elif args.action == "connect":
        ssid = args.ssid or input("Network name: ")
        _, seen, saved = board.wifi_list()
        password = None
        is_open = any(n["ssid"] == ssid and n["security"] == "open" for n in seen)
        if args.password_stdin:
            password = sys.stdin.readline().rstrip("\r\n")
        elif ssid not in saved and not is_open:
            password = getpass.getpass("Password for %s (not shown): " % ssid)
        status = board.wifi_connect(ssid, password, remember=not args.no_remember)
        show_wifi_status(status)
        if status["link"] != "connected":
            raise DebugError("not connected")
    elif args.action == "disconnect":
        board.wifi_disconnect()
    elif args.action == "forget":
        if not args.ssid:
            raise DebugError("which network? dbg.py wifi forget NAME")
        board.wifi_forget(args.ssid)


def bt_command(board, args):
    """The bt command: Bluetooth keyboards and mice."""
    def address_of(text, entries):
        # The six bytes are enough when the device is in one of the lists.
        if text is None:
            raise DebugError("which device? dbg.py bt %s ADDRESS (see dbg.py bt)" % args.action)
        for e in entries:
            if e["address"].split()[0] == text.split()[0].lower():
                return e["address"]
        return text

    if args.action in ("status", "scan"):
        if args.action == "scan":
            board.bt_listen(True)
            time.sleep(args.seconds)
        state = board.bt_list()
        if args.action == "scan":
            board.bt_listen(False)
        if not state["on"]:
            print("Bluetooth is off (C:\\.config\\bluetooth.ini, enabled) or did not start")
            return
        print("on, %s, pairing by itself %s; has: %s" % ("listening" if state["listening"] else "not listening",
                                                         "on" if state["auto_pair"] else "off", state["has"]))
        for l in state["links"]:
            print("connected   %s  %-14s %-20s %s" % (l["address"], l["kind"], l["state"], l["name"]))
        for r in state["remembered"]:
            if not r["connected"]:
                print("remembered  %s  %-14s %-20s %s" % (r["address"], r["kind"], "not connected", r["name"]))
        for f in state["found"]:
            marks = ", ".join(m for m in ("pairing", "remembered") if f[m])
            print("heard       %s  %-14s %4d dBm %-11s %s%s" % (
                f["address"], f["kind"] if f["hid"] else "not an input", f["rssi"], "", f["name"],
                "  (%s)" % marks if marks else ""))
    elif args.action == "pair":
        state = board.bt_list()
        if not state["listening"]:
            board.bt_listen(True)
            time.sleep(args.seconds)
            state = board.bt_list()
            board.bt_listen(False)
        link = board.bt_pair(address_of(args.address, state["found"]))
        if link is None:
            raise DebugError("not paired (is it in pairing mode, and in dbg.py bt scan?)")
        print("paired: %s, a %s" % (link["name"] or link["address"], link["kind"]))
    elif args.action == "forget":
        state = board.bt_list()
        board.bt_forget(address_of(args.address, state["remembered"] + state["links"]))
    elif args.action == "auto":
        if args.address not in ("on", "off"):
            raise DebugError("dbg.py bt auto on|off")
        board.bt_auto_pair(args.address == "on")


def task_table(totals, tasks, earlier=None):
    """The task list as lines of text, busiest first. Shares are of one
    core, so two cores add up to 200 %. With `earlier` (the result of an
    earlier Board.tasks()) there is also a column for the time between the
    two; a task that started in between is counted from zero."""
    def share(run_us, clock_us):
        return "%5.1f" % (100.0 * run_us / clock_us) if clock_us > 0 else "    -"

    before = {t["id"]: t["run_us"] for t in earlier[1]} if earlier else {}
    span = totals["clock_us"] - earlier[0]["clock_us"] if earlier else 0
    rows = []
    for t in tasks:
        recent = t["run_us"] - before.get(t["id"], 0)
        rows.append((recent if earlier else t["run_us"], t, recent))
    rows.sort(key=lambda r: (-r[0], r[1]["name"]))
    lines = ["%4s  %-16s %-9s %4s %4s %7s%s %12s  %6s %s" % (
        "ID", "NAME", "STATE", "PRI", "CORE", "%START", "    %NOW" if earlier else "", "TIME", "STACK", "IN")]
    for _, t, recent in rows:
        seconds = t["run_us"] / 1e6
        lines.append("%4d  %-16s %-9s %4d %4s %7s%s %12s  %6d %s" % (
            t["id"], t["name"], t["state"], t["priority"], "-" if t["core"] is None else t["core"],
            share(t["run_us"], totals["clock_us"]), "   " + share(recent, span) if earlier else "",
            "%d:%06.3f" % (seconds // 60, seconds % 60), t["stack_free"], t["stack"]))
    idle = sum(t["run_us"] for t in tasks if t["name"].startswith("IDLE"))
    summary = "%d tasks on %d cores, up %.1f s, idle %s %% of %d00 %% since start" % (
        len(tasks), totals["cores"], totals["clock_us"] / 1e6, share(idle, totals["clock_us"]).strip(),
        totals["cores"])
    if earlier:
        idle_now = sum(t["run_us"] - before.get(t["id"], 0) for t in tasks if t["name"].startswith("IDLE"))
        summary += ", %s %% in the last %.2f s" % (share(idle_now, span).strip(), span / 1e6)
    lines.append(summary)
    if totals["left_out"]:
        lines.append("%d more tasks did not fit in the reply" % totals["left_out"])
    return lines


def main(argv=None):
    parser = argparse.ArgumentParser(description="Debug tool for the board.")
    parser.add_argument("--port", default=os.environ.get("DBG_PORT", "/dev/ttyACM0"))
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--logs", action="store_true", help="also show the board's log text")
    parser.add_argument("--reset", action="store_true", help="restart the board first")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("ping", help="check that the board answers")
    sub.add_parser("info", help="screen size and firmware name")
    sub.add_parser("stats", help="uptime, free memory, counters")
    p = sub.add_parser("reboot", help="restart the board")
    p.add_argument("--fast", action="store_true",
                   help="this one start without the start-up screens (boot.ini unchanged)")
    sub.add_parser("windows", help="list the windows, bottom to top")
    p = sub.add_parser("ls", help="list a folder on the board (default C:\\)")
    p.add_argument("folder", nargs="?", default="C:\\")
    p = sub.add_parser("get", help="copy a file from the board")
    p.add_argument("remote")
    p.add_argument("local", nargs="?")
    p = sub.add_parser("put", help="copy a file to the board")
    p.add_argument("local")
    p.add_argument("remote")
    p = sub.add_parser("rm", help="delete a file or an empty folder on the board")
    p.add_argument("remote")
    p = sub.add_parser("mkdir", help="make a folder on the board")
    p.add_argument("remote")
    p = sub.add_parser("time", help="read the board's clock, or set it")
    p.add_argument("action", choices=["now", "set"])
    p.add_argument("value", nargs="?", help="HH:MM[:SS]; default: this computer's time")
    p = sub.add_parser("animations", help="caret blinking on or off (off for stable screenshots)")
    p.add_argument("state", choices=["on", "off"])
    sub.add_parser("repaint", help="paint the whole screen again and time it")
    p = sub.add_parser("mode", help="the screen's modes; with a number or a size (800x600), change to that one "
                                    "(not saved, and no question asked)")
    p.add_argument("to", nargs="?", help="a mode's number, or its size as WIDTHxHEIGHT")
    sub.add_parser("paintbench", help="time each kind of drawing and each open window on the screen (it draws "
                                      "over the screen for about a second)")
    sub.add_parser("memory", help="where the memory goes: heaps and video timing (stacks: tasks)")
    sub.add_parser("dmesg", help="the system log the board keeps, oldest first")
    p = sub.add_parser("misses", help="missed video refills in the log not caused by the debug link; exits 1 if any")
    p.add_argument("--all", action="store_true", help="also the lines caused only by dbg.py tasks")
    p = sub.add_parser("frames", help="whether the desktop keeps up with the monitor: pieces of work longer than "
                                      "one video frame, and where their time went; exits 1 if any")
    p.add_argument("--reset", action="store_true", help="start counting again after reading")
    p.add_argument("--json", action="store_true", help="the figures as JSON")
    p = sub.add_parser("tasks", help="the kernel's tasks: state, priority, core, processor time, unused stack")
    p.add_argument("--interval", type=float, default=1.0,
                   help="seconds between the two readings behind the %%NOW column (default 1; 0: one reading)")
    p.add_argument("--json", action="store_true", help="the last reading as JSON")
    p = sub.add_parser("boot", help="show a start-up screen again (repaint brings the desktop back)")
    p.add_argument("screen", choices=["selftest", "fast-next", "crash"],
                   help="fast-next: show nothing, make the next start a fast one (for before flashing). "
                        "crash: the error screen, with the last crash of this start or a made-up one")
    p = sub.add_parser("fault", help="make the board fail on purpose, to try the watchdog and the error screen "
                                     "(the report is then in C:\\crash.txt)")
    p.add_argument("kind", choices=["hang", "pointer", "abort", "slow", "wait"],
                   help="hang: the desktop's loop spins until the watchdog restarts the board. "
                        "pointer: a write through a null pointer. abort: abort(). "
                        "slow: busy for five seconds, then it carries on. "
                        "wait: the loop sleeps for ever, the hang of a program that waits")
    p.add_argument("--pid", type=int, default=0, help="as if this program had done it (see dbg.py windows)")
    p = sub.add_parser("wifi", help="Wi-Fi: status, scan, connect (asks for the password), disconnect, forget")
    p.add_argument("action", nargs="?", default="status",
                   choices=["status", "scan", "list", "connect", "disconnect", "forget"])
    p.add_argument("ssid", nargs="?", help="network name, for connect and forget")
    p.add_argument("--no-remember", action="store_true", help="connect without remembering the network")
    p.add_argument("--password-stdin", action="store_true",
                   help="read the password from standard input instead of asking")
    p = sub.add_parser("bt", help="Bluetooth keyboards and mice: status, scan, pair ADDRESS, forget ADDRESS, "
                                  "auto on|off (pairing by itself)")
    p.add_argument("action", nargs="?", default="status", choices=["status", "scan", "pair", "forget", "auto"])
    p.add_argument("address", nargs="?", help="the device, as dbg.py bt shows it (the six bytes are enough); "
                                              "on or off for auto")
    p.add_argument("--seconds", type=float, default=4.0, help="how long scan (and pair) listens first")
    sub.add_parser("monitor", help="show the board's log text until Ctrl+C")
    sub.add_parser("console", help="the board's command shell on this wire (needs `console:once` in "
                                   "C:\\.config\\startup.txt); Ctrl+] leaves")

    p = sub.add_parser("screenshot", help="save the screen as a PNG")
    p.add_argument("--no-pointer", action="store_true", help="leave out the mouse pointer")
    p.add_argument("file")

    p = sub.add_parser("key", help="press, release or tap a key")
    p.add_argument("action", choices=["down", "up", "tap"])
    p.add_argument("name", help="a key name such as a, enter, f1, or a usage code such as 0x28")

    p = sub.add_parser("type", help="type text with key presses (US keyboard)")
    p.add_argument("text")
    p = sub.add_parser("text", help="send text as characters (accents and ñ work)")
    p.add_argument("text")

    p = sub.add_parser("mouse", help="move, press, release, click or scroll")
    msub = p.add_subparsers(dest="action", required=True)
    m = msub.add_parser("move")
    m.add_argument("--rel", action="store_true", help="move by an amount instead of to a position")
    m.add_argument("--buttons", default="", help="buttons held meanwhile, e.g. left (for dragging)")
    m.add_argument("x", type=int)
    m.add_argument("y", type=int)
    m = msub.add_parser("doubleclick")
    m.add_argument("x", type=int)
    m.add_argument("y", type=int)
    for name in ("down", "up", "click"):
        m = msub.add_parser(name)
        m.add_argument("button", nargs="?", default="left", choices=sorted(BUTTONS))
    m = msub.add_parser("wheel", help="notches: positive is up; --x sideways, positive is right")
    m.add_argument("amount", type=int)
    m.add_argument("--x", type=int, default=0)

    args = parser.parse_args(argv)
    try:
        board = Board(args.port, args.baud, show_logs=args.logs or args.command == "monitor",
                      reset=args.reset)
    except DebugError as error:
        sys.exit("error: %s" % error)
    except Exception as error:  # the port could not be opened
        sys.exit("error: %s" % error)

    exit_code = 0
    try:
        if args.command == "ping":
            print("answered in %.0f ms" % (board.ping() * 1000))
        elif args.command == "info":
            for name, value in board.info().items():
                print("%s: %s" % (name, value))
        elif args.command == "stats":
            for name, value in board.stats().items():
                print("%s: %s" % (name, value))
        elif args.command == "reboot":
            board.reboot(fast=args.fast)
        elif args.command == "windows":
            for w in board.windows():
                print("%(id)d pid %(p)s z%(z)d %(x)d,%(y)d %(w)dx%(h)d %(state)s%(f)s  %(title)s"
                      % dict(w, p=w.get("pid", "?"), f=" focused" if w["focused"] else ""))
        elif args.command == "ls":
            for name, is_folder, size in board.ls(args.folder):
                print("%-8s %s" % ("<folder>" if is_folder else size, name))
        elif args.command == "get":
            data = board.get(args.remote)
            local = args.local or args.remote.replace("\\", "/").split("/")[-1]
            with open(local, "wb") as f:
                f.write(data)
            print("%s: %d bytes" % (local, len(data)))
        elif args.command == "put":
            with open(args.local, "rb") as f:
                data = f.read()
            start = time.monotonic()
            board.put(args.remote, data)
            print("%s: %d bytes in %.1f s" % (args.remote, len(data), time.monotonic() - start))
        elif args.command == "rm":
            board.rm(args.remote)
        elif args.command == "mkdir":
            board.mkdir(args.remote)
        elif args.command == "time":
            if args.action == "set":
                if args.value:
                    parts = [int(v) for v in args.value.split(":")] + [0]
                    hms = parts[:3]
                else:
                    t = time.localtime()
                    hms = [t.tm_hour, t.tm_min, t.tm_sec]
                h, m, s = board.time(hms)
            else:
                h, m, s = board.time()
            print("%02d:%02d:%02d" % (h, m, s))
        elif args.command == "animations":
            board.animations(args.state == "on")
        elif args.command == "repaint":
            print("repainted in %d us" % board.repaint())
        elif args.command == "mode":
            current, modes = board.video_modes()
            if args.to is not None:
                sizes = ["%dx%d" % (m["width"], m["height"]) for m in modes]
                to = sizes.index(args.to.lower()) if args.to.lower() in sizes else None
                if to is None and args.to.isdigit() and int(args.to) < len(modes):
                    to = int(args.to)
                if to is None:
                    sys.exit("no such mode: %s (there are: %s)" % (args.to, ", ".join(sizes)))
                current, modes = board.video_modes(to)
            for i, m in enumerate(modes):
                hz = 1e6 / m["frame_us"] if m["frame_us"] else 0
                print("%s %d: %d x %d, %.2f frames a second (%d us a frame), pixel clock %.3f MHz" %
                      ("*" if i == current else " ", i, m["width"], m["height"], hz, m["frame_us"],
                       m["pixel_clock_hz"] / 1e6))
        elif args.command == "paintbench":
            print(board.paint_bench(), end="")
        elif args.command == "memory":
            print(board.memory_report(), end="")
        elif args.command == "dmesg":
            print(board.log_text(), end="")
        elif args.command == "misses":
            lines = board.missed_refills()
            own = [(line, n) for line, n in lines if n > 0]
            for line, _ in (lines if args.all else own):
                print(line)
            debug = sum(1 for _, n in lines if n == 0)
            print("%d missed-refill line(s) not caused by the debug link; %d caused only by it (dbg.py tasks)" %
                  (len(own), debug), file=sys.stderr)
            if own:
                exit_code = 1
        elif args.command == "frames":
            f = board.frames(reset=args.reset)
            print(json.dumps(f, indent=1) if args.json else "\n".join(frames_report(f)))
            if f["over"]:
                exit_code = 1
        elif args.command == "tasks":
            earlier = None
            if args.interval > 0:
                earlier = board.tasks()
                time.sleep(args.interval)
            totals, tasks = board.tasks()
            if args.json:
                print(json.dumps({"totals": totals, "tasks": tasks}, indent=1))
            else:
                print("\n".join(task_table(totals, tasks, earlier)))
        elif args.command == "boot":
            board.boot_screen(args.screen)
            if args.screen == "fast-next":
                print("the next start skips the start-up screens, once")
            else:
                print("showing the %s screen; dbg.py repaint brings the desktop back" % args.screen)
        elif args.command == "fault":
            board.fault(args.kind, args.pid)
            print("fault sent: %s%s" % (args.kind, " as PID %d" % args.pid if args.pid else ""))
        elif args.command == "wifi":
            wifi_command(board, args)
        elif args.command == "bt":
            bt_command(board, args)
        elif args.command == "console":
            run_console(board)
        elif args.command == "monitor":
            while True:
                board._read()
                board.pending.clear()
        elif args.command == "screenshot":
            start = time.monotonic()
            width, height, pixels = board.screenshot(pointer=not args.no_pointer)
            write_png(args.file, width, height, pixels)
            print("%s: %dx%d in %.1f s" % (args.file, width, height, time.monotonic() - start))
        elif args.command == "key":
            if args.action == "tap":
                board.tap(args.name)
            else:
                board.key(args.name, args.action == "down")
        elif args.command == "type":
            board.type_text(args.text)
        elif args.command == "text":
            board.text(args.text)
        elif args.command == "mouse":
            if args.action == "move":
                held = sum(BUTTONS[b] for b in args.buttons.split(",") if b)
                board.mouse(args.x, args.y, held, absolute=not args.rel)
            elif args.action == "doubleclick":
                board.doubleclick(args.x, args.y)
            elif args.action == "wheel":
                board.mouse(wheel=args.amount, wheel_x=args.x)
            elif args.action == "down":
                board.mouse(buttons=BUTTONS[args.button])
            elif args.action == "up":
                board.mouse(buttons=0)
            else:
                board.mouse(buttons=BUTTONS[args.button])
                time.sleep(0.03)
                board.mouse(buttons=0)
    except DebugError as error:
        sys.exit("error: %s" % error)
    except KeyboardInterrupt:
        pass
    finally:
        board.close()
    if exit_code:
        sys.exit(exit_code)


if __name__ == "__main__":
    main()
