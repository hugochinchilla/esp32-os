#!/usr/bin/env python3
"""End-to-end test of tools/dbg.py against the simulated board.

  python3 tools/test_dbg_sim.py build/host/sim
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbg  # noqa: E402


def main():
    sim = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, text=True)
    try:
        port = sim.stdout.readline().strip()
        board = dbg.Board(port, timeout=2.0)

        assert board.ping() < 1.0
        info = board.info()
        assert (info["width"], info["height"], info["firmware"]) == (640, 480, "sim"), info
        assert info["protocol"] == dbg.PROTOCOL_VERSION

        width, height, first = board.screenshot()
        assert (width, height, len(first)) == (640, 480, 640 * 480)
        # The pointer starts in the middle: its tip is black.
        assert first[240 * 640 + 320] == 0x00

        # Moving the pointer changes the picture there and at the new place.
        board.mouse(100, 50, absolute=True)
        _, _, moved = board.screenshot()
        assert moved[240 * 640 + 320] != 0x00
        assert moved[50 * 640 + 100] == 0x00
        board.mouse(5, 5)  # relative
        _, _, nudged = board.screenshot()
        assert nudged[55 * 640 + 105] == 0x00

        # A held key and a held button each change the picture; releasing restores it.
        board.key("a", True)
        board.mouse(buttons=1)
        _, _, held = board.screenshot()
        assert held != nudged
        board.key("a", False)
        board.mouse(buttons=0)
        _, _, released = board.screenshot()
        assert released == nudged

        board.type_text("Hi 1\n")
        stats = board.stats()
        assert stats["keys"] == 2 + 2 * 5 + 2, stats  # a, H i space 1 enter, shift
        assert stats["mouse_events"] == 4, stats
        assert stats["receive_errors"] == 0, stats

        try:
            board.request(0x55)
            raise AssertionError("an unknown command was accepted")
        except dbg.DebugError:
            pass

        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "shot.png")
            dbg.write_png(path, width, height, released)
            with open(path, "rb") as f:
                assert f.read(8) == b"\x89PNG\r\n\x1a\n"

        # The command line works too. Only one program can use the port.
        board.close()
        out = subprocess.run([sys.executable, dbg.__file__, "--port", port, "info"],
                             capture_output=True, text=True, timeout=20)
        assert "firmware: sim" in out.stdout, out

        board = dbg.Board(port, timeout=2.0)
        board.reboot()
        board.close()
        sim.wait(timeout=5)
        print("debug tool test passed")
    finally:
        if sim.poll() is None:
            sim.kill()


if __name__ == "__main__":
    main()
