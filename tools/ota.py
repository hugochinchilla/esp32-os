#!/usr/bin/env python3
"""Sends a new version of the system to the board over the network.

  tools/ota.py chinchilla.local                      the build in firmware/os/build
  tools/ota.py 192.168.1.50 some/esp32-os.bin
  tools/ota.py chinchilla.local --no-restart         written now, used from the next start

The board must have updates switched on: `[update] enabled = true` in
C:\\.config\\network.ini (off unless that line is there). The image goes to
the app slot that is not running; the board checks it, and with a restart
starts it. If the new version does not come up, the next reset goes back to
the old one. What is said on the wire is in core/include/core/net/update.hpp.
"""
import argparse
import os
import socket
import sys
import time
import zlib

DEFAULT_IMAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "os", "build", "esp32-os.bin")
PORT = 3232


class UpdateError(Exception):
    pass


def read_line(sock, seconds):
    sock.settimeout(seconds)
    line = b""
    while not line.endswith(b"\n"):
        try:
            byte = sock.recv(1)
        except socket.timeout:
            raise UpdateError("the board did not answer")
        if not byte:
            raise UpdateError("the board hung up" + (": " + line.decode("utf-8", "replace") if line else ""))
        line += byte
    return line.decode("utf-8", "replace").strip()


def send(address, image, port=PORT, restart=True, progress=None):
    """Sends `image` (bytes). Returns the seconds it took; raises UpdateError."""
    start = time.monotonic()
    try:
        sock = socket.create_connection((address, port), timeout=10)
    except OSError as e:
        raise UpdateError("cannot reach %s port %d (%s). Are updates switched on in network.ini?" % (address, port, e))
    with sock:
        first = "UPDATE %d %08x%s\n" % (len(image), zlib.crc32(image) & 0xFFFFFFFF, " restart" if restart else "")
        sock.sendall(first.encode())
        answer = read_line(sock, 10)
        if answer != "OK":
            raise UpdateError("the board says: " + answer)
        sock.settimeout(30)
        sent = 0
        while sent < len(image):
            piece = image[sent:sent + 4096]
            try:
                sock.sendall(piece)
            except OSError as e:
                # The board hangs up when it gives up; it said why first.
                try:
                    raise UpdateError("the board says: " + read_line(sock, 2))
                except OSError:
                    raise UpdateError("the connection broke at %d of %d bytes (%s)" % (sent, len(image), e))
            sent += len(piece)
            if progress:
                progress(sent, len(image))
        answer = read_line(sock, 60)
        if answer != "DONE":
            raise UpdateError("the board says: " + answer)
    return time.monotonic() - start


def main(argv=None):
    parser = argparse.ArgumentParser(description="Send a new version of the system to the board over the network.")
    parser.add_argument("address", help="the board's name or address (chinchilla.local)")
    parser.add_argument("image", nargs="?", default=DEFAULT_IMAGE, help="the image to send (default: the last build)")
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--no-restart", action="store_true", help="write it and leave the board running")
    args = parser.parse_args(argv)
    try:
        with open(args.image, "rb") as f:
            image = f.read()
    except OSError as e:
        sys.exit("cannot read the image: %s" % e)
    if not image or image[0] != 0xE9:
        sys.exit("%s is not an ESP32 app image" % args.image)

    def progress(sent, total):
        sys.stderr.write("\r%3d %%  %d of %d KB" % (100 * sent // total, sent // 1024, total // 1024))
        sys.stderr.flush()

    try:
        took = send(args.address, image, args.port, not args.no_restart, progress)
    except UpdateError as e:
        sys.stderr.write("\n")
        sys.exit("update failed: %s" % e)
    sys.stderr.write("\n")
    print("%d bytes in %.1f s (%d KB/s). %s" % (len(image), took, len(image) / 1024 / took,
                                               "The board keeps running the old version until its next start."
                                               if args.no_restart else "The board is restarting into it."))


if __name__ == "__main__":
    main()
