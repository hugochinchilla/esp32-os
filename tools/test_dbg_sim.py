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
        dbg.wait_for_desktop(board)  # after the start-up screens, as on the board

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

        # The desktop against the video frame (core/sys/frames.hpp): what
        # was done above is counted, and the count starts again on request.
        frames = board.frames(reset=True)
        assert frames["budget_us"] == 16800, frames
        assert frames["pieces"] > 10 and frames["painted"] >= 4, frames  # the drag, the drop, the key
        assert sum(frames["buckets"]) == frames["pieces"], frames
        assert frames["over"] <= len(frames["slow"]) <= 16, frames
        assert frames["busy_us"] >= frames["worst_us"] > 0, frames
        again = board.frames()
        assert again["pieces"] < frames["pieces"] and again["slow"] == [], again
        assert dbg.frames_report(frames)[0] == "budget: 16800 us a frame (59.52 frames a second)"
        assert stats["mouse_events"] == 7, stats
        assert stats["receive_errors"] == 0, stats

        # The kernel's tasks (the simulator makes a list up), read twice.
        totals, tasks = board.tasks()
        assert totals["cores"] == 2 and totals["left_out"] == 0 and totals["clock_us"] > 0, totals
        by_name = {t["name"]: t for t in tasks}
        assert {"ui", "IDLE0", "IDLE1", "wifi"} <= set(by_name), by_name
        assert by_name["ui"]["core"] is None and by_name["IDLE1"]["core"] == 1
        assert by_name["ui"]["state"] == "running" and by_name["wifi"]["priority"] == 23
        time.sleep(0.05)
        later = board.tasks()
        assert later[0]["clock_us"] > totals["clock_us"]
        lines = dbg.task_table(later[0], later[1], (totals, tasks))
        assert lines[0].split() == ["ID", "NAME", "STATE", "PRI", "CORE", "%START", "%NOW", "TIME", "STACK", "IN"]
        assert lines[1].split()[1] == "IDLE0" and lines[1].split()[5:7] == ["93.0", "93.0"], lines[1]
        assert "idle 182.0 % of 200 % since start, 182.0 % in the last" in lines[-1], lines[-1]
        # A task that started between the readings counts from zero, and
        # one reading alone has no %NOW column.
        fresh = dbg.task_table({"clock_us": 2000000, "cores": 2, "left_out": 3},
                               [dict(by_name["ui"], id=99, name="job", run_us=500000)],
                               ({"clock_us": 1000000, "cores": 2, "left_out": 0}, []))
        assert fresh[1].split()[5:7] == ["25.0", "50.0"], fresh
        assert fresh[-1] == "3 more tasks did not fit in the reply"
        assert "%NOW" not in dbg.task_table(totals, tasks)[0]

        # The log read (the simulator has no video, so no missed refills), and
        # how the missed-refill lines are told apart.
        assert isinstance(board.log_text(), str)
        assert board.missed_refills() == []
        real = board.log_text
        board.log_text = lambda: (
            "I (900) os: started\n"
            "W (2000) os: video: 1 refill missed, worst 412 us late, last at line 240\n"
            "W (3000) os: video: 2 refills missed (all during a debug task list), worst 812 us late, last at line 96\n"
            "W (4000) os: video: 5 refills missed (2 during a debug task list), worst 640 us late, last at line 30\n")
        assert [n for _, n in board.missed_refills()] == [1, 0, 3], board.missed_refills()
        board.log_text = real

        # Files: the RAM disk has the samples; put, list, get, delete.
        names = [n for n, _, _ in board.ls("C:\\")]
        assert "Documents" in names and "readme.txt" in names, names
        data = bytes(range(256)) * 1200  # 300 KB, every byte value
        board.put("C:\\Documents\\big.bin", data)
        entries = {n: (d, s) for n, d, s in board.ls("C:/Documents")}
        assert entries["big.bin"] == (False, len(data)), entries
        assert board.get("C:\\Documents\\big.bin") == data
        board.put("C:\\Documents\\big.bin", b"short now")  # replacing truncates
        assert board.get("C:\\Documents\\big.bin") == b"short now"
        board.mkdir("C:\\new")
        try:
            board.rm("C:\\Documents")
            raise AssertionError("a folder with files in it was removed")
        except dbg.DebugError as error:
            assert "not empty" in str(error), error
        board.rm("C:\\Documents\\big.bin")
        board.rm("C:\\new")
        assert "big.bin" not in [n for n, _, _ in board.ls("C:\\Documents")]
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
        # The error screen after a crash, drawn on request (the simulator
        # shows a made-up report), and the desktop back afterwards.
        before = board.screenshot(pointer=False)[2]
        board.boot_screen("crash")
        crash = board.screenshot(pointer=False)[2]
        assert crash != before and crash[:640 * 16] == bytes(640 * 16), "the error screen is black at the top"
        assert sum(1 for p in crash if p != 0) > 2000
        board.repaint()
        assert board.screenshot(pointer=False)[2] == before
        # A fault on purpose: the simulator answers and carries on (a PC
        # does not crash to order); a PID that is not running is refused.
        board.fault("hang")
        board.fault("pointer", pid=1)
        try:
            board.fault("abort", pid=999)
            refused = False
        except dbg.DebugError as e:
            refused = "bad arguments" in str(e)
        assert refused, "a fault for a PID that is not running was accepted"
        board.close()
        out = subprocess.run([sys.executable, dbg.__file__, "--port", port, "fault", "hang", "--pid", "1"],
                             capture_output=True, text=True, timeout=20)
        assert "fault sent: hang as PID 1" in out.stdout, out
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
