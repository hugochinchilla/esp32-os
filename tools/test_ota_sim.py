#!/usr/bin/env python3
"""tools/ota.py against the simulated board: an image sent, checked and
"installed", the restart, and the ways it is refused.

  python3 tools/test_ota_sim.py build/host/sim
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbg  # noqa: E402
import ota  # noqa: E402


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    folder = tempfile.mkdtemp()
    out = os.path.join(folder, "installed.bin")
    port = free_port()
    env = dict(os.environ, CHINCHILLA_UPDATE_OUT=out, ASAN_OPTIONS="detect_leaks=0")
    sim = subprocess.Popen([sys.argv[1], folder, "0", "0", "0", str(port)], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, text=True, env=env)
    try:
        board = dbg.Board(sim.stdout.readline().strip(), timeout=2.0)
        dbg.wait_for_desktop(board)

        image = bytes([0xE9]) + bytes((i * 31 + i // 253) & 0xFF for i in range(1, 700000))

        # Not an image: refused by the board after the check, nothing installed.
        try:
            ota.send("127.0.0.1", b"\x7f" + image[1:], port, restart=False)
            raise AssertionError("an image that is not one was taken")
        except ota.UpdateError as e:
            assert "not an image this machine can start" in str(e), e
        assert not os.path.exists(out)

        # Written without a restart: the board goes on, and still answers.
        steps = []
        took = ota.send("127.0.0.1", image, port, restart=False, progress=lambda sent, total: steps.append(sent))
        assert steps and steps[-1] == len(image) and took < 30
        end = time.monotonic() + 5
        while not os.path.exists(out) and time.monotonic() < end:
            time.sleep(0.05)
        time.sleep(0.2)
        with open(out, "rb") as f:
            assert f.read() == image
        assert board.ping() < 1.0 and sim.poll() is None

        # With a restart: the simulated board ends, as the real one restarts.
        ota.send("127.0.0.1", image[:5000], port, restart=True)
        assert sim.wait(timeout=10) is not None
        with open(out, "rb") as f:
            assert f.read() == image[:5000]

        # Nobody listening any more: the tool says so.
        try:
            ota.send("127.0.0.1", image[:100], port)
            raise AssertionError("sent to nobody")
        except ota.UpdateError as e:
            assert "cannot reach" in str(e), e
    finally:
        sim.kill()
    print("updates over the network: all checks passed")


if __name__ == "__main__":
    main()
