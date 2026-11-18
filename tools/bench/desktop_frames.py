#!/usr/bin/env python3
"""Does the desktop keep up with the monitor under load?

Drives the board (or the simulator) through the debug link with the mouse and
the keyboard, and after each part reads what the desktop counted against one
video frame (core/sys/frames.hpp; `tools/dbg.py frames` shows the same
figures). Nothing is filmed and no screenshot is taken while a part is
measured. Backlog item 63, docs/journal/2026-11-18-frame-budget.md.

  python3 tools/bench/desktop_frames.py                    # the board on /dev/ttyACM0 (or $DBG_PORT)
  python3 tools/bench/desktop_frames.py --sim build/host/sim
  python3 tools/bench/desktop_frames.py --json frames.json # the figures, to keep

Start from a board that has just started (`tools/dbg.py reboot --fast`), so
the windows are the same every time. It opens the Editor, the File manager,
a Terminal, Culebra and Chess (set to Expert, which thinks up to ten
seconds a move), then measures, one after the other:

  idle             nothing is touched
  pointer          the pointer goes round, no button held
  drag + chess     the Editor is dragged round (the outline) while Chess thinks
  drops + chess    the Editor is dropped in a new place ten times while Chess thinks
  drag + culebra   Culebra's own window is dragged while the snake is steered
  culebra          the snake is steered, the pointer goes round over the windows
  to front + chess each window in turn is clicked to the front while Chess thinks
  typing + chess   text is typed into the Editor while Chess thinks
  maximise         the Editor is maximised and put back three times

Three things are reported for each part: frames over budget (a piece of the
desktop's work that took longer than one video frame), timers that fired a
frame or more late (a game's step shown late), and new missed-refill lines in
the system log (the video signal itself, `tools/dbg.py misses`). The rate the
monitor is really fed at is measured too, from the board's count of frames
shown. Exits 1 if any part had a frame over budget, a late timer or a missed
refill of its own.
"""
import argparse
import json
import math
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import dbg  # noqa: E402

MOUSE_HZ = 125  # what a USB mouse reports at
# What Start > Run is given, the file to give it instead where the program
# is not built in (on the board the games are files in C:\Programs, backlog
# item 27), and how the window's title starts or ends.
PROGRAMS = (("Editor", None, "Editor"), ("File manager", None, "My Board"), ("Terminal", None, "Terminal"),
            ("Culebra", "C:\\Programs\\culebra.app", "Culebra"), ("Chess", "C:\\Programs\\chess.app", "Chess"))
# Chess's board inside its window: where square a8 starts, and a square's side.
CHESS_BOARD = (12, 74, 40)
# Quiet moves for White that stay legal whatever Black answers in so few moves.
CHESS_MOVES = ("a2a3", "h2h3", "b2b3", "g2g3", "c2c3", "a3a4", "h3h4")


class Bench:
    def __init__(self, board, seconds):
        self.board = board
        self.seconds = seconds
        self.rows = []
        self.chess_move = 0
        info = board.info()
        self.width, self.height = info["width"], info["height"]

    # ---- Finding and moving windows ----

    def window(self, title):
        for w in self.board.windows():
            if w["title"].startswith(title) or w["title"].endswith(title):
                return w
        raise dbg.DebugError("no window called %s" % title)

    def grab_point(self, title):
        """A point on the window's title bar that no window above it covers."""
        windows = self.board.windows()
        at = next(i for i, w in enumerate(windows) if w["title"].startswith(title) or w["title"].endswith(title))
        w = windows[at]
        y = w["y"] + 12
        for x in range(w["x"] + 30, w["x"] + w["w"] - 70, 4):
            if not 0 <= x < self.width or not 0 <= y < self.height - 30:
                continue
            if not any(o["state"] != "minimized" and o["x"] <= x < o["x"] + o["w"] and o["y"] <= y < o["y"] + o["h"]
                       for o in windows[at + 1:]):
                return x, y
        return None

    def focus(self, title):
        """Brings the window to the front: a click on its title bar where it
        shows, else Alt+Tab until it has the keys."""
        for _ in range(12):
            if self.window(title)["focused"]:
                return
            point = self.grab_point(title)
            if point:
                self.board.mouse(point[0], point[1], absolute=True)
                self.board.mouse(point[0], point[1], buttons=1, absolute=True)
                self.board.mouse(point[0], point[1], buttons=0, absolute=True)
            else:
                self.board.key("alt", True)
                self.board.tap("tab")
                self.board.key("alt", False)
            time.sleep(0.1)
        raise dbg.DebugError("could not bring %s to the front" % title)

    def run(self, name, path, title):
        """Start > Run, the program's name, Enter. Where Run does not know
        the name (it says so in a box), the program's file instead."""
        # Twice round: just after a start the Start menu may not be there yet.
        for what in (name, path, name, path):
            if what is None:
                continue
            self.board.tap("gui")
            time.sleep(0.2)
            self.board.tap("r")
            time.sleep(0.4)
            self.board.text(what)
            self.board.tap("enter")
            end = time.monotonic() + 10
            while time.monotonic() < end:
                time.sleep(0.3)
                windows = self.board.windows()
                asking = [w for w in windows if w["title"] == "Run"]
                if any(title in w["title"] for w in windows) and not asking:
                    return
                if len(asking) > 1:
                    break  # the dialog and its box: "Cannot find"
            # Put away the box, then the dialog, and try the next.
            for _ in range(3):
                if not any(w["title"] == "Run" for w in self.board.windows()):
                    break
                self.board.tap("esc")
                time.sleep(0.3)
        raise dbg.DebugError("%s did not open" % name)

    def paced(self, seconds, step):
        """Calls step(i, t) MOUSE_HZ times a second for `seconds`."""
        start = time.monotonic()
        i = 0
        while True:
            t = time.monotonic() - start
            if t >= seconds:
                return i
            step(i, t)
            i += 1
            ahead = start + i / MOUSE_HZ - time.monotonic()
            if ahead > 0:
                time.sleep(ahead)

    def circle(self, cx, cy, rx, ry, t, turn_s=2.0):
        a = 2 * math.pi * t / turn_s
        x = int(cx + rx * math.sin(a))
        y = int(cy + ry * (1 - math.cos(a)) / 2)
        return max(0, min(self.width - 1, x)), max(0, min(self.height - 40, y))

    def drag_round(self, title, seconds, also=None):
        """Holds the window by its title bar and takes it round for a while,
        then lets go where it was picked up."""
        self.focus(title)
        x, y = self.grab_point(title)
        self.board.mouse(x, y, absolute=True)
        self.board.mouse(x, y, buttons=1, absolute=True)

        def step(i, t):
            px, py = self.circle(x, y, 90, 120, t)
            self.board.mouse(px, py, buttons=1, absolute=True)
            if also:
                also(i, t)
        self.paced(seconds, step)
        self.board.mouse(x, y, buttons=1, absolute=True)
        self.board.mouse(x, y, buttons=0, absolute=True)

    # ---- The games ----

    def chess_think(self):
        """Plays White's next quiet move: the engine then thinks, in its job."""
        move = CHESS_MOVES[self.chess_move % len(CHESS_MOVES)]
        self.chess_move += 1
        self.focus("Chess")
        w = self.window("Chess")
        ox, oy, side = CHESS_BOARD
        for square in (move[:2], move[2:]):
            x = w["x"] + ox + side * (ord(square[0]) - ord("a")) + side // 2
            y = w["y"] + oy + side * (8 - int(square[1])) + side // 2
            self.board.mouse(x, y, absolute=True)
            self.board.mouse(x, y, buttons=1, absolute=True)
            self.board.mouse(x, y, buttons=0, absolute=True)
            time.sleep(0.15)

    def chess_blacks_half(self):
        """The pixels of the top half of Chess's board, ranks 5 to 8: our
        quiet moves change nothing there, Black's answer does (the piece
        that left, or the mark on the move just made)."""
        w = self.window("Chess")
        ox, oy, side = CHESS_BOARD
        width, _, pixels = self.board.screenshot(pointer=False)
        return b"".join(pixels[(w["y"] + oy + row) * width + w["x"] + ox:
                               (w["y"] + oy + row) * width + w["x"] + ox + 8 * side]
                        for row in range(4 * side))

    def chess_check(self):
        """Before anything is measured: a move of ours is answered, so the
        engine does think when the parts below ask it to."""
        self.focus("Chess")
        before = self.chess_blacks_half()
        self.chess_think()
        end = time.monotonic() + 30
        while time.monotonic() < end:
            time.sleep(1.0)
            if self.chess_blacks_half() != before:
                return
        raise dbg.DebugError("Chess did not answer the first move in 30 s: has its window changed?")

    def steer(self, i, t):
        """Keeps Culebra's snake going round in a small loop."""
        every = MOUSE_HZ // 2  # a turn every half second
        if i % every == 0:
            self.board.tap(("right", "down", "left", "up")[i // every % 4], hold=0.0)

    # ---- Measuring ----

    def misses(self):
        lines = self.board.missed_refills()
        return len(lines), sum(1 for _, own in lines if own > 0)

    def measure(self, name, action, expected_slow=False):
        self.board.frames(reset=True)
        lines_before, own_before = self.misses()
        shown = self.board.stats()["frames_shown"]
        start = time.monotonic()
        action()
        elapsed = time.monotonic() - start
        shown = self.board.stats()["frames_shown"] - shown
        f = self.board.frames()
        lines_after, own_after = self.misses()
        row = {"part": name, "seconds": round(elapsed, 2), "frames_shown": shown,
               "shown_per_second": round(shown / elapsed, 2) if elapsed > 0 else 0,
               "missed_refill_lines": own_after - own_before,
               "missed_refill_lines_debug_link": (lines_after - lines_before) - (own_after - own_before),
               "busy_percent": round(f["busy_us"] / 1e4 / elapsed, 1) if elapsed > 0 else 0,
               "expected_slow": expected_slow}
        row.update(f)
        self.rows.append(row)
        print("%-17s %6.1f s  frames drawn %5d  over budget %3d  late timers %3d  longest %6.2f ms  "
              "desktop busy %5.1f %%  missed-refill lines %d" %
              (name, elapsed, f["painted"], f["over"], f["late"], f["worst_us"] / 1000, row["busy_percent"],
               row["missed_refill_lines"]), flush=True)
        for s in f["slow"]:
            top = "; longest %s %.2f ms, PID %d" % (s["top"], s["top_us"] / 1000, s["top_pid"]) if s["top"] else ""
            late = "; timer %.2f ms late" % (s["late_us"] / 1000) if s["late_us"] else ""
            print("    a %s took %.2f ms: painting %.2f ms for %d pixels, timers %.2f ms%s%s" %
                  (s["work"], s["total_us"] / 1000, s["paint_us"] / 1000, s["pixels"], s["timers_us"] / 1000,
                   top, late), flush=True)

    # ---- The parts ----

    def setup(self):
        board = self.board
        board.animations(True)
        # Chess: Expert thinks up to ten seconds a move, and we play White.
        try:
            board.mkdir("C:\\.config")
        except dbg.DebugError:
            pass  # it is there
        board.put("C:\\.config\\chess.ini", b"[game]\nlevel = 5\nside = white\n")
        open_now = [w["title"] for w in board.windows()]
        for name, path, title in PROGRAMS:
            if not any(title in t for t in open_now):
                self.run(name, path, title)
        if any(w["title"] == "New game" for w in board.windows()):
            board.tap("enter")  # Chess's dialog before a game: as the file says
            time.sleep(0.5)
        self.chess_check()
        print("windows: %s" % ", ".join("%s (PID %s)" % (w["title"], w.get("pid", "?")) for w in board.windows()),
              flush=True)

    def part_idle(self):
        time.sleep(self.seconds)

    def part_pointer(self):
        self.paced(self.seconds, lambda i, t: self.board.mouse(
            *self.circle(self.width // 2, 60, 250, 300, t), absolute=True))

    def part_drag_chess(self):
        self.chess_think()
        self.drag_round("Editor", self.seconds)

    def part_drops_chess(self):
        self.chess_think()
        self.focus("Editor")
        for i in range(10):
            x, y = self.grab_point("Editor")
            dx, dy = (60, 40) if i % 2 == 0 else (-60, -40)
            self.board.mouse(x, y, absolute=True)
            self.board.mouse(x, y, buttons=1, absolute=True)
            for k in range(1, 11):
                self.board.mouse(x + dx * k // 10, y + dy * k // 10, buttons=1, absolute=True)
                time.sleep(1 / MOUSE_HZ)
            self.board.mouse(x + dx, y + dy, buttons=0, absolute=True)
            time.sleep(0.25)

    def culebra_start(self):
        self.focus("Culebra")
        self.board.tap("f2")  # a new game
        self.board.tap("right")

    def part_drag_culebra(self):
        self.culebra_start()
        self.drag_round("Culebra", self.seconds, also=self.steer)

    def part_culebra(self):
        self.culebra_start()

        def step(i, t):
            self.board.mouse(*self.circle(self.width // 2, 60, 250, 300, t), absolute=True)
            self.steer(i, t)
        self.paced(self.seconds, step)

    def part_to_front_chess(self):
        self.chess_think()
        for _ in range(2):
            for _, _, title in PROGRAMS:
                self.focus(title)
                time.sleep(0.2)

    def part_typing_chess(self):
        self.chess_think()
        self.focus("Editor")
        text = "the quick brown fox jumps over the lazy dog "
        count = int(self.seconds * 20)
        for i in range(count):  # twenty letters a second
            self.board.text(text[i % len(text)])
            time.sleep(0.05)

    def part_maximise(self):
        self.focus("Editor")
        for _ in range(6):  # three times there and back
            x, y = self.grab_point("Editor")
            self.board.doubleclick(x, y)
            time.sleep(0.5)

    def run_all(self):
        self.setup()
        self.measure("idle", self.part_idle)
        self.measure("pointer", self.part_pointer)
        self.measure("drag + chess", self.part_drag_chess)
        self.measure("drops + chess", self.part_drops_chess)
        self.measure("drag + culebra", self.part_drag_culebra)
        self.measure("culebra", self.part_culebra)
        self.measure("to front + chess", self.part_to_front_chess)
        self.measure("typing + chess", self.part_typing_chess)
        self.measure("maximise", self.part_maximise, expected_slow=True)


def wait_for_desktop(board, timeout=30):
    """Waits until the desktop's loop is running: it answers, and a mouse
    event is counted as a piece of its work. (A board may start with no
    window at all, so windows are not what to wait for.)"""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            before = board.frames()["pieces"]
            board.mouse(1, 0)
            board.mouse(-1, 0)
            time.sleep(0.2)
            if board.frames()["pieces"] > before:
                board.windows()
                # The taskbar comes a moment after the desktop, and the
                # programs that are files are put in C:\\Programs two
                # seconds after that.
                time.sleep(5)
                return
        except dbg.DebugError:
            pass
        time.sleep(0.5)
    raise dbg.DebugError("the desktop did not start")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", default=os.environ.get("DBG_PORT", "/dev/ttyACM0"))
    parser.add_argument("--sim", metavar="PROGRAM", help="start this simulator (build/host/sim) and measure it")
    parser.add_argument("--seconds", type=float, default=6.0, help="how long each timed part lasts (default 6)")
    parser.add_argument("--json", metavar="FILE", help="write every figure there")
    args = parser.parse_args()

    sim = None
    port = args.port
    if args.sim:
        sim = subprocess.Popen([args.sim, tempfile.mkdtemp()], stdout=subprocess.PIPE, text=True)
        port = sim.stdout.readline().strip()
    failed = True
    try:
        board = dbg.Board(port, timeout=2.0)
        wait_for_desktop(board)
        bench = Bench(board, args.seconds)
        bench.run_all()
        rows = bench.rows
        budget = rows[0]["budget_us"]
        over = sum(r["over"] for r in rows)
        late = sum(r["late"] for r in rows)
        missed = sum(r["missed_refill_lines"] for r in rows)
        painted = sum(r["painted"] for r in rows)
        seconds = sum(r["seconds"] for r in rows)
        shown = sum(r["frames_shown"] for r in rows)
        print()
        print("the monitor: one frame every %d us (%.2f a second by the pixel clock); %d frames shown in %.1f s, "
              "%.2f a second by the PC's clock" % (budget, 1e6 / budget, shown, seconds, shown / seconds))
        print("the desktop: %d frames drawn, %d over budget, %d late timers; %d missed-refill line(s) of its own"
              % (painted, over, late, missed))
        slow_parts = [r["part"] for r in rows if r["over"] or r["late"] or r["missed_refill_lines"]]
        if slow_parts:
            print("not steady in: %s" % ", ".join(slow_parts))
        else:
            print("steady: every piece of work fitted in one frame")
        if args.json:
            with open(args.json, "w") as f:
                json.dump(rows, f, indent=1)
        failed = bool(slow_parts)
        board.close()
    except dbg.DebugError as error:
        print("error: %s" % error, file=sys.stderr)
    finally:
        if sim:
            sim.kill()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
