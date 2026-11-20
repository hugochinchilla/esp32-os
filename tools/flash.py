#!/usr/bin/env python3
"""Flashes the board and puts on C: the files that come with the system.

  tools/flash.py                   build, write the firmware, copy the files
  tools/flash.py --no-build        the build that is already in firmware/os/build
  tools/flash.py --files-only      copy the files to the running board, no flashing
  tools/flash.py --no-files        write the firmware only
  tools/flash.py --list            what would be on C:, and where it comes from
  tools/flash.py --wad ~/DOOM1.WAD the game data from this file, not from the network

The firmware does not carry the programs kept in files, the sample pictures
or anything else that lives on C: (backlog item 77). After the firmware is
written and C: answers (a few seconds after the reset), this copies them
over the debug link:

  build/programs/*.app            ->  C:\\Programs
  the sample pictures             ->  C:\\pictures     (tools/gen_pictures.py)
  DOOM1.WAD, if programs/doom is  ->  C:\\Programs     (fetched once, checked)
  there or --wad is given

Only what changed is sent. C:\\system\\installed.txt on the board lists what
was copied (hash, size, path), so a second run sends nothing, and a file
that left the set is deleted from the board. Everything else on C: is left
alone: settings, documents, the Trash.

Flashing writes the bootloader, the partition table, otadata and the OS
(0x0, 0x8000, 0xf0000, 0x100000) and refuses any other address. It never
erases the flash, and never touches nvs (the saved Wi-Fi networks), the
BIOS (tools/flash-bios.sh writes that) or C:.

The port comes from --port, else the DBG_PORT variable, else /dev/ttyACM0.
Building and flashing need the ESP-IDF environment (get_idf); --files-only
needs pyserial alone.
"""
import argparse
import glob
import hashlib
import json
import os
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import dbg  # noqa: E402

FIRMWARE = os.path.join(REPO, "firmware", "os")
MANIFEST = "C:\\system\\installed.txt"
# What a cable flash may write: the bootloader, the partition table, otadata
# and the first OS slot (firmware/os/partitions.csv). Not nvs (0x9000), not
# phy_init (0xf000), not the BIOS (0x10000), not C: (0x700000).
ALLOWED_OFFSETS = {0x0: "bootloader", 0x8000: "partition table", 0xF0000: "otadata", 0x100000: "the OS"}

# The game data of Doom's shareware release, version 1.9. id Software lets
# it be passed on unchanged but it is not freely licensed, so it is not in
# the repository: it is fetched to the computer, checked and copied to the
# board. Wanted when the Doom program is there to use it.
WAD = {
    "to": "C:\\Programs\\DOOM1.WAD",
    "size": 4196020,
    "sha256": "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771",
    "needs": os.path.join(REPO, "programs", "doom"),
    "urls": (
        "https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad",
        "https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/doom1.wad",
    ),
}


class FlashError(Exception):
    pass


def stamp(data):
    """How a file is told from another version of itself: 16 hex digits."""
    return hashlib.sha256(data).hexdigest()[:16]


class Wanted:
    """A file that belongs on C:. `data` is a function, called only when the
    file has to be sent; `mark` and `size` are known without it where they
    can be (the game data), so a board that has the file costs no download."""

    def __init__(self, to, source, data, mark=None, size=None):
        self.to, self.source, self._data, self._bytes = to, source, data, None
        self._mark, self._size = mark, size
        # One file only can have this name and this size (the game data,
        # checked by its sum when it was fetched).
        self.fixed = mark is not None and size is not None

    def data(self):
        if self._bytes is None:
            self._bytes = self._data()
        return self._bytes

    @property
    def mark(self):
        return self._mark or stamp(self.data())

    @property
    def size(self):
        return self._size if self._size is not None else len(self.data())


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def fetch_wad(local=None, cache=None, say=print):
    """The game data, from `local`, else from the copy fetched before, else
    from the network. Checked every time."""
    def checked(data, where):
        if len(data) != WAD["size"] or hashlib.sha256(data).hexdigest() != WAD["sha256"]:
            raise FlashError("%s is not DOOM1.WAD of the shareware release 1.9 (%d bytes, sha256 %s...)"
                             % (where, WAD["size"], WAD["sha256"][:16]))
        return data

    if local:
        return checked(read_file(os.path.expanduser(local)), local)
    cache = cache or os.path.join(REPO, "build", "downloads", "DOOM1.WAD")
    if os.path.exists(cache):
        try:
            return checked(read_file(cache), cache)
        except FlashError:
            os.remove(cache)  # half a download: fetched again
    problems = []
    for url in WAD["urls"]:
        say("fetching DOOM1.WAD from %s" % url)
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "curl/8"}),
                                        timeout=60) as answer:
                data = checked(answer.read(), url)
        except (OSError, FlashError) as e:
            problems.append("%s: %s" % (url, e))
            continue
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        with open(cache, "wb") as f:
            f.write(data)
        return data
    raise FlashError("DOOM1.WAD could not be fetched; give it with --wad FILE.\n  " + "\n  ".join(problems))


def wanted_files(programs_dir, wad="auto", wad_path=None):
    """What belongs on C:. `wad`: "auto" (when the Doom program is in the
    repository or a file was given), "yes" or "no"."""
    out = []
    for path in sorted(glob.glob(os.path.join(programs_dir, "*.app"))):
        out.append(Wanted("C:\\Programs\\" + os.path.basename(path), os.path.relpath(path, REPO),
                          lambda path=path: read_file(path)))
    import gen_pictures  # the pictures are drawn here, and the photographs are NASA's (third_party/samples)
    pictures = {}

    def picture(name):
        if not pictures:
            pictures.update(gen_pictures.samples())
        return pictures[name]

    for name in gen_pictures.sample_names():
        out.append(Wanted("C:\\pictures\\" + name, "tools/gen_pictures.py", lambda name=name: picture(name)))
    if wad == "yes" or (wad == "auto" and (wad_path or os.path.isdir(WAD["needs"]))):
        out.append(Wanted(WAD["to"], wad_path or "fetched", lambda: fetch_wad(wad_path),
                          mark=WAD["sha256"][:16], size=WAD["size"]))
    return out


def parse_manifest(text):
    """{path: (mark, size)} from the board's list. Lines that do not read
    are dropped: their files are sent again."""
    out = {}
    for line in text.splitlines():
        parts = line.strip().split(" ", 2)
        if len(parts) == 3 and parts[1].isdigit():
            out[parts[2]] = (parts[0], int(parts[1]))
    return out


def format_manifest(entries):
    return "".join("%s %d %s\r\n" % (mark, size, path) for path, (mark, size) in sorted(entries.items())).encode()


def folder_of(path):
    return path.rsplit("\\", 1)[0]


def sync(board, wanted, keep=(), say=print):
    """Makes the board's copies of `wanted` match, over the debug link.
    Files this tool copied before and that are no longer wanted are deleted,
    except the paths in `keep`. Returns (sent, deleted, bytes sent)."""
    try:
        listed = parse_manifest(board.get(MANIFEST).decode("utf-8", "replace"))
    except dbg.DebugError:
        listed = {}  # a new C:, or one this tool has not seen
    # What is there now, by folder: a file deleted on the board, or one of
    # another size, is sent again whatever the list says.
    there = {}
    for folder in sorted({folder_of(w.to) for w in wanted} | {folder_of(MANIFEST)}):
        try:
            for name, is_folder, size in board.ls(folder):
                if not is_folder:
                    there[(folder + "\\" + name).lower()] = size
        except dbg.DebugError:
            board.mkdir(folder)

    def record():
        board.put(MANIFEST, format_manifest(listed))

    sent = deleted = total = 0
    noted = False
    paths = {w.to for w in wanted}
    for path in sorted(set(listed) - paths - set(keep)):
        try:
            board.rm(path)
            say("deleted  %s" % path)
            deleted += 1
        except dbg.DebugError:
            pass  # gone already
        del listed[path]
    for w in wanted:
        on_board = there.get(w.to.lower())
        if w.fixed and on_board == w.size:
            # The game data: 4 MB and six minutes over this wire. A file of
            # that name and exactly that size is it, whatever the list says
            # (a list lost, or a copy that this tool was stopped in the
            # middle of after the board had the whole file).
            if listed.get(w.to) != (w.mark, w.size):
                listed[w.to] = (w.mark, w.size)
                noted = True
            continue
        if listed.get(w.to) == (w.mark, w.size) and on_board == w.size:
            continue
        # Why it is sent, so that a file sent every time can be explained.
        if on_board is None:
            why = "not on the board"
        elif on_board != w.size:
            why = "%d bytes on the board" % on_board
        elif w.to not in listed:
            why = "not in the board's list"
        else:
            why = "changed"
        data = w.data()
        start = time.monotonic()
        last = [start]

        def progress(done, of, last=last, w=w):
            if of > 256 * 1024 and time.monotonic() - last[0] > 5:
                last[0] = time.monotonic()
                say("         %s: %d %%" % (w.to, 100 * done // of))

        # Out of the list while it is being written: a copy cut short is
        # sent again.
        if listed.pop(w.to, None) is not None and len(data) > 256 * 1024:
            record()
        board.put(w.to, data, progress=progress)
        listed[w.to] = (stamp(data), len(data))
        say("copied   %s (%d bytes, %.1f s; %s)" % (w.to, len(data), time.monotonic() - start, why))
        sent += 1
        total += len(data)
        if len(data) > 256 * 1024:
            record()  # a long copy is not lost to what fails after it
    if sent or deleted or noted or MANIFEST.lower() not in there:
        record()
    return sent, deleted, total


def wait_for_drive(board, timeout=90, settle_ms=3000, say=print):
    """Waits until the board takes files: C: answers over the debug link and
    the system has been up for `settle_ms`, so the copy does not fall on the
    desktop's own start. Not dbg.wait_for_desktop: that waits for a window,
    and a desktop with none open is as ready as any (board batch 26)."""
    end = time.monotonic() + timeout
    problem = "no answer"
    while time.monotonic() < end:
        try:
            board.ls("C:\\")
            up = board.stats()["uptime_ms"]
            if up >= settle_ms:
                return
            time.sleep((settle_ms - up) / 1000.0)
        except dbg.DebugError as e:
            problem = str(e)
            time.sleep(0.5)
    raise FlashError("the board did not take files after %d s (%s)" % (timeout, problem))


def flash_plan(build_dir):
    """[(offset, file)] and esptool's settings from the build, refused if it
    would write anywhere but the four allowed places."""
    path = os.path.join(build_dir, "flasher_args.json")
    if not os.path.exists(path):
        raise FlashError("%s is not there: build first" % path)
    with open(path) as f:
        args = json.load(f)
    plan = sorted((int(offset, 0), os.path.join(build_dir, name)) for offset, name in args["flash_files"].items())
    bad = [offset for offset, _ in plan if offset not in ALLOWED_OFFSETS]
    if bad:
        raise FlashError("the build wants to write at %s; only %s are written by this tool"
                         % (", ".join("0x%x" % o for o in bad), ", ".join("0x%x" % o for o in sorted(ALLOWED_OFFSETS))))
    if 0x100000 not in dict(plan):
        raise FlashError("the build has no OS image at 0x100000: not this project's partition table")
    return plan, list(args.get("write_flash_args", []))


def run(command, cwd=None):
    print("+ " + " ".join(command), flush=True)
    if subprocess.call(command, cwd=cwd) != 0:
        raise FlashError("failed: " + " ".join(command))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Flash the board and copy the system's files to C:.")
    ap.add_argument("--port", default=os.environ.get("DBG_PORT", "/dev/ttyACM0"))
    ap.add_argument("--build-dir", default=os.path.join(FIRMWARE, "build"), help="the firmware's build folder")
    ap.add_argument("--no-build", action="store_true", help="do not run idf.py build first")
    ap.add_argument("--files-only", action="store_true", help="copy the files; do not build or flash")
    ap.add_argument("--no-files", action="store_true", help="flash; do not copy the files")
    ap.add_argument("--list", action="store_true", help="show what belongs on C: and stop")
    ap.add_argument("--wad", metavar="FILE", help="DOOM1.WAD (shareware 1.9) from this file")
    ap.add_argument("--no-wad", action="store_true", help="do not copy DOOM1.WAD (one on the board stays)")
    ap.add_argument("--baud", type=int, default=460800, help="esptool's speed")
    args = ap.parse_args(argv)
    build_dir = os.path.abspath(args.build_dir)
    try:
        if not args.files_only and not args.list:
            if not args.no_build:
                run(["idf.py", "-B", build_dir, "build"], cwd=FIRMWARE)
            plan, settings = flash_plan(build_dir)
            for offset, name in plan:
                print("0x%06x  %-15s %s" % (offset, ALLOWED_OFFSETS[offset], os.path.relpath(name, REPO)))
            command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", args.port, "-b", str(args.baud),
                       "--before", "default_reset", "--after", "hard_reset", "write_flash", *settings]
            for offset, name in plan:
                command += ["0x%x" % offset, name]
            run(command)
        if args.no_files:
            return 0
        wanted = wanted_files(os.path.join(build_dir, "programs"), "no" if args.no_wad else "auto", args.wad)
        if not any(w.to.lower().endswith(".app") for w in wanted):
            raise FlashError("no programs in %s: build first (idf.py build, or tools/build_programs.sh with "
                             "PROGRAMS_OUT set to that folder)" % os.path.join(build_dir, "programs"))
        if args.list:
            for w in wanted:
                print("%-34s %9d  %s" % (w.to, w.size, w.source))
            return 0
        board = dbg.Board(args.port, timeout=20.0)
        try:
            board.timeout = 2.0  # short while it starts; long for the writes

            wait_for_drive(board)
            board.timeout = 20.0
            start = time.monotonic()
            # A WAD on the board stays when this run was told to leave it.
            sent, deleted, total = sync(board, wanted, keep=(WAD["to"],))
            print("C: is up to date: %d file(s) copied (%d bytes), %d deleted, %.1f s"
                  % (sent, total, deleted, time.monotonic() - start))
        finally:
            board.close()
        return 0
    except (FlashError, dbg.DebugError) as e:
        print("flash: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
