#!/usr/bin/env python3
"""Synthesize the sounds of the games and of the Trash (backlog item 70):
a chess piece put down, check, checkmate; Culebra's snake eating a berry,
and its game over; paper crumpled when the Trash is emptied.

Everything is made from oscillators and noise with the building blocks of
make_sounds.py; nothing is sampled from a recording or copied from another
system. The timbres are those of the system's sounds the owner chose on
2026-11-16 (wood, kalimba, the mellow electric piano), so that the games
sound like the same machine.

Output (default /mnt/project-files/look/sounds/games/):
  <sound>-<letter>-<name>.wav   the candidates for each sound, to listen to
  final/<file>.wav              the one of each that the build plays
and the final ones as raw samples in docs/sounds/<file>.pcm (16 bits, one
channel, 22,050 a second), which is what the PC and browser builds compile
in.

Needs only numpy.  Run:  python3 tools/sounds/make_game_sounds.py [out_dir]
"""
import os
import sys

import numpy as np

sys.argv, argv = sys.argv[:1], sys.argv  # make_sounds reads its folder from the arguments
import make_sounds as ms  # noqa: E402
from make_sounds import SR, additive_bell, chip_tone, fm, note, onepole_lp, place, reverb, t_axis, wood  # noqa: E402

OUT = argv[1] if len(argv) > 1 else "/mnt/project-files/look/sounds/games"
rng = np.random.default_rng(1996)  # its own, so that the system's sounds stay as they are


def highpass(x, fc):
    return x - onepole_lp(x, fc)


def pluck(f, tau=0.10, dur=None):
    """The kalimba tine of the terminal's bell, dry."""
    t = t_axis(dur or 6 * tau)
    y = np.sin(2 * np.pi * f * t) * np.exp(-t / tau) + 0.5 * np.sin(2 * np.pi * f * 5.95 * t) * np.exp(-t / 0.02)
    y += 0.15 * np.sin(2 * np.pi * f * 2 * t) * np.exp(-t / (tau / 3))
    return y * np.clip(t / 0.001, 0, 1)


def tock(low=430.0, bright=1.0, dur=0.16):
    """A wooden piece put down on a wooden board: the click of the touch and
    two short resonances of the wood."""
    t = t_axis(dur)
    y = np.sin(2 * np.pi * low * t) * np.exp(-t / 0.022)
    y += 0.55 * bright * np.sin(2 * np.pi * low * 2.71 * t + 1.0) * np.exp(-t / 0.010)
    y += 0.30 * bright * np.sin(2 * np.pi * low * 5.3 * t + 2.0) * np.exp(-t / 0.004)
    y += 0.5 * bright * onepole_lp(rng.standard_normal(len(t)), 5000) * np.exp(-t / 0.0025)
    return y * np.clip(t / 0.0005, 0, 1)


# ================================================================ CHESS: A MOVE
def move_a():
    """Wood: a wooden piece set down on a wooden board, short and dry."""
    return reverb(tock(), 0.08, 0.5, 0.08)


def move_b():
    """Felt: the same piece with a felt foot, lower and softer, no click."""
    t = t_axis(0.16)
    y = np.sin(2 * np.pi * 250 * t) * np.exp(-t / 0.03) + 0.3 * np.sin(2 * np.pi * 610 * t) * np.exp(-t / 0.012)
    y += 0.6 * onepole_lp(rng.standard_normal(len(t)), 900) * np.exp(-t / 0.008)
    return y * np.clip(t / 0.002, 0, 1)


def move_c():
    """Stone: a small hard click, as a piece on a marble board."""
    return tock(low=900.0, bright=1.6, dur=0.10)


# ================================================================ CHESS: CHECK
def check_a():
    """The piece, then two kalimba notes going up: look here."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    tr = place(tr, pluck(note("E5"), 0.07), 0.07, 0.6)
    tr = place(tr, pluck(note("A5"), 0.10), 0.17, 0.7)
    return reverb(tr, 0.10, 0.5, 0.15)


def check_b():
    """Two knocks: the piece, and a second, higher knock on the board."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    tr = place(tr, wood(note("A5"), 0.3), 0.11, 0.6)
    return reverb(tr, 0.12, 0.6, 0.2)


def check_c():
    """The piece, then one small glass bell."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    bell = additive_bell(0.7, note("E6"), [(1, 1, 1), (2.0, 0.3, 1.8), (3.01, 0.25, 2.6), (4.2, 0.1, 4)], 0.22)
    tr = place(tr, bell, 0.07, 0.5)
    return reverb(tr, 0.15, 0.8, 0.3)


# The owner, 2026-11-19, on the three above: check "suena alegre"; it is heard
# when the player is put in check, so it must warn, as an alert does.
def check_d():
    """The piece, then the same high wooden note twice, quickly: a warning."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    tr = place(tr, wood(note("D6"), 0.25), 0.09, 0.6)
    tr = place(tr, wood(note("D6"), 0.3), 0.20, 0.6)
    return reverb(tr, 0.14, 0.7, 0.3)


def check_e():
    """The piece, then two kalimba notes falling a tense step (a tritone)."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    tr = place(tr, pluck(note("A5"), 0.07), 0.08, 0.65)
    tr = place(tr, pluck(note("D#5"), 0.12), 0.20, 0.7)
    return reverb(tr, 0.12, 0.6, 0.25)


def check_f():
    """The piece, then the message box's knock turned over: two wooden notes
    going down instead of up."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    tr = place(tr, wood(note("G5")), 0.09, 0.6)
    tr = place(tr, wood(note("C#5")), 0.20, 0.6)
    return reverb(tr, 0.16, 0.7, 0.4)


# ================================================================ CHESS: CHECKMATE
def mate_a():
    """The piece, then the kalimba coming down to rest on a low wooden note:
    the game is over, whoever won."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    for n, at, tau in [("A5", 0.08, 0.08), ("E5", 0.22, 0.08), ("C#5", 0.36, 0.09), ("A4", 0.52, 0.22)]:
        tr = place(tr, pluck(note(n), tau), at, 0.6)
    tr = place(tr, wood(note("A3"), 0.6), 0.52, 0.5)
    return reverb(tr, 0.16, 0.8, 0.5)


def mate_b():
    """The piece, then a glass chord that rings out."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    partials = [(1, 1, 1), (2.0, 0.3, 1.8), (3.01, 0.25, 2.6), (4.2, 0.1, 4)]
    for n, at in [("A5", 0.08), ("C#6", 0.11), ("E6", 0.14)]:
        tr = place(tr, additive_bell(1.3, note(n), partials, 0.4), at, 0.3)
    return reverb(tr, 0.25, 1.0, 0.7)


def mate_c():
    """The piece, then two low notes of the Shut Down piano: the end."""
    tr = place(np.zeros(1), tock(), 0.0, 0.8)
    tr = place(tr, fm(0.9, note("E4"), 1.0, 0.7, 0.35, a=0.01, index_tau=0.2), 0.10, 0.5)
    tr = place(tr, fm(1.3, note("A3"), 1.0, 0.7, 0.5, a=0.012, index_tau=0.25), 0.36, 0.6)
    return onepole_lp(reverb(tr, 0.25, 1.0, 0.7), 3000)


# ================================================================ CULEBRA: A BERRY
def eat_a():
    """Two quick kalimba notes going up, dry: got it."""
    tr = place(np.zeros(1), pluck(note("E5"), 0.035), 0.0, 0.7)
    tr = place(tr, pluck(note("B5"), 0.05), 0.045, 0.7)
    return tr


def eat_b():
    """A gulp: one round note that slides up fast."""
    t = t_axis(0.14)
    f = 330 * (900 / 330) ** np.clip(t / 0.07, 0, 1)
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.045)
    return y * np.clip(t / 0.003, 0, 1)


def eat_c():
    """A bite: a short crunch and a small wooden pop."""
    t = t_axis(0.05)
    crunch = highpass(rng.standard_normal(len(t)), 1500) * np.exp(-t / 0.012)
    tr = place(np.zeros(1), 0.35 * crunch, 0.0)
    tr = place(tr, wood(note("G5"), 0.18), 0.02, 0.6)
    return tr


# ================================================================ CULEBRA: GAME OVER
def over_a():
    """Wooden bars stepping down, and a low thud: the snake hit something."""
    tr = np.zeros(1)
    for n, at in [("G4", 0.0), ("E4", 0.13), ("C#4", 0.26)]:
        tr = place(tr, wood(note(n), 0.35), at, 0.6)
    tr = place(tr, wood(note("A2"), 0.6), 0.42, 0.9)
    return reverb(tr, 0.16, 0.7, 0.4)


def over_b():
    """One electric piano note that droops down an octave."""
    t = t_axis(0.8)
    f = note("A4") * 2 ** (-np.clip(t / 0.6, 0, 1) ** 1.5)
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = np.sin(ph + 1.2 * np.exp(-t / 0.3) * np.sin(ph)) * np.exp(-t / 0.35) * np.clip(t / 0.008, 0, 1)
    return onepole_lp(reverb(y, 0.2, 0.9, 0.4), 3500)


def over_c():
    """The kalimba falling: three notes down, the last one left to ring."""
    tr = np.zeros(1)
    for n, at, tau in [("B4", 0.0, 0.07), ("G4", 0.14, 0.07), ("E4", 0.28, 0.2)]:
        tr = place(tr, pluck(note(n), tau), at, 0.7)
    return reverb(tr, 0.15, 0.7, 0.4)


# ================================================================ THE TRASH: PAPER
def crackle(dur, squeezes, rate=900.0, dark=1.0):
    """Paper being crumpled: a great many tiny cracks, each a few
    thousandths of a second long and of its own pitch and loudness, thick
    while the hand squeezes and thin between squeezes, over a quiet rustle.
    `squeezes` is a list of (when, how long, how hard)."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    effort = np.zeros(n)
    for at, length, hard in squeezes:
        effort += hard * np.exp(-0.5 * ((t - at) / (length / 2.5)) ** 2)
    y = np.zeros(n + int(0.01 * SR))
    # The cracks come at random, more of them the harder the squeeze.
    count = rng.poisson(rate * np.sum(effort) / SR)
    when = rng.choice(n, size=count, p=effort / np.sum(effort))
    for i in when:
        tau = rng.uniform(0.0003, 0.0018)
        m = int(6 * tau * SR) + 2
        k = np.arange(m) / SR
        f = rng.uniform(1800, 8000) / dark
        loud = rng.pareto(2.5) * 0.25 + 0.05  # a few loud ones among many quiet
        y[i:i + m] += min(loud, 1.5) * np.sin(2 * np.pi * f * k + rng.uniform(0, 6.28)) * np.exp(-k / tau)
    y = y[:n]
    rustle = highpass(onepole_lp(rng.standard_normal(n), 6000 / dark), 1200)
    y += 0.10 * rustle * effort
    return highpass(y, 600) * np.clip(effort * 4, 0, 1) ** 0.5


def crumple_a():
    """A sheet crumpled into a ball: three squeezes of the hand, under a second."""
    return crackle(0.85, [(0.14, 0.26, 1.0), (0.40, 0.24, 0.8), (0.63, 0.22, 0.55)])


def crumple_b():
    """The same, and the ball dropped into the bin."""
    tr = place(np.zeros(1), crackle(0.62, [(0.12, 0.22, 1.0), (0.36, 0.24, 0.75)]), 0.0)
    t = t_axis(0.18)
    thud = np.sin(2 * np.pi * 150 * t) * np.exp(-t / 0.03) + 0.5 * np.sin(2 * np.pi * 410 * t) * np.exp(-t / 0.015)
    thud += 0.5 * crackle(0.18, [(0.02, 0.05, 1.0)], rate=1500)
    return place(tr, thud * np.clip(t / 0.001, 0, 1), 0.74, 0.5)


def crumple_c():
    """One quick scrunch, darker: less than half a second."""
    return crackle(0.42, [(0.12, 0.2, 1.0), (0.27, 0.16, 0.7)], rate=1100, dark=1.3)


CANDIDATES = [
    ("chess-move", [("a-wood", move_a), ("b-felt", move_b), ("c-stone", move_c)]),
    ("chess-check", [("a-kalimba-up", check_a), ("b-two-knocks", check_b), ("c-glass-bell", check_c)]),
    ("chess-mate", [("a-kalimba-down", mate_a), ("b-glass-chord", mate_b), ("c-low-piano", mate_c)]),
    ("snake-eat", [("a-kalimba-up", eat_a), ("b-gulp", eat_b), ("c-bite", eat_c)]),
    ("snake-over", [("a-wood-down", over_a), ("b-droop", over_b), ("c-kalimba-down", over_c)]),
    ("trash-crumple", [("a-three-squeezes", crumple_a), ("b-into-the-bin", crumple_b), ("c-quick", crumple_c)]),
    # The second round for check, made last so that the random source gives
    # the sounds above the same noise as when the owner heard them.
    ("chess-check", [("d-wood-twice", check_d), ("e-kalimba-tritone", check_e), ("f-wood-down", check_f)]),
]

# What the build plays: docs/sounds/<file>.pcm, in the order of sound::Id
# after the system's five. The owner's picks of 2026-11-19 (the move, the
# berry and checkmate as first built; game over b; the paper c), but for
# check, which is the agent's pick of the second three and waits for him.
FINAL = [
    ("chess_move", move_a, -14.0),
    ("chess_check", check_d, -12.0),
    ("chess_mate", mate_a, -12.0),
    ("snake_eat", eat_a, -14.0),
    ("snake_over", over_b, -12.0),
    ("crumple", crumple_c, -15.0),
]


def main():
    lines = ["sound\tcandidate\tseconds\tdescription"]
    made = {}
    for sound, options in CANDIDATES:
        for name, fn in options:
            x = ms.finish(fn())
            made[fn] = x
            ms.write_wav(f"{OUT}/{sound}-{name}.wav", x)
            lines.append(f"{sound}\t{name}\t{len(x) / SR:.2f}\t{' '.join(fn.__doc__.split())}")
            print(f"{sound}-{name}: {len(x) / SR:.2f} s")
    repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs", "sounds")
    for file, fn, db in FINAL:
        # The short ones a little quieter than the system's sounds: they
        # are heard many times in a game.
        x = made[fn] * 10 ** ((db + 12.0) / 20)
        ms.write_wav(f"{OUT}/final/{file}.wav", x)
        with open(os.path.join(repo, file + ".pcm"), "wb") as f:
            f.write((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())
    with open(f"{OUT}/manifest.tsv", "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
