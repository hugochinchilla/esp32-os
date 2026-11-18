#!/usr/bin/env python3
"""The serial console and the debug tool on one wire, against the simulated
board: the shell answers what is typed while tools/dbg.py goes on working.

  python3 tools/test_serial_console.py build/host/sim
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbg  # noqa: E402


def main():
    env = dict(os.environ, CHINCHILLA_STARTUP="desktop:once;inetd:once;telnetd:once;console:once",
               ASAN_OPTIONS="detect_leaks=0")
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        telnet_port = probe.getsockname()[1]
    sim = subprocess.Popen([sys.argv[1], tempfile.mkdtemp(), "0", "0", str(telnet_port)], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, text=True, env=env)
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

        # One service stopped and started by hand (the `service` command):
        # telnetd stopped, its port refuses and its row goes while inetd
        # stays; started, it answers again with a new row.
        def answers():
            try:
                socket.create_connection(("127.0.0.1", telnet_port), timeout=2).close()
                return True
            except OSError:
                return False

        def wait_for(what, why):
            end = time.monotonic() + 5
            while not what():
                assert time.monotonic() < end, why
                time.sleep(0.05)

        def processes():
            board.console_send(b"ps\r")
            return until(b"C:\\> ").decode()

        assert answers()
        board.console_send(b"service\r")
        listing = until(b"C:\\> ").decode()
        assert "telnetd    running" in listing and "inetd      running" in listing, listing
        assert "taskbar    running, comes back if it ends" in listing and "vncd       stopped" in listing, listing
        board.console_send(b"service telnet stop\r")
        assert b"Stopped until the next start" in until(b"C:\\> ")
        wait_for(lambda: not answers(), "the telnet port still answers after service telnet stop")
        wait_for(lambda: "telnetd" not in processes(), "the telnetd row is still there")
        assert "inetd" in processes()
        board.console_send(b"service telnetd\r")
        assert b"telnetd: stopped by hand, until the next start" in until(b"C:\\> ")
        board.console_send(b"service telnetd stop\r")
        assert b"it is not running" in until(b"C:\\> ")
        board.console_send(b"service telnetd start\r")
        reply = until(b"C:\\> ")
        assert b"service:" not in reply, reply
        assert answers()
        assert "telnetd" in processes()
        # A session that is connected is hung up on by the stop.
        caller = socket.create_connection(("127.0.0.1", telnet_port), timeout=2)
        caller.settimeout(5)
        got = b""
        while b"C:\\> " not in got:
            got += caller.recv(4096)
        board.console_send(b"service telnetd stop\r")
        until(b"C:\\> ")
        while True:
            try:
                if caller.recv(4096) == b"":
                    break
            except ConnectionError:
                break
        caller.close()
        wait_for(lambda: not answers(), "the telnet port still answers")
        # The port closes a moment before the loop says it has stopped:
        # until then a start is refused.
        def started():
            board.console_send(b"service telnetd start\r")
            return b"service:" not in until(b"C:\\> ")

        wait_for(started, "telnetd did not start again")
        assert answers()
        # What cannot be stopped this way, and what is not there.
        board.console_send(b"service inetd stop\r")
        assert b"it cannot be stopped this way" in until(b"C:\\> ")
        board.console_send(b"service nothing start\r")
        assert b"there is no such service" in until(b"C:\\> ")
        board.console_send(b"service telnetd restart\r")
        assert b"it is start, stop or status" in until(b"C:\\> ")
        rows = {}
        for line in processes().splitlines():
            fields = line.split()
            if len(fields) >= 6 and fields[0].isdigit():
                rows[fields[5]] = fields
        assert "telnetd" in rows and "inetd" in rows, rows

        # The network services are a process that can be ended (backlog 5b):
        # kill it and its ports close and its rows go. The console, which is
        # protected, cannot be.
        socket.create_connection(("127.0.0.1", telnet_port), timeout=2).close()
        board.console_send(b"kill %s\r" % rows["console"][0].encode())
        assert b"protected" in until(b"C:\\> ")
        board.console_send(b"kill %s\r" % rows["inetd"][0].encode())
        until(b"C:\\> ")
        end = time.monotonic() + 5
        while True:
            board.console_send(b"ps\r")
            listing = until(b"C:\\> ").decode()
            if "inetd" not in listing and "telnetd" not in listing:
                break
            assert time.monotonic() < end, listing
            time.sleep(0.1)
        assert "console" in listing and "desktop" in listing
        try:
            socket.create_connection(("127.0.0.1", telnet_port), timeout=2).close()
            raise AssertionError("the telnet port still answers")
        except OSError:
            pass
        assert board.ping() < 1.0
        # With the loop gone its services count as stopped, and cannot be
        # started.
        board.console_send(b"service telnetd\r")
        assert b"telnetd: stopped" in until(b"C:\\> ")
        board.console_send(b"service telnetd start\r")
        assert b"it did not start" in until(b"C:\\> ")

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
