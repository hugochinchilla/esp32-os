#!/usr/bin/env python3
"""Previews of what the piezo beeper (GPIO14) would play (design exploration).

The beeper gets one square wave at a time from the chip's PWM: a pitch, on or
off, nothing else. So every sound here is a list of (Hz, ms) steps, 0 Hz
being silence: the very list the firmware would play. The preview passes the
square wave through a rough model of a small piezo disc (weak below 1 kHz, a
resonance near 3.5 kHz), so the files give the character, not the exact
loudness, of the real part.

Output (default /mnt/project-files/look/sounds/beeper/):
  post/<name>.wav     the self-test's beeps: the one for "all well", in two
                      pitches, and the error codes
  chime/<name>.wav    candidates for the beeper's chime when the desktop loads
  chime2/<name>.wav   round two: slower, calmer, with notes that fade
  chime3/<name>.wav   round three: longer, with a bell that echoes away
  chime4/<name>.wav   round four: the reference's length and shape, our notes
  manifest.tsv        name, seconds and the steps of each

Needs only numpy.  Run:  python3 tools/sounds/make_beeps.py [out_dir]

To hear a tune of your own before it goes to C:\\.config\\chime.txt (the
format is in docs/settings.md: "Hz ms [fade_ms [loudness]]" a line):
  python3 tools/sounds/make_beeps.py --file chime.txt     -> chime.txt.wav
"""
import os
import sys
import wave

import numpy as np

SR = 44100
OVER = 8  # the square wave is made at 8 times the rate, then averaged down
FILE = sys.argv[sys.argv.index("--file") + 1] if "--file" in sys.argv else None
OUT = sys.argv[1] if len(sys.argv) > 1 and not FILE else "/mnt/project-files/look/sounds/beeper"


def note(name):
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    pitch, octave = name[:-1], int(name[-1])
    return round(440.0 * 2 ** ((names.index(pitch) + 12 * (octave + 1) - 69) / 12))


def duty(level):
    """Loudness 0..1 -> the pulse's width: its fundamental goes as sin(pi d)."""
    return np.arcsin(min(1.0, max(0.0, level))) / np.pi


def square(steps):
    """(Hz, ms[, level]) steps -> the pin's signal; silence 0; phase runs on.

    Without a level the wave is square. With one, the pulse is narrower: the
    only way a pin has to play more quietly, and so to let a note fade.
    """
    parts, phase = [], 0.0
    for step in steps:
        hz, ms = step[0], step[1]
        d = duty(step[2]) if len(step) > 2 else 0.5
        n = int(ms * SR * OVER / 1000)
        if hz == 0:
            parts.append(np.zeros(n))
            continue
        ph = (phase + np.arange(n) * hz / (SR * OVER)) % 1.0
        phase = (phase + n * hz / (SR * OVER)) % 1.0
        parts.append(np.where(ph < d, 1.0, 0.0) - d)
    x = np.concatenate(parts)
    return x[: len(x) // OVER * OVER].reshape(-1, OVER).mean(axis=1)


def biquad(x, b, a):
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    b0, b1, b2 = (v / a[0] for v in b)
    a1, a2 = a[1] / a[0], a[2] / a[0]
    for i, v in enumerate(x):
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def piezo(x):
    """A small piezo disc: little below 1 kHz, a peak near 3.5 kHz."""
    w = 2 * np.pi * 900 / SR  # high-pass
    al = np.sin(w) / (2 * 0.7)
    c = np.cos(w)
    x = biquad(x, [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al])
    w = 2 * np.pi * 3500 / SR  # the resonance, +12 dB
    A = 10 ** (12 / 40)
    al = np.sin(w) / (2 * 2.5)
    c = np.cos(w)
    x = biquad(x, [1 + al * A, -2 * c, 1 - al * A], [1 + al / A, -2 * c, 1 - al / A])
    return x


def write(path, steps):
    x = piezo(np.concatenate([np.zeros(SR // 20), square(steps), np.zeros(SR // 5)]))
    return x, path


def save(path, x, peak):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = np.clip(x / peak * 0.5, -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((pcm * 32767).astype("<i2").tobytes())


# ------------------------------------------------------------------- the sounds
SHORT, LONG, GAP = 150, 600, 150


def code(longs, shorts, hz=1000):
    steps = []
    for ms in [LONG] * longs + [SHORT] * shorts:
        steps += [(hz, ms), (0, GAP)]
    return steps[:-1]


def sweep(f0, f1, ms, step=5):
    n = ms // step
    return [(round(f0 * (f1 / f0) ** (i / max(1, n - 1))), step) for i in range(n)]


def bell(name, ms, tau=None, step=20, top=1.0):
    """A note that fades like a struck bar: steps of falling loudness."""
    tau = tau or ms / 2.5
    return [(note(name), step, max(0.03, top * np.exp(-i * step / tau))) for i in range(ms // step)]


def phrase(notes, **kw):
    out = []
    for name, ms in notes:
        out += [(0, ms)] if name is None else bell(name, ms, **kw)
    return out


def trill(names, each, total):
    out, i = [], 0
    while sum(ms for _, ms in out) < total:
        out.append((note(names[i % len(names)]), each))
        i += 1
    return out


POST = {
    "ok-1000hz": ("All well, the PC's pitch: one short beep at 1,000 Hz.", [(1000, 200)]),
    "ok-2000hz": ("All well, an octave up, where a piezo is louder.", [(2000, 120)]),
    "error-not-fatal": ("Two short: a check failed, starting anyway (keyboard, sound).", code(0, 2)),
    "error-memory": ("Three long: the memory test failed.", code(3, 0)),
    "error-video": ("One long, two short: video.", code(1, 2)),
    "error-drive": ("One long, three short: drive C:.", code(1, 3)),
    "error-no-system": ("Long beeps without end: no operating system (three shown).",
                        [(1000, LONG), (0, 400)] * 3),
}

CHIME = {
    "a-up-we-go": ("Up we go: C major climbing an octave, the last note held.",
                   [(note("C6"), 70), (note("E6"), 70), (note("G6"), 70), (note("C7"), 200)]),
    "b-ta-da": ("Ta-da: a short note, a breath, a longer one a fourth up.",
                [(note("G6"), 80), (0, 40), (note("C7"), 240)]),
    "c-chip-chord": ("Chip chord: three notes swapped so fast they pass for a chord, then the top.",
                     trill(["C6", "E6", "G6"], 16, 290) + [(note("C7"), 160)]),
    "d-chirp": ("Chinchilla chirp: two quick rising squeaks.",
                sweep(2400, 4200, 70) + [(0, 60)] + sweep(2800, 4800, 90)),
    "e-pad-notes": ("The jack's boot chord, its notes one by one: F, C, G, A, E.",
                    [(note("F5"), 80), (note("C6"), 80), (note("G6"), 80), (note("A6"), 80), (note("E7"), 220)]),
}


# Round two (the owner, 2026-11-18: "more mellow, calm, not so fast", a longer
# melody). Every note fades, by narrowing the pulse; melodies of our own.
CHIME2 = {
    "f-dawn": ("Dawn: a long note, three rising ones, then three slow bells.",
               phrase([("E5", 600), ("G5", 220), ("A5", 220), ("B5", 320), ("D6", 480), ("B5", 480), ("E6", 1000)])),
    "f-dawn-plain": ("Dawn again with plain square notes, no fading, to compare.",
                     [(note(n), ms) for n, ms in [("E5", 560)]] + [(0, 40)] +
                     [s for n, ms in [("G5", 200), ("A5", 200), ("B5", 300), ("D6", 440), ("B5", 440), ("E6", 700)]
                      for s in ((note(n), ms), (0, 20))]),
    "g-lullaby": ("Lullaby: four slow notes rocking down and up.",
                  phrase([("C6", 520), ("G5", 520), ("A5", 520), ("E6", 1100)])),
    "h-andes-calm": ("Andes, calm: a pentatonic phrase that rises, waits, and comes home.",
                     phrase([("A5", 420), ("C6", 420), ("D6", 700), (None, 120), ("E6", 320), ("D6", 320),
                             ("A5", 1100)])),
    "i-three-bells": ("Three bells: three long, far-apart notes, nothing else.",
                      phrase([("G5", 750), ("D6", 750), ("B5", 1300)], tau=420)),
    "j-pad-phrase": ("The jack's boot chord as a slow phrase: F, C, G, A, and E to rest.",
                     phrase([("F5", 520), ("C6", 320), ("G6", 320), ("A6", 640), ("E6", 1200)])),
}


def echoes(name, ms, tops, tau=320):
    out = []
    for top in tops:
        out += bell(name, ms, tau=tau, top=top)
    return out


# Round three (the owner, 2026-11-18, with a 1995 start-up sound as a reference
# for the mood, not the notes: six seconds, a soft opening, a swell, then
# notes repeating ever more quietly). Our own melodies in that shape: a held
# note, a small rising figure, then one bell struck three times, dying away.
CHIME3 = {
    "k-dawn-echo": ("Dawn with echoes: a held note, three rising, then one high bell three times, fading.",
                    phrase([("E5", 900), ("G5", 260), ("A5", 260), ("B5", 560)], tau=300) +
                    echoes("E6", 760, [1.0, 0.5, 0.25]) + [(0, 300)]),
    "l-andes-echo": ("Andes with echoes: the pentatonic phrase, its last note answering itself.",
                     phrase([("A5", 850), ("C6", 300), ("D6", 300), ("E6", 620)], tau=300) +
                     echoes("A6", 720, [0.9, 0.45, 0.22]) + [(0, 300)]),
    "m-bells-echo": ("Bells with echoes: two slow notes, then a third that rings three times.",
                     phrase([("G5", 1000), ("D6", 1000)], tau=420) + echoes("B5", 800, [1.0, 0.5, 0.25], tau=380) +
                     [(0, 300)]),
}


def rising(notes, ms, tau=330):
    """Bells that climb and grow quieter: (name, loudness) pairs."""
    out = []
    for name, top in notes:
        out += bell(name, ms, tau=tau, top=top)
    return out


# Round four (the owner: "make it different but not so different"). The
# reference's length and shape, measured, with notes of our own that were
# not taken from it: about 5.5 s, a quiet held note, a fuller one at 0.85 s
# where the reference swells, a small figure, then bells that climb and
# fade, as the reference grows brighter while it dies away.
CHIME4 = {
    "n-first-light": ("First light: D major. A quiet held note, a full one, a small figure, four bells climbing away.",
                      [(note("D5"), 850, 0.35)] + phrase([("A5", 520), ("F#5", 260), ("A5", 260), ("D6", 620)], tau=300) +
                      rising([("F#6", 0.8), ("A6", 0.55), ("D7", 0.38), ("F#7", 0.22)], 720) + [(0, 300)]),
    "o-glass-morning": ("Glass morning: the same shape on the jack's boot chord (F, C, G, A, E).",
                        [(note("F5"), 850, 0.35)] + phrase([("C6", 520), ("G5", 260), ("A5", 260), ("C6", 620)], tau=300) +
                        rising([("E6", 0.8), ("G6", 0.55), ("A6", 0.38), ("E7", 0.22)], 720) + [(0, 300)]),
    "p-first-light-slow": ("First light, slower and sparser: three bells, further apart.",
                           [(note("D5"), 850, 0.35)] + phrase([("A5", 700), ("F#5", 350), ("D6", 800)], tau=380) +
                           rising([("A6", 0.75), ("D7", 0.45), ("A6", 0.22)], 950, tau=420) + [(0, 300)]),
}


def main():
    made = []
    for folder, sounds in (("post", POST), ("chime", CHIME), ("chime2", CHIME2), ("chime3", CHIME3), ("chime4", CHIME4)):
        for name, (text, steps) in sounds.items():
            x, path = write(os.path.join(OUT, folder, name + ".wav"), steps)
            made.append((folder, name, text, steps, x, path))
    peak = max(np.abs(m[4]).max() for m in made)  # one level for all: loudness compares
    with open(os.path.join(OUT, "manifest.tsv"), "w") as f:
        f.write("category\tname\tseconds\tdescription\tsteps_hz_ms\n")
        for folder, name, text, steps, x, path in made:
            save(path, x, peak)
            seconds = sum(st[1] for st in steps) / 1000
            shown = " ".join(f"{st[0]}:{st[1]}" for st in steps) if len(steps) <= 12 else f"{len(steps)} steps"
            f.write(f"{folder}\t{name}\t{seconds:.2f}\t{text}\t{shown}\n")
            print(f"{folder}/{name}.wav  {seconds:.2f} s  {text}")


def from_file(path):
    """A chime.txt as the firmware reads it (sound::parse_tune) -> steps."""
    steps = []
    for line in open(path):
        words = line.split("#")[0].replace(",", " ").split()
        if not 2 <= len(words) <= 4 or not all(w.isdigit() for w in words):
            continue
        hz, ms, fade, level = (list(map(int, words)) + [0, 0])[:4]
        if hz > 20000 or not 0 < ms <= 10000 or fade > 10000 or level > 255 or len(steps) >= 64:
            continue
        top = level / 255 if level else 1.0
        if hz == 0 or (not fade and not level):
            steps.append((hz, ms))
        elif not fade:
            steps.append((hz, ms, top))
        else:  # as firmware/common/beeper.h: narrower every 20 ms
            done = 0
            while done < ms:
                steps.append((hz, min(20, ms - done), max(0.03, top * np.exp(-done / fade))))
                done += 20
    return steps


if __name__ == "__main__":
    if FILE:
        x, path = write(FILE + ".wav", from_file(FILE))
        save(os.path.abspath(path), x, max(1e-9, np.abs(x).max()))
        print(path)
    else:
        os.makedirs(OUT, exist_ok=True)
        main()
