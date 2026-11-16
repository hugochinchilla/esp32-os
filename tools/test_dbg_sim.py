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

        # Moving the pointer changes the picture there and at the new place,
        # but not the picture in memory.
        _, _, bare = board.screenshot(pointer=False)
        assert bare[240 * 640 + 320] != 0x00
        board.mouse(100, 50, absolute=True)
        _, _, moved = board.screenshot()
        assert moved[240 * 640 + 320] != 0x00
        assert moved[50 * 640 + 100] == 0x00
        assert board.screenshot(pointer=False)[2] == bare
        differ = [i for i in range(640 * 480) if moved[i] != bare[i]]
        assert differ and all(100 <= i % 640 < 112 and 50 <= i // 640 < 69 for i in differ)
        board.mouse(5, 5)  # relative
        _, _, nudged = board.screenshot()
        assert nudged[55 * 640 + 105] == 0x00

        # The demo desktop: Welcome below, About on top with the focus.
        windows = board.windows()
        assert [w["title"] for w in windows] == ["Welcome to Chinchilla", "About Chinchilla"], windows
        about = windows[1]
        assert about["focused"] and about["state"] == "normal"

        # Drag About by its title bar with mouse events.
        grab_x, grab_y = about["x"] + 150, about["y"] + 10
        board.mouse(grab_x, grab_y, absolute=True)
        board.mouse(grab_x, grab_y, buttons=1, absolute=True)
        board.mouse(grab_x - 60, grab_y - 50, buttons=1, absolute=True)
        board.mouse(grab_x - 100, grab_y - 80, buttons=1, absolute=True)
        board.mouse(grab_x - 100, grab_y - 80, buttons=0, absolute=True)
        moved_about = board.windows()[1]
        assert (moved_about["x"], moved_about["y"]) == (about["x"] - 100, about["y"] - 80), moved_about

        # What is on the screen equals a full repaint: nothing stale.
        _, _, before = board.screenshot(pointer=False)
        assert board.repaint() >= 0
        _, _, after = board.screenshot(pointer=False)
        assert before == after
        # The title bar moved with the window (dark blue at its left end).
        assert after[(moved_about["y"] + 8) * 640 + moved_about["x"] + 5] != after[(about["y"] + 8) * 640 + about["x"] + 5]

        # Enter presses About's OK button, which closes it.
        board.tap("enter")
        windows = board.windows()
        assert [w["title"] for w in windows] == ["Welcome to Chinchilla"], windows
        assert windows[0]["focused"]

        stats = board.stats()
        assert stats["keys"] == 2, stats
        assert stats["mouse_events"] == 7, stats
        assert stats["receive_errors"] == 0, stats

        try:
            board.request(0x55)
            raise AssertionError("an unknown command was accepted")
        except dbg.DebugError:
            pass

        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "shot.png")
            dbg.write_png(path, width, height, after)
            with open(path, "rb") as f:
                assert f.read(8) == b"\x89PNG\r\n\x1a\n"

        # The command line works too. Only one program can use the port.
        board.close()
        out = subprocess.run([sys.executable, dbg.__file__, "--port", port, "info"],
                             capture_output=True, text=True, timeout=20)
        assert "firmware: sim" in out.stdout, out
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "bare.png")
            out = subprocess.run([sys.executable, dbg.__file__, "--port", port, "screenshot",
                                  "--no-pointer", path], capture_output=True, text=True, timeout=20)
            assert out.returncode == 0 and os.path.exists(path), out

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
