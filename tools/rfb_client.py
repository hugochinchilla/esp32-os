#!/usr/bin/env python3
"""A small VNC (RFB 3.8) client for checking the board's remote screen without
installing a viewer: connects with no password, saves the screen as a PNG,
sends mouse and key events, and times a full-screen update.

  python3 tools/rfb_client.py HOST[:PORT] shot out.png
  python3 tools/rfb_client.py HOST time            # full-screen update time
  python3 tools/rfb_client.py HOST click X Y
  python3 tools/rfb_client.py HOST key KEYSYM      # e.g. 0xff0d (Enter)

Only the raw encoding is asked for, so any RFB server can answer.
"""
import socket
import struct
import sys
import time
import zlib


class Rfb:
    def __init__(self, host, port=5900, timeout=10.0):
        self.s = socket.create_connection((host, port), timeout=timeout)
        version = self._read(12)
        if not version.startswith(b"RFB "):
            raise RuntimeError("not a VNC server: %r" % version)
        self.s.sendall(b"RFB 003.008\n")
        n = self._read(1)[0]
        if n == 0:
            raise RuntimeError("server refused: %r" % self._reason())
        types = self._read(n)
        if 1 not in types:
            raise RuntimeError("server wants a password or other security: %r" % list(types))
        self.s.sendall(b"\x01")
        if struct.unpack(">I", self._read(4))[0] != 0:
            raise RuntimeError("security failed: %r" % self._reason())
        self.s.sendall(b"\x01")  # shared
        self.width, self.height = struct.unpack(">HH", self._read(4))
        self.server_format = self._read(16)
        self.name = self._read(struct.unpack(">I", self._read(4))[0]).decode("utf-8", "replace")
        # 32 bits per pixel, true colour, little endian, 8 bits per channel.
        fmt = struct.pack(">BBBBHHHBBB3x", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
        self.s.sendall(struct.pack(">B3x", 0) + fmt)
        self.s.sendall(struct.pack(">BxHi", 2, 1, 0))  # encodings: raw only
        self.pixels = bytearray(self.width * self.height * 4)

    def _read(self, n):
        out = b""
        while len(out) < n:
            part = self.s.recv(n - len(out))
            if not part:
                raise RuntimeError("connection closed")
            out += part
        return out

    def _reason(self):
        return self._read(struct.unpack(">I", self._read(4))[0])

    def update(self, incremental=False):
        """Asks for the screen and reads until one update has arrived."""
        self.s.sendall(struct.pack(">BBHHHH", 3, 1 if incremental else 0, 0, 0, self.width, self.height))
        while True:
            kind = self._read(1)[0]
            if kind == 0:
                count = struct.unpack(">xH", self._read(3))[0]
                for _ in range(count):
                    x, y, w, h, enc = struct.unpack(">HHHHi", self._read(12))
                    if enc != 0:
                        raise RuntimeError("unexpected encoding %d" % enc)
                    data = self._read(w * h * 4)
                    for row in range(h):
                        o = ((y + row) * self.width + x) * 4
                        self.pixels[o:o + w * 4] = data[row * w * 4:(row + 1) * w * 4]
                return count
            if kind == 1:  # colour map entries
                first, n = struct.unpack(">xHH", self._read(5))
                self._read(n * 6)
            elif kind == 2:  # bell
                continue
            elif kind == 3:  # cut text
                self._read(3)
                self._read(struct.unpack(">I", self._read(4))[0])
            else:
                raise RuntimeError("unknown message %d" % kind)

    def pointer(self, x, y, buttons=0):
        self.s.sendall(struct.pack(">BBHH", 5, buttons, x, y))

    def key(self, keysym, down):
        self.s.sendall(struct.pack(">BBxxI", 4, 1 if down else 0, keysym))

    def png(self, path):
        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
        raw = bytearray()
        for y in range(self.height):
            raw.append(0)
            row = self.pixels[y * self.width * 4:(y + 1) * self.width * 4]
            for x in range(self.width):
                b, g, r = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
                raw += bytes((r, g, b))
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))

    def rgb_rows(self):
        """The screen as (r, g, b) per pixel, row by row."""
        p = self.pixels
        return [[(p[(y * self.width + x) * 4 + 2], p[(y * self.width + x) * 4 + 1], p[(y * self.width + x) * 4])
                 for x in range(self.width)] for y in range(self.height)]


def main():
    host, what = sys.argv[1], sys.argv[2]
    port = 5900
    if ":" in host:
        host, port = host.rsplit(":", 1)
        port = int(port)
    c = Rfb(host, port)
    print("connected: %dx%d \"%s\"" % (c.width, c.height, c.name))
    if what == "shot":
        c.update()
        c.png(sys.argv[3])
        print("saved", sys.argv[3])
    elif what == "time":
        t = time.monotonic()
        c.update()
        print("full screen in %.0f ms" % ((time.monotonic() - t) * 1000))
    elif what == "click":
        x, y = int(sys.argv[3]), int(sys.argv[4])
        c.pointer(x, y, 0); c.pointer(x, y, 1); c.pointer(x, y, 0)
    elif what == "key":
        k = int(sys.argv[3], 0)
        c.key(k, True); c.key(k, False)
    time.sleep(0.2)


if __name__ == "__main__":
    main()
