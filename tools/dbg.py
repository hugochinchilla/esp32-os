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
import os
import struct
import sys
import time
import zlib

PROTOCOL_VERSION = 1

PING, INFO, SCREENSHOT, KEY, MOUSE, STATS, REBOOT = 1, 2, 3, 4, 5, 6, 7
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
            data = self.serial.read(4096)
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
                 "frames_received", "receive_errors", "keys", "mouse_events", "scanout_us")
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

    def reboot(self):
        try:
            self.request(REBOOT)
        except PortClosed:
            pass  # the simulated board closes its port as it exits

    def screenshot(self, pointer=True):
        """Returns (width, height, pixels) with one RRRGGGBB byte per pixel.

        With pointer=False the mouse pointer is left out: the picture in memory."""
        for _ in range(2):
            head = self.request(SCREENSHOT, b"" if pointer else b"\x01")
            width, height, fmt, encoding, total = struct.unpack("<HHBBI", head)
            if fmt != 1 or encoding != 1:
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
                pixels = run_length_decode(packed)
                if len(pixels) == total and zlib.crc32(pixels) == crc:
                    return width, height, pixels
            time.sleep(0.3)  # let the rest of a broken transfer drain, then retry
            self._read()
            self.pending.clear()
        raise DebugError("the screenshot arrived damaged twice")


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
    sub.add_parser("monitor", help="show the board's log text until Ctrl+C")

    p = sub.add_parser("screenshot", help="save the screen as a PNG")
    p.add_argument("--no-pointer", action="store_true", help="leave out the mouse pointer")
    p.add_argument("file")

    p = sub.add_parser("key", help="press, release or tap a key")
    p.add_argument("action", choices=["down", "up", "tap"])
    p.add_argument("name", help="a key name such as a, enter, f1, or a usage code such as 0x28")

    p = sub.add_parser("type", help="type text (letters, digits and basic punctuation)")
    p.add_argument("text")

    p = sub.add_parser("mouse", help="move, press, release, click or scroll")
    msub = p.add_subparsers(dest="action", required=True)
    m = msub.add_parser("move")
    m.add_argument("--rel", action="store_true", help="move by an amount instead of to a position")
    m.add_argument("--buttons", default="", help="buttons held meanwhile, e.g. left (for dragging)")
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
        elif args.command == "mouse":
            if args.action == "move":
                held = sum(BUTTONS[b] for b in args.buttons.split(",") if b)
                board.mouse(args.x, args.y, held, absolute=not args.rel)
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
