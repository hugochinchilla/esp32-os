#!/usr/bin/env python3
"""End-to-end test of the remote screen (VNC) against the simulated board,
with a small viewer written here on Python's own zlib: what the viewer
decodes must equal the screenshot from the debug link, pixel for pixel.

  python3 tools/test_vnc_sim.py build/host/sim
"""
import base64
import hashlib
import os
import re
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbg  # noqa: E402

RAW, ZRLE, CURSOR = 0, 16, -239


class Viewer:
    """Just enough of a VNC viewer: 8, 16 or 32 bits a pixel, Raw and ZRLE."""

    def __init__(self, port, version=b"RFB 003.008\n", sock=None):
        self.sock = sock or socket.create_connection(("127.0.0.1", port), timeout=5)
        assert self.read(12) == b"RFB 003.008\n"
        self.sock.sendall(version)
        if version == b"RFB 003.003\n":
            assert self.read(4) == b"\0\0\0\1"
        else:
            assert self.read(2) == b"\1\1"  # one security type: none
            self.sock.sendall(b"\1")
            if version == b"RFB 003.008\n":
                assert self.read(4) == b"\0\0\0\0"
        self.sock.sendall(b"\1")
        self.width, self.height = struct.unpack(">HH", self.read(4))
        fmt = self.read(16)
        self.bpp, self.depth, self.big, self.true = fmt[0], fmt[1], fmt[2], fmt[3]
        self.max = struct.unpack(">HHH", fmt[4:10])
        self.shift = tuple(fmt[10:13])
        self.name = self.read(struct.unpack(">I", self.read(4))[0]).decode()
        self.pixels = [None] * (self.width * self.height)
        self.inflater = zlib.decompressobj()
        self.cursor = None
        self.colour_map = None
        self.cpixel = self.bpp // 8
        self.rects = 0

    def read(self, n):
        data = b""
        while len(data) < n:
            piece = self.sock.recv(n - len(data))
            assert piece, "the server closed the connection"
            data += piece
        return data

    def set_format(self, bpp, depth, big, maxes, shifts, true=1):
        self.sock.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, bpp, depth, big, true, *maxes, *shifts))
        self.bpp, self.depth, self.big, self.max, self.shift, self.true = bpp, depth, big, maxes, shifts, true
        mask = sum(m << s for m, s in zip(maxes, shifts))
        self.cpixel = bpp // 8
        self.high = False
        if true and bpp == 32 and depth <= 24 and (mask < 1 << 24 or mask & 0xFF == 0):
            self.cpixel = 3
            self.high = not mask < 1 << 24

    def set_encodings(self, encodings):
        self.sock.sendall(struct.pack(">BxH%di" % len(encodings), 2, len(encodings), *encodings))

    def request(self, incremental, x=0, y=0, w=None, h=None):
        self.sock.sendall(struct.pack(">BBHHHH", 3, incremental, x, y, w or self.width, h or self.height))

    def key(self, keysym, down):
        self.sock.sendall(struct.pack(">BBxxI", 4, down, keysym))

    def tap(self, keysym):
        self.key(keysym, 1)
        self.key(keysym, 0)

    def pointer(self, x, y, mask=0):
        self.sock.sendall(struct.pack(">BBHH", 5, mask, x, y))

    def value(self, data):
        return int.from_bytes(data, "big" if self.big else "little")

    def cvalue(self, data):
        v = self.value(data)
        return v << 8 if self.cpixel == 3 and self.high else v

    def update(self):
        """Reads one screen update (answering colour maps on the way)."""
        while True:
            kind = self.read(1)[0]
            if kind == 1:  # colour map
                first, count = struct.unpack(">xHH", self.read(5))
                assert (first, count) == (0, 256)
                self.colour_map = [struct.unpack(">HHH", self.read(6)) for _ in range(256)]
                continue
            assert kind == 0, kind
            break
        count = struct.unpack(">xH", self.read(3))[0]
        for _ in range(count):
            x, y, w, h, encoding = struct.unpack(">HHHHi", self.read(12))
            self.rects += 1
            if encoding == CURSOR:
                size = self.bpp // 8
                colours = self.read(w * h * size)
                mask = self.read((w + 7) // 8 * h)
                self.cursor = (x, y, w, h, colours, mask)
            elif encoding == RAW:
                size = self.bpp // 8
                data = self.read(w * h * size)
                for i in range(w * h):
                    self.pixels[(y + i // w) * self.width + x + i % w] = self.value(data[i * size:(i + 1) * size])
            elif encoding == ZRLE:
                assert w <= 64 and h <= 64
                packed = self.read(struct.unpack(">I", self.read(4))[0])
                tile = self.inflater.decompress(packed)
                self.zrle_tile(x, y, w, h, tile)
            else:
                raise AssertionError("encoding %d was never asked for" % encoding)
        return count

    def zrle_tile(self, x0, y0, w, h, t):
        c = self.cpixel
        kind = t[0]
        if kind == 0:
            assert len(t) == 1 + w * h * c
            values = [self.cvalue(t[1 + i * c:1 + (i + 1) * c]) for i in range(w * h)]
        elif kind == 1:
            assert len(t) == 1 + c
            values = [self.cvalue(t[1:1 + c])] * (w * h)
        else:
            assert 2 <= kind <= 16, kind
            palette = [self.cvalue(t[1 + i * c:1 + (i + 1) * c]) for i in range(kind)]
            bits = 1 if kind == 2 else 2 if kind <= 4 else 4
            row_bytes = (w * bits + 7) // 8
            body = t[1 + kind * c:]
            assert len(body) == row_bytes * h
            values = []
            for y in range(h):
                row = body[y * row_bytes:(y + 1) * row_bytes]
                for x in range(w):
                    at = x * bits
                    values.append(palette[(row[at // 8] >> (8 - bits - at % 8)) & ((1 << bits) - 1)])
        for i, v in enumerate(values):
            self.pixels[(y0 + i // w) * self.width + x0 + i % w] = v

    def close(self):
        self.sock.close()


class WebSocket:
    """A browser's side of a WebSocket, used in place of a plain socket."""

    def __init__(self, port, key=b"dGhlIHNhbXBsZSBub25jZQ==", protocol=True, piece=None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.piece = piece  # cut what is sent into frames of this size
        self.data = b""
        self.pongs = []
        self.sock.sendall(b"GET /rfb HTTP/1.1\r\nHost: board\r\nUpgrade: WebSocket\r\nConnection: Upgrade\r\n"
                          b"Sec-WebSocket-Key: " + key + b"\r\nSec-WebSocket-Version: 13\r\n" +
                          (b"Sec-WebSocket-Protocol: binary, base64\r\n" if protocol else b"") + b"\r\n")
        head = b""
        while not head.endswith(b"\r\n\r\n"):
            got = self.sock.recv(1)
            assert got, "closed during the greeting"
            head += got
        lines = head.decode().split("\r\n")
        assert lines[0] == "HTTP/1.1 101 Switching Protocols", lines[0]
        want = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest()).decode()
        assert "Sec-WebSocket-Accept: " + want in lines, lines
        assert ("Sec-WebSocket-Protocol: binary" in lines) == protocol

    def frame(self, opcode, payload):
        mask = os.urandom(4)
        size = len(payload)
        head = bytes([0x80 | opcode])
        if size < 126:
            head += bytes([0x80 | size])
        elif size < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", size)
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", size)
        self.sock.sendall(head + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(payload)))

    def sendall(self, data):
        step = self.piece or len(data)
        for at in range(0, len(data), step):
            self.frame(2, data[at:at + step])

    def exactly(self, n):
        out = b""
        while len(out) < n:
            got = self.sock.recv(n - len(out))
            if not got:
                return None
            out += got
        return out

    def recv(self, n):
        while not self.data:
            head = self.exactly(2)
            if head is None:
                return b""
            assert head[1] & 0x80 == 0, "a server must not mask"
            size = head[1]
            if size == 126:
                size = struct.unpack(">H", self.exactly(2))[0]
            payload = self.exactly(size) if size else b""
            if payload is None:
                return b""
            if head[0] & 0x0F == 0x0A:
                self.pongs.append(payload)
            elif head[0] & 0x0F == 8:
                return b""
            else:
                assert head[0] == 0x82, head
                self.data += payload
        out, self.data = self.data[:n], self.data[n:]
        return out

    def settimeout(self, seconds):
        self.sock.settimeout(seconds)

    def fileno(self):
        return self.sock.fileno()

    def close(self):
        self.sock.close()


def http_get(port, path):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as c:
        c.sendall(b"GET " + path + b" HTTP/1.1\r\nHost: board\r\n\r\n")
        answer = b""
        while True:
            got = c.recv(65536)
            if not got:
                break
            answer += got
    head, _, body = answer.partition(b"\r\n\r\n")
    return head.decode().split("\r\n"), body


def check_system_keys(board, v, page):
    """The page's buttons for the keys a browser never gets: the keys each
    one names in the page, sent as the page sends them (down in order, up in
    reverse), do on the board what the real keys do."""
    buttons = dict((label, [int(k, 0) for k in keys.split()])
                   for keys, label in re.findall(r'<button data-k="([^"]+)">([^<]+)</button>', page.decode()))
    assert sorted(buttons) == ["Alt+F4", "Alt+Tab", "Ctrl+Alt+Del", "Start"], buttons

    def press(label):
        for k in buttons[label]:
            v.key(k, 1)
        for k in reversed(buttons[label]):
            v.key(k, 0)
        settle(board)

    def titles():
        return [w["title"] for w in board.windows()]

    while len(titles()) > 0:  # whatever the checks before left open
        before = titles()
        press("Alt+F4")
        assert len(titles()) == len(before) - 1, (before, titles())
    press("Start")
    assert board.request(dbg.WINDOWS).find(b'"start_menu":true') >= 0
    press("Start")
    assert board.request(dbg.WINDOWS).find(b'"start_menu":true') < 0
    press("Ctrl+Alt+Del")
    assert titles() == ["Task Manager"], titles()
    for name in ["gui", "r"]:  # Start > Run, a second window
        board.tap(name)
    two = titles()
    assert len(two) == 2 and two[0] == "Task Manager", two
    press("Alt+Tab")
    assert titles() == two[::-1], titles()
    press("Alt+Tab")
    assert titles() == two, titles()
    press("Alt+F4")
    assert titles() == ["Task Manager"], titles()
    press("Alt+F4")
    assert titles() == [], titles()
    print("web: the page's Ctrl+Alt+Del, Alt+Tab, Alt+F4 and Start buttons work")


def check_web(board, port, web, shot):
    """The web server: the viewer page, and VNC inside a WebSocket."""
    head, body = http_get(web, b"/")
    page = body
    assert head[0] == "HTTP/1.1 200 OK" and "Content-Length: %d" % len(body) in head, head
    assert body.startswith(b"<!doctype html>") and b"WebSocket" in body and body.endswith(b"</html>\n")
    head, body = http_get(web, b"/novnc")
    assert head[0] == "HTTP/1.1 200 OK" and b"novnc/noVNC@" in body and body.endswith(b"</html>\n"), head
    # Every noVNC file the page loads has its hash in the page.
    assert body.count(b'.js":"sha384-') >= 50 and b'core/rfb.js":"sha384-' in body
    assert http_get(web, b"/novncx")[0][0] == "HTTP/1.1 404 Not Found"
    head, body = http_get(web, b"/secret.txt")
    assert head[0] == "HTTP/1.1 404 Not Found", head
    # Connections that never ask for anything (browsers open spares) do not
    # shut the door: the page still comes.
    idle = [socket.create_connection(("127.0.0.1", web), timeout=5) for _ in range(9)]
    assert http_get(web, b"/index.html")[0][0] == "HTTP/1.1 200 OK"
    for c in idle:
        c.close()

    # The picture through a WebSocket, as the page asks for it: a colour
    # map and compressed. The requests arrive cut into small frames.
    v = Viewer(None, sock=WebSocket(web, piece=3))
    assert (v.width, v.height, v.name) == (640, 480, "Chinchilla")
    v.set_format(8, 8, 0, (0, 0, 0), (0, 0, 0), true=0)
    v.set_encodings([ZRLE, RAW, CURSOR])
    v.request(0)
    v.update()
    assert bytes(v.pixels) == shot, "the picture through the WebSocket differs"
    assert v.colour_map[0xFF] == (65535, 65535, 65535) and v.cursor is not None
    # A ping is answered with its own text.
    v.sock.frame(9, b"are you there")
    v.pointer(300, 200)
    v.key(0xFFE1, 1)  # Shift down and up: harmless
    v.key(0xFFE1, 0)
    v.request(0)
    v.update()
    assert v.sock.pongs == [b"are you there"], v.sock.pongs
    assert bytes(v.pixels) == shot
    check_system_keys(board, v, page)
    _, _, shot = board.screenshot(pointer=False)

    # One viewer at a time, whichever door it came through.
    plain = Viewer(port)
    settle(board)
    v.sock.settimeout(1)
    try:
        assert v.sock.recv(1) == b"", "the WebSocket viewer was not ended"
    except ConnectionError:
        pass
    by_web = Viewer(None, sock=WebSocket(web, key=b"x3JJHMbDL1EzLkh9GBhXDw==", protocol=False))
    settle(board)
    plain.sock.settimeout(1)
    try:
        assert plain.sock.recv(1) == b"", "the VNC viewer was not ended"
    except ConnectionError:
        pass
    again = Viewer(None, sock=WebSocket(web))
    settle(board)
    by_web.sock.settimeout(1)
    try:
        assert by_web.sock.recv(1) == b"", "the first WebSocket viewer was not ended"
    except ConnectionError:
        pass
    again.set_encodings([RAW])
    again.request(0)
    again.update()
    assert bytes(again.pixels) == shot
    # An unmasked frame is against the rules: the connection ends.
    again.sock.sock.sendall(b"\x82\x01\x00")
    settle(board)
    try:
        assert again.sock.recv(1) == b""
    except ConnectionError:
        pass
    assert http_get(web, b"/")[0][0] == "HTTP/1.1 200 OK"
    print("web: the page and the WebSocket viewer work")


def caught_up(v, board, tries=10):
    """True once the viewer's picture equals the screenshot, asking for
    more updates while there are changes it has not had yet."""
    for _ in range(tries):
        _, _, shot = board.screenshot(pointer=False)
        if bytes(v.pixels) == shot:
            return True
        v.request(1)
        v.update()
    return False


def settle(board):
    """Lets the simulator's loop run (it serves the viewer between debug commands)."""
    for _ in range(3):
        board.ping()
        time.sleep(0.02)


# What the board's resistors really show: red and green levels, blue levels.
LEVELS3 = None


def check_compressor(tool):
    """The compressor's stream, 400 pieces of every kind, read back by zlib."""
    with tempfile.TemporaryDirectory() as folder:
        base = os.path.join(folder, "pieces")
        subprocess.run([tool, base], check=True)
        raw = open(base + ".raw", "rb").read()
        packed = open(base + ".z", "rb").read()
        assert len(raw) > 1 << 20
        assert zlib.decompressobj().decompress(packed) == raw, "zlib reads something else"
        print("compressor: %d bytes became %d, and zlib reads them back" % (len(raw), len(packed)))


def main():
    if len(sys.argv) > 2:
        check_compressor(sys.argv[2])
    with socket.socket() as probe, socket.socket() as probe2:  # two free ports
        probe.bind(("127.0.0.1", 0))
        probe2.bind(("127.0.0.1", 0))
        port, web = probe.getsockname()[1], probe2.getsockname()[1]
    folder = tempfile.mkdtemp()
    sim = subprocess.Popen([sys.argv[1], folder, str(port), str(web)], stdout=subprocess.PIPE, text=True)
    try:
        board = dbg.Board(sim.stdout.readline().strip(), timeout=2.0)
        dbg.wait_for_desktop(board)  # after the start-up screens, as on the board
        board.animations(False)
        _, _, shot = board.screenshot(pointer=False)

        # 1. The screen's own format (8 bits), compressed: byte for byte.
        v = Viewer(port)
        assert (v.width, v.height, v.name) == (640, 480, "Chinchilla")
        assert (v.bpp, v.depth, v.true, v.max, v.shift) == (8, 8, 1, (7, 7, 3), (5, 2, 0))
        v.set_encodings([ZRLE, CURSOR, RAW])
        v.request(0)
        v.update()
        while None in v.pixels:  # a full screen comes in one update
            raise AssertionError("part of the screen was not sent")
        assert bytes(v.pixels) == shot, "the viewer's picture differs from the screenshot"
        # The pointer's shape came too: the arrow, 12 wide, hotspot at its tip.
        assert v.cursor is not None and v.cursor[:2] == (0, 0) and v.cursor[2] >= 8, v.cursor[:4]
        first_rects = v.rects

        # 2. Nothing changed: an incremental request gets no answer yet.
        v.request(1)
        settle(board)
        v.sock.settimeout(0.3)
        try:
            v.sock.recv(1)
            raise AssertionError("an update came with nothing changed")
        except socket.timeout:
            pass
        v.sock.settimeout(5)

        # 3. A click from the viewer: About's OK button closes it; only the
        # changed part comes, and the picture is right again.
        about = board.windows()[1]
        assert about["title"] == "About Chinchilla"
        ok_x, ok_y = about["x"] + about["w"] - 50, about["y"] + about["h"] - 28
        v.pointer(ok_x, ok_y, 0)
        v.pointer(ok_x, ok_y, 1)
        v.pointer(ok_x, ok_y, 0)
        settle(board)
        assert [w["title"] for w in board.windows()] == ["Welcome to Chinchilla"]
        before = v.rects
        v.update()
        assert 0 < v.rects - before < first_rects, (v.rects - before, first_rects)
        _, _, shot = board.screenshot(pointer=False)
        assert bytes(v.pixels) == shot

        # 4. Keys from the viewer: the logo key opens the Start menu, Escape
        # closes it. The first update can come before the key's painting is
        # finished, so the viewer asks again until it has caught up.
        v.request(1)
        v.tap(0xFFEB)
        settle(board)
        assert board.request(dbg.WINDOWS).find(b'"start_menu":true') >= 0
        v.update()
        assert caught_up(v, board)
        v.request(1)
        v.tap(0xFF1B)
        settle(board)
        v.update()
        assert caught_up(v, board)

        # 5. A photograph (every pixel different: tiles sent whole), with
        # many updates through the same compressed stream on the way. The
        # Picture viewer is found in Start > Programs, wherever it is.
        updates = 0
        for down in range(12):
            for name in ["gui", "down", "right"] + ["down"] * down + ["enter"]:
                board.tap(name)
            if any("Picture viewer" in w["title"] for w in board.windows()):
                break
            while len(board.windows()) > 1:
                board.tap("esc")
                board.key("alt", True)
                board.tap("f4")
                board.key("alt", False)
            v.request(1)
            settle(board)
            v.update()
            updates += 1
        else:
            raise AssertionError("no Picture viewer in the Start menu")
        board.text("landscape.jpg")
        board.tap("enter")
        for _ in range(40):  # it decodes a little at a time
            v.request(1)
            settle(board)
            time.sleep(0.05)
            if select.select([v.sock], [], [], 0.2)[0]:
                v.update()
                updates += 1
        assert updates >= 3, updates
        _, _, shot = board.screenshot(pointer=False)
        v.request(0)
        v.update()
        assert bytes(v.pixels) == shot
        stats = board.stats()
        assert stats["receive_errors"] == 0
        v.close()

        # 6. As a browser viewer asks: 32 bits, red in the third byte. Every
        # screen colour maps to one viewer colour; white is white.
        for big in (0, 1):
            v = Viewer(port)
            v.set_format(32, 24, big, (255, 255, 255), (16, 8, 0))
            v.set_encodings([ZRLE, RAW])
            v.request(0)
            v.update()
            seen = {}
            for mine, theirs in zip(shot, v.pixels):
                assert seen.setdefault(mine, theirs) == theirs
            assert seen[0xFF] == 0xFFFFFF and seen[0x00] == 0
            assert v.cursor is None  # it did not ask for the pointer shape
            wide = dict(seen)
            v.close()
        # Colours in the top three bytes; 16 bits (5-6-5), big-endian, raw.
        v = Viewer(port)
        v.set_format(32, 24, 0, (255, 255, 255), (24, 16, 8))
        v.set_encodings([ZRLE])
        v.request(0)
        v.update()
        assert all(v.pixels[i] == wide[shot[i]] << 8 for i in range(0, len(shot), 97))
        v.close()
        v = Viewer(port, b"RFB 003.003\n")
        v.set_format(16, 16, 1, (31, 63, 31), (11, 5, 0))
        v.set_encodings([RAW])
        v.request(0)
        v.update()
        for i in range(0, len(shot), 53):
            r, g, b = wide[shot[i]] >> 16, wide[shot[i]] >> 8 & 255, wide[shot[i]] & 255
            want = ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | (b * 31 + 127) // 255
            assert v.pixels[i] == want, (i, v.pixels[i], want)
        v.close()
        # A colour map (old 8-bit viewers), protocol 3.7.
        v = Viewer(port, b"RFB 003.007\n")
        v.set_format(8, 8, 0, (0, 0, 0), (0, 0, 0), true=0)
        v.set_encodings([RAW])
        v.request(0)
        v.update()
        assert bytes(v.pixels) == shot and v.colour_map[0xFF] == (65535, 65535, 65535)
        assert all(v.colour_map[c][0] >> 8 == wide[c] >> 16 for c in wide)

        # 7. A second viewer takes over; the first is closed. Nonsense from
        # a viewer closes its connection and harms nothing.
        second = Viewer(port)
        settle(board)
        try:
            v.request(0)
            v.sock.settimeout(1)
            assert v.sock.recv(1) == b""
        except (ConnectionError, socket.timeout):
            pass
        second.sock.sendall(b"\x63garbage")
        settle(board)
        assert second.sock.recv(1) == b""
        third = Viewer(port)
        third.set_encodings([ZRLE])
        third.request(0)
        third.update()
        assert bytes(third.pixels) == shot
        third.close()
        check_web(board, port, web, shot)
        assert board.ping() < 1.0
        board.close()
        print("vnc: all checks passed")
    finally:
        sim.kill()


if __name__ == "__main__":
    main()
