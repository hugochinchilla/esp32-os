#!/usr/bin/env python3
"""The serial console and the debug tool on one wire, against the simulated
board: the shell answers what is typed while tools/dbg.py goes on working.

  python3 tools/test_serial_console.py build/host/sim
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbg  # noqa: E402


def main():
    env = dict(os.environ, CHINCHILLA_STARTUP="start desktop;start console")
    sim = subprocess.Popen([sys.argv[1], tempfile.mkdtemp()], stdout=subprocess.PIPE, text=True, env=env)
    try:
        port = sim.stdout.readline().strip()
        board = dbg.Board(port, timeout=2.0)
        seen = bytearray()
        board.console = seen.extend
        dbg.wait_for_desktop(board)

        def until(text, seconds=10):
            end = time.monotonic() + seconds
            while text not in seen:
                assert time.monotonic() < end, (text, bytes(seen))
                board._read()
            at = seen.index(text) + len(text)
            before = bytes(seen[:at])
            del seen[:at]
            return before

        # The greeting was sent when the console started, before anyone was
        # listening; from here on what is typed is answered.
        board.console_send(b"echo ready\r")
        until(b"\r\nready\r\nC:\\> ")

        # A command, with the tool's requests before, during and after.
        assert board.ping() < 1.0
        board.console_send(b"echo ho")
        assert board.info()["firmware"] == "sim"
        board.console_send(b"la\r")
        assert until(b"C:\\> ") == b"echo hola\r\nhola\r\nC:\\> ", bytes(seen)

        # The process list, asked of the UI task: init, the desktop and the
        # console itself, which nobody may signal.
        board.console_send(b"ps\r")
        listing = until(b"C:\\> ").decode()
        rows = {line.split()[-1]: line.split() for line in listing.splitlines() if line[:5].strip().isdigit()}
        assert rows["init"][0] == "1" and rows["desktop"][:2] == ["2", "1"], listing
        assert rows["console"][1] == "1" and "P" in rows["console"][2], listing

        # A file written from the console is there for the tool, and one put
        # by the tool is there for the console.
        board.console_send(b"echo from the console > C:\\note.txt\r")
        until(b"C:\\> ")
        assert board.get("C:\\note.txt").strip() == b"from the console"
        board.put("C:\\tool.txt", b"from the tool\n")
        board.console_send(b"type C:\\tool.txt\r")
        assert b"from the tool" in until(b"C:\\> ")

        # A screenshot (many frames, a second or so) while a listing runs.
        board.console_send(b"help\r")
        width, height, pixels = board.screenshot()
        assert (width, height, len(pixels)) == (640, 480, 640 * 480)
        assert b"exit" in until(b"C:\\> ")

        # The desktop took none of it as keys: its windows are as they were.
        assert [w["title"] for w in board.windows()] == ["Welcome to Chinchilla", "About Chinchilla"]

        # exit: the next session greets.
        board.console_send(b"exit\r")
        assert b"Chinchilla" in until(b"C:\\> ")
        assert board.ping() < 1.0
        board.close()
    finally:
        sim.kill()
    print("serial console: all checks passed")


if __name__ == "__main__":
    main()
