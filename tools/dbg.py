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
  tools/dbg.py monitor                    # show the board's log text

The port comes from --port, else the DBG_PORT variable, else /dev/ttyACM0 (the board's UART bridge on the bench).
Needs pyserial, which the ESP-IDF environment (get_idf) already has.
"""
import argparse
import binascii
import getpass
import json
import os
import struct
import sys
import time
import zlib

PROTOCOL_VERSION = 1

PING, INFO, SCREENSHOT, KEY, MOUSE, STATS, REBOOT = 1, 2, 3, 4, 5, 6, 7
WINDOWS, REPAINT, TEXT, TIME, ANIMATIONS = 8, 9, 10, 11, 12
FILE_LIST, FILE_READ, FILE_WRITE, FILE_DELETE, FILE_MKDIR = 13, 14, 15, 16, 17
BOOT_SCREEN = 18
WIFI = 0x20
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
        if reset:
            self.reset()

    def close(self):
        self.serial.close()

    def reset(self):
        """Restarts the board through the port's control lines."""
        self.serial.dtr = False
        self.serial.rts = True   # holds the board in reset
        time.sleep(0.1)
        self.serial.dtr = True   # both raised: released, normal start
        self.wait_hello(5.0)

    def wait_hello(self, timeout):
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
        try:
            # Wait for the first byte (up to the timeout), then take whatever
            # else has arrived, so a reply is handled as soon as it is in.
            data = self.serial.read(1)
            waiting = self.serial.in_waiting
            if waiting:
                data += self.serial.read(waiting)
        except Exception:
            raise PortClosed("the serial port went away")
        for byte in data:
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

    def mouse(self, x=0, y=0, buttons=0, absolute=False, wheel=0):
        self.request(MOUSE, struct.pack("<BhhBb", 1 if absolute else 0, x, y, buttons, wheel))

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

    def boot_screen(self, which):
        """Shows a start-up screen again ("selftest" runs the checks again,
        "splash"); repaint() brings the desktop back."""
        self.request(BOOT_SCREEN, bytes([{"selftest": 0, "splash": 1}[which]]))

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

    # ---- Files. Paths are written C:\docs\notes.txt ("/" works too).

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

    def put(self, path, data):
        """Writes a file on the board (replacing it)."""
        p = self._path(path)
        chunk = MAX_REQUEST - 8 - len(p)
        offset = 0
        while True:
            piece = data[offset:offset + chunk]
            flags = 1 if offset == 0 else 0
            self.request(FILE_WRITE, struct.pack("<IBB", offset, flags, len(p)) + p + piece)
            offset += len(piece)
            if offset >= len(data):
                return

    def rm(self, path):
        self.request(FILE_DELETE, self._path(path))

    def mkdir(self, path):
        self.request(FILE_MKDIR, self._path(path))

    def windows(self):
        """The windows, bottom to top, as a list of dicts."""
        return json.loads(self.request(WINDOWS).decode("utf-8"))["windows"]

    def repaint(self):
        """Paints the whole screen again; returns the time it took in µs."""
        return struct.unpack("<I", self.request(REPAINT))[0]

    def reboot(self):
        try:
            self.request(REBOOT)
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
    sub.add_parser("reboot", help="restart the board")
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
    p = sub.add_parser("boot", help="show a start-up screen again (repaint brings the desktop back)")
    p.add_argument("screen", choices=["selftest", "splash"])
    p = sub.add_parser("wifi", help="Wi-Fi: status, scan, connect (asks for the password), disconnect, forget")
    p.add_argument("action", nargs="?", default="status",
                   choices=["status", "scan", "list", "connect", "disconnect", "forget"])
    p.add_argument("ssid", nargs="?", help="network name, for connect and forget")
    p.add_argument("--no-remember", action="store_true", help="connect without remembering the network")
    p.add_argument("--password-stdin", action="store_true",
                   help="read the password from standard input instead of asking")
    sub.add_parser("monitor", help="show the board's log text until Ctrl+C")

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
    m = msub.add_parser("wheel")
    m.add_argument("amount", type=int)

    args = parser.parse_args(argv)
    try:
        board = Board(args.port, args.baud, show_logs=args.logs or args.command == "monitor",
                      reset=args.reset)
    except DebugError as error:
        sys.exit("error: %s" % error)
    except Exception as error:  # the port could not be opened
        sys.exit("error: %s" % error)

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
            board.reboot()
        elif args.command == "windows":
            for w in board.windows():
                print("%(id)d z%(z)d %(x)d,%(y)d %(w)dx%(h)d %(state)s%(f)s  %(title)s"
                      % dict(w, f=" focused" if w["focused"] else ""))
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
        elif args.command == "boot":
            board.boot_screen(args.screen)
            print("showing the %s screen; dbg.py repaint brings the desktop back" % args.screen)
        elif args.command == "wifi":
            wifi_command(board, args)
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
                board.mouse(wheel=args.amount)
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


if __name__ == "__main__":
    main()
