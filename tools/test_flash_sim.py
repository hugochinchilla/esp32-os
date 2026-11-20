#!/usr/bin/env python3
"""Test of tools/flash.py: the copy to C: against the simulated board, and
the check of what a build may write to the flash.

  python3 tools/test_flash_sim.py build/host/sim
"""
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbg  # noqa: E402
import flash  # noqa: E402


def want(to, data, **more):
    return flash.Wanted(to, "test", lambda: data, **more)


def plans():
    """flash_plan: the four places of this project's layout, nothing else."""
    folder = tempfile.mkdtemp()

    def build(files):
        with open(os.path.join(folder, "flasher_args.json"), "w") as f:
            json.dump({"write_flash_args": ["--flash_mode", "dio"], "flash_files": files}, f)

    good = {"0x0": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
            "0xf0000": "ota_data_initial.bin", "0x100000": "esp32-os.bin"}
    build(good)
    plan, settings = flash.flash_plan(folder)
    assert [o for o, _ in plan] == [0x0, 0x8000, 0xF0000, 0x100000] and settings == ["--flash_mode", "dio"]
    for bad in ({**good, "0x9000": "nvs.bin"}, {**good, "0x700000": "storage.bin"},
                {"0x0": "b.bin", "0x8000": "t.bin", "0x10000": "esp32-os.bin"}):  # the layout before the BIOS
        build(bad)
        try:
            flash.flash_plan(folder)
            raise AssertionError("accepted %r" % bad)
        except flash.FlashError:
            pass


def main():
    plans()
    lines = []
    sim = subprocess.Popen([sys.argv[1], tempfile.mkdtemp()], stdout=subprocess.PIPE, text=True)
    try:
        board = dbg.Board(sim.stdout.readline().strip(), timeout=5.0)
        dbg.wait_for_desktop(board)
        # Ready is "C: answers", not "a window is open": the wait does not
        # ask for the windows at all (board batch 26: with none open, the
        # copy never started).
        flash.wait_for_drive(board, timeout=10, settle_ms=0)
        # The simulated board has the system's programs linked into it, each
        # with its file in C:\\Programs: they are not the tool's.
        programs = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "programs")
        linked = {name + ".app" for name in os.listdir(programs)} - {"hello.app"}

        class Mute:  # a board that is still restarting, then answers
            def __init__(self):
                self.asked = 0

            def ls(self, folder):
                self.asked += 1
                if self.asked < 3:
                    raise dbg.DebugError("no answer from the board")
                return []

            def stats(self):
                return {"uptime_ms": 5000}

            def windows(self):
                raise AssertionError("asked for the windows")

        mute = Mute()
        flash.wait_for_drive(mute, timeout=10)
        assert mute.asked == 3
        try:
            mute.asked = -1000
            flash.wait_for_drive(mute, timeout=1)
            raise AssertionError("waited for ever")
        except flash.FlashError:
            pass
        hello, card, big = b"\x7fELF hello" * 40, b"PNG" * 700, bytes(range(256)) * 1200  # 307,200 bytes
        first = [want("C:\\Programs\\hello.app", hello), want("C:\\pictures\\card.png", card),
                 want("C:\\Programs\\DATA.BIN", big)]
        notes = board.get("C:\\Documents\\notes.txt")

        # A board this tool has not seen: folders made, everything sent, the list written.
        assert flash.sync(board, first, say=lines.append) == (3, 0, len(hello) + len(card) + len(big))
        assert board.get("C:\\Programs\\hello.app") == hello and board.get("C:\\Programs\\DATA.BIN") == big
        listed = flash.parse_manifest(board.get(flash.MANIFEST).decode())
        assert listed["C:\\Programs\\hello.app"] == (flash.stamp(hello), len(hello)) and len(listed) == 3
        # Again: nothing sent, and nothing read from the computer for a file
        # whose mark and size are known beforehand (the game data).
        def never():
            raise AssertionError("fetched a file the board has")
        known = flash.Wanted("C:\\Programs\\DATA.BIN", "test", never, mark=flash.stamp(big), size=len(big))
        assert flash.sync(board, first[:2] + [known], say=lines.append) == (0, 0, 0)
        # The same with the board's list gone (the board, 2026-11-20: the
        # game data was sent again at every run, six minutes each): a file
        # of the known name and size is it, and goes back on the list.
        board.rm(flash.MANIFEST)
        assert flash.sync(board, first[:2] + [known], say=lines.append) == (2, 0, len(hello) + len(card))
        assert flash.parse_manifest(board.get(flash.MANIFEST).decode())["C:\\Programs\\DATA.BIN"] == \
            (flash.stamp(big), len(big))
        assert "not in the board's list" in lines[-1]
        # One changed (same size), one deleted on the board, one of another
        # size on the board: those three are sent, each once.
        hello2 = hello.replace(b"hello", b"HELLO")
        board.rm("C:\\pictures\\card.png")
        board.put("C:\\Programs\\DATA.BIN", b"cut short")
        second = [want("C:\\Programs\\hello.app", hello2), first[1], known]
        known._data = lambda: big
        assert flash.sync(board, second, say=lines.append)[:2] == (3, 0)
        assert board.get("C:\\Programs\\hello.app") == hello2 and board.get("C:\\pictures\\card.png") == card
        assert board.get("C:\\Programs\\DATA.BIN") == big
        assert flash.sync(board, second, say=lines.append) == (0, 0, 0)
        # A file that left the set is deleted; one named in `keep` stays, on
        # the board and in the list. What the tool never copied is not touched.
        board.put("C:\\Programs\\mine.app", b"the user's own")
        assert flash.sync(board, second[:1], keep=("C:\\Programs\\DATA.BIN",), say=lines.append) == (0, 1, 0)
        names = {n for n, _, _ in board.ls("C:\\Programs")}
        assert names - linked == {"hello.app", "DATA.BIN", "mine.app"}, names
        assert "card.png" not in {n for n, _, _ in board.ls("C:\\pictures")}
        assert set(flash.parse_manifest(board.get(flash.MANIFEST).decode())) == \
            {"C:\\Programs\\hello.app", "C:\\Programs\\DATA.BIN"}
        assert board.get("C:\\Documents\\notes.txt") == notes
        # Without `keep` it goes too.
        assert flash.sync(board, second[:1], say=lines.append) == (0, 1, 0)
        assert {n for n, _, _ in board.ls("C:\\Programs")} - linked == {"hello.app", "mine.app"}
        board.close()
    finally:
        sim.kill()
    # The set from a build folder: programs, the six small pictures, the
    # seven photographs at two sizes, and the game data only when asked for
    # or when the Doom program is there.
    build = tempfile.mkdtemp()
    with open(os.path.join(build, "hello.app"), "wb") as f:
        f.write(hello)
    doom = os.path.isdir(flash.WAD["needs"])
    paths = [w.to for w in flash.wanted_files(build)]
    assert paths[0] == "C:\\Programs\\hello.app" and "C:\\pictures\\earthrise.jpg" in paths
    assert "C:\\pictures\\mir over clouds 800x600.jpg" in paths and "C:\\pictures\\mascot blinks.gif" in paths
    assert len(paths) == 1 + 6 + 14 + doom and (flash.WAD["to"] in paths) == doom
    wad = flash.wanted_files(build, "yes")[-1]
    assert (wad.to, wad.size, wad.mark) == (flash.WAD["to"], 4196020, flash.WAD["sha256"][:16])
    assert flash.WAD["to"] not in [w.to for w in flash.wanted_files(build, "no", "some.wad")]
    # A file that is not the game data is refused.
    with open(os.path.join(build, "wrong.wad"), "wb") as f:
        f.write(b"IWAD")
    try:
        flash.fetch_wad(os.path.join(build, "wrong.wad"))
        raise AssertionError("took a wrong WAD")
    except flash.FlashError:
        pass
    print("flash tool: ok")


if __name__ == "__main__":
    main()
