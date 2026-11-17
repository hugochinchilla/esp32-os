#!/usr/bin/env python3
"""tools/dbg.py goes on reading the port while it is busy (backlog item 14b).

The link to the board has no flow control. A stand-in for the serial port
here behaves as the USB bridge is believed to: it holds 16 KB and
drops what arrives while it is full. The "board" sends numbered console lines
at a steady pace while the program does something else for a second and does
not ask for anything. Every line must still arrive, in order.

  python3 tools/test_dbg_reader.py
"""
import os
import sys
import threading
import time
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class FakeSerial:
    """A port that holds `ROOM` bytes and drops the rest."""

    ROOM = 16384

    def __init__(self):
        self.port = self.baudrate = self.dsrdtr = None
        self.timeout = 0.05
        self.dtr = self.rts = True
        self.held = bytearray()
        self.dropped = 0
        self.lock = threading.Condition()

    def open(self):
        pass

    def close(self):
        pass

    def write(self, data):
        return len(data)

    @property
    def in_waiting(self):
        with self.lock:
            return len(self.held)

    def read(self, count):
        with self.lock:
            if not self.held:
                self.lock.wait(self.timeout)
            data = bytes(self.held[:count])
            del self.held[:count]
            return data

    # The board's side: bytes on the wire.
    def feed(self, data):
        with self.lock:
            room = self.ROOM - len(self.held)
            self.held += data[:max(room, 0)]
            self.dropped += max(len(data) - room, 0)
            self.lock.notify()


def send_lines(port, count):
    for i in range(count):
        port.feed(b"line %06d of the listing, padded to a fair length\r\n" % i)
        if i % 4 == 0:
            time.sleep(0.001)  # 200 KB a second at most: twice the real link


def main():
    lines = 4000

    # The stand-in does drop when nobody reads: the test can fail.
    alone = FakeSerial()
    send_lines(alone, lines)
    assert alone.dropped > 0

    sys.modules["serial"] = types.SimpleNamespace(Serial=FakeSerial)
    import dbg

    board = dbg.Board("fake")
    seen = bytearray()
    board.console = seen.extend
    sender = threading.Thread(target=send_lines, args=(board.serial, lines))
    sender.start()
    time.sleep(1.0)  # busy with something else: nothing is asked of the port
    sender.join()
    end = time.monotonic() + 5
    while seen.count(b"\n") < lines and time.monotonic() < end:
        board._read()
    assert board.serial.dropped == 0, "%d bytes dropped" % board.serial.dropped
    got = seen.split(b"\r\n")[:-1]
    assert len(got) == lines, len(got)
    for i, line in enumerate(got):
        assert line.startswith(b"line %06d " % i), (i, line)

    # Closing stops the reader, and a closed port says so.
    board.close()
    assert not board.reader.is_alive()
    try:
        board._read()
        board._read()
        raise AssertionError("a closed port was read")
    except dbg.PortClosed:
        pass
    print("dbg reader: all %d lines arrived" % lines)


if __name__ == "__main__":
    main()
