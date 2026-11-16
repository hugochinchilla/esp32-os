#!/usr/bin/env python3
"""End-to-end test of tools/dbg.py against the simulated board.

  python3 tools/test_dbg_sim.py build/host/sim
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbg  # noqa: E402


def main():
    pc_folder = tempfile.mkdtemp()  # drive D: of the simulator
    sim = subprocess.Popen([sys.argv[1], pc_folder], stdout=subprocess.PIPE, text=True)
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

        # Files: the RAM disk has the samples; put, list, get, delete.
        names = [n for n, _, _ in board.ls("C:\\")]
        assert "docs" in names and "readme.txt" in names, names
        data = bytes(range(256)) * 1200  # 300 KB, every byte value
        board.put("C:\\docs\\big.bin", data)
        entries = {n: (d, s) for n, d, s in board.ls("C:/docs")}
        assert entries["big.bin"] == (False, len(data)), entries
        assert board.get("C:\\docs\\big.bin") == data
        board.put("C:\\docs\\big.bin", b"short now")  # replacing truncates
        assert board.get("C:\\docs\\big.bin") == b"short now"
        board.mkdir("C:\\new")
        try:
            board.rm("C:\\docs")
            raise AssertionError("a folder with files in it was removed")
        except dbg.DebugError as error:
            assert "not empty" in str(error), error
        board.rm("C:\\docs\\big.bin")
        board.rm("C:\\new")
        assert "big.bin" not in [n for n, _, _ in board.ls("C:\\docs")]
        try:
            board.get("C:\\nothing.txt")
            raise AssertionError("a missing file was read")
        except dbg.DebugError as error:
            assert "not found" in str(error), error
        # Drive D: is a folder of this computer.
        board.put("D:\\from-board.txt", "año\r\n".encode("latin-1"))
        with open(os.path.join(pc_folder, "from-board.txt"), "rb") as f:
            assert f.read() == "año\r\n".encode("latin-1")
        assert [n for n, _, _ in board.ls("D:\\")] == ["from-board.txt"]

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

        # Wi-Fi against the simulator's pretend radio.
        status = board.wifi_status()
        assert status["link"] == "not connected" and status["remembered"] == 0, status
        scanning, seen, saved = board.wifi_scan()
        assert not scanning and saved == []
        assert [n["ssid"] for n in seen] == ["Casa Chinchilla", "Cafe & Churros", "Vecinos 2.4G"], seen
        assert seen[1]["security"] == "open" and seen[0]["rssi"] == -48
        status = board.wifi_connect("Casa Chinchilla", "not-the-password")
        assert status["link"] == "not connected" and "password" in status["failure"], status
        assert board.wifi_list()[2] == []
        status = board.wifi_connect("Casa Chinchilla", "semillas-y-heno")
        assert status["link"] == "connected" and status["ssid"] == "Casa Chinchilla", status
        assert status["address"] == "192.168.1.37" and status["rssi"] == -48
        _, seen, saved = board.wifi_list()
        assert saved == ["Casa Chinchilla"] and seen[0]["connected"] and seen[0]["remembered"]
        board.wifi_disconnect()
        assert board.wifi_status()["link"] == "not connected"
        status = board.wifi_connect("Casa Chinchilla")  # the remembered password
        assert status["link"] == "connected", status
        # The clock was set from the network time (this computer's, in Madrid time).
        for _ in range(20):
            if board.wifi_status()["clock_from_network"]:
                break
            time.sleep(0.1)
        assert board.wifi_status()["clock_from_network"]
        board.wifi_forget("Casa Chinchilla")
        assert board.wifi_list()[2] == [] and board.wifi_status()["link"] == "not connected"
        try:
            board.request(dbg.WIFI, b"\x03\x40")  # a name longer than the frame
            raise AssertionError("a bad Wi-Fi command was accepted")
        except dbg.DebugError:
            pass

        # The command line works too. Only one program can use the port.
        board.close()
        out = subprocess.run([sys.executable, dbg.__file__, "--port", port, "info"],
                             capture_output=True, text=True, timeout=20)
        assert "firmware: sim" in out.stdout, out
        # The password goes in on standard input, never as an argument.
        out = subprocess.run([sys.executable, dbg.__file__, "--port", port, "wifi", "connect",
                              "Casa Chinchilla", "--password-stdin", "--no-remember"],
                             input="semillas-y-heno\n", capture_output=True, text=True, timeout=30)
        assert "connected to Casa Chinchilla" in out.stdout and "0 network(s) remembered" in out.stdout, out
        out = subprocess.run([sys.executable, dbg.__file__, "--port", port, "wifi", "scan"],
                             capture_output=True, text=True, timeout=30)
        assert "Casa Chinchilla  (connected)" in out.stdout, out
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
