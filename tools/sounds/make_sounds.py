#!/usr/bin/env python3
"""Synthesize candidate system sounds for Chinchilla (design exploration).

Everything here is generated from oscillators and noise; nothing is sampled
from a recording or copied from another system.

Output (default /mnt/project-files/look/sounds/):
  <category>/<name>.wav            22,050 Hz, 16-bit mono: the sound as designed
  <category>/board/<name>.wav      the same through the adapter board's audio
                                   filter (1 kOhm + 100 nF, about 1.6 kHz) and
                                   8-bit samples: roughly what the jack gives
  round2/...                       the owner's round-one picks with his changes
  final/...                        the five sounds he chose; 8-bit copies also
                                   go to docs/sounds/ in the repository

The piezo beeper (GPIO14) gets no system sounds: it is kept for the modem.

Needs only numpy.  Run:  python3 tools/sounds/make_sounds.py [out_dir]
"""
import os
import sys
import wave

import numpy as np

SR = 22050
OUT = sys.argv[1] if len(sys.argv) > 1 else "/mnt/project-files/look/sounds"
rng = np.random.default_rng(1998)


# ---------------------------------------------------------------- building blocks
def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def note(name):
    """'A4' -> Hz.  Sharps only (C#, F#...)."""
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    pitch, octave = name[:-1], int(name[-1])
    n = names.index(pitch) + 12 * (octave + 1)
    return 440.0 * 2 ** ((n - 69) / 12)


def env_adsr(n, a, d, s, r, sus_len=None):
    a, d, r = int(a * SR), int(d * SR), int(r * SR)
    if sus_len is None:
        sus_len = max(0, n - a - d - r)
    else:
        sus_len = int(sus_len * SR)
    e = np.concatenate([
        np.linspace(0, 1, a, endpoint=False),
        np.linspace(1, s, d, endpoint=False),
        np.full(sus_len, s),
        np.linspace(s, 0, r),
    ])
    out = np.zeros(n)
    out[:min(n, len(e))] = e[:n]
    return out


def env_exp(n, a, tau):
    t = np.arange(n) / SR
    att = np.clip(t / max(a, 1e-4), 0, 1)
    return att * np.exp(-t / tau)


def fm(dur, f, ratio, index, tau, a=0.003, index_tau=None, fb=0.0):
    """Two-operator FM voice: the 90s sound-card staple."""
    t = t_axis(dur)
    n = len(t)
    ienv = np.exp(-t / (index_tau or tau))
    mod = np.sin(2 * np.pi * f * ratio * t)
    if fb:
        mod = np.sin(2 * np.pi * f * ratio * t + fb * mod)
    y = np.sin(2 * np.pi * f * t + index * ienv * mod)
    return y * env_exp(n, a, tau)


def additive_bell(dur, f, partials, tau):
    """partials: list of (ratio, amp, decay factor)."""
    t = t_axis(dur)
    y = np.zeros_like(t)
    for ratio, amp, k in partials:
        y += amp * np.sin(2 * np.pi * f * ratio * t + rng.uniform(0, 6.28)) * np.exp(-t * k / tau)
    return y * np.clip(t / 0.002, 0, 1)


def square(dur, f, duty=0.5):
    t = t_axis(dur)
    ph = (t * f) % 1.0
    return np.where(ph < duty, 1.0, -1.0)


def saw(t, f):
    return 2 * ((t * f) % 1.0) - 1


def onepole_lp(x, fc):
    a = np.exp(-2 * np.pi * fc / SR)
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def svf_lp(x, fc_curve, q=0.7):
    """State-variable low-pass with a time-varying cutoff (array or scalar)."""
    fc = np.broadcast_to(np.asarray(fc_curve, dtype=float), x.shape)
    low = band = 0.0
    y = np.zeros_like(x)
    damp = 1.0 / q
    for i, v in enumerate(x):
        f = 2 * np.sin(np.pi * min(fc[i], SR / 6) / SR)
        high = v - low - damp * band
        band += f * high
        low += f * band
        y[i] = low
    return y


def comb(x, delay_s, g):
    d = int(delay_s * SR)
    y = x.copy()
    for k in range(d, len(y), d):
        end = min(k + d, len(y))
        y[k:end] += g * y[k - d:end - d]
    return y


def reverb(x, mix=0.25, size=1.0, tail=1.2):
    """Small Schroeder-style room: parallel combs, then two all-pass smears."""
    x = np.concatenate([x, np.zeros(int(tail * SR))])
    wet = sum(comb(x, d * size, g) for d, g in
              [(0.0297, 0.78), (0.0371, 0.76), (0.0411, 0.74), (0.0437, 0.72)]) / 4
    for d, g in [(0.005, 0.7), (0.0017, 0.7)]:
        n = int(d * SR)
        y = np.zeros_like(wet)
        buf = np.concatenate([np.zeros(n), wet])
        yb = np.zeros(len(wet) + n)
        for i in range(len(wet)):
            yb[i + n] = -g * buf[i + n] + buf[i] + g * yb[i]
        wet = yb[n:]
    wet = onepole_lp(wet, 5000)
    return (1 - mix) * x + mix * wet


def place(track, sig, at, gain=1.0):
    i = int(at * SR)
    end = i + len(sig)
    if end > len(track):
        track = np.concatenate([track, np.zeros(end - len(track))])
    track[i:end] += gain * sig
    return track


def loudest_rms(x, win=0.3):
    n = int(win * SR)
    if len(x) <= n:
        return np.sqrt(np.mean(x ** 2))
    c = np.concatenate([[0], np.cumsum(x ** 2)])
    return np.sqrt(np.max(c[n:] - c[:-n]) / n)


def finish(x, peak=0.89, target_db=-12.0):
    """Trim the tail below -60 dB, fade, and match loudness across sounds."""
    x = x - np.mean(x)
    thresh = 1e-3 * np.max(np.abs(x))
    nz = np.nonzero(np.abs(x) > thresh)[0]
    x = x[: nz[-1] + 1] if len(nz) else x
    fade = min(len(x), int(0.03 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    gain = min(peak / np.max(np.abs(x)), 10 ** (target_db / 20) / loudest_rms(x))
    return x * gain


# ---------------------------------------------------------------- output
manifest = []


def write_wav(path, x, bits=16):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setframerate(SR)
        if bits == 16:
            w.setsampwidth(2)
            w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())
        else:
            w.setsampwidth(1)
            w.writeframes((np.clip(x, -1, 1) * 127 + 128).round().astype(np.uint8).tobytes())


def board_version(x):
    """What the jack gives: 8-bit samples, then the first-order RC filter."""
    q = np.round(np.clip(x, -1, 1) * 127) / 127
    y = onepole_lp(q, 1.0 / (2 * np.pi * 1000 * 100e-9))
    return y * min(0.89 / np.max(np.abs(y)), loudest_rms(x) / loudest_rms(y))  # same loudness


def save(category, name, x, blurb):
    x = finish(x)
    write_wav(f"{OUT}/{category}/{name}.wav", x)
    write_wav(f"{OUT}/{category}/board/{name}.wav", board_version(x))
    manifest.append((category, name, len(x) / SR, blurb))
    print(f"{category}/{name}: {len(x)/SR:.2f} s")


# ================================================================ BOOT CHIME
def boot_a():
    """Sunrise: FM electric piano climbing a lydian arpeggio, with a shimmer."""
    tr = np.zeros(1)
    seq = [("D4", 0.00), ("A4", 0.14), ("E5", 0.28), ("G#5", 0.42), ("C#6", 0.56)]
    for i, (n, at) in enumerate(seq):
        f = note(n)
        v = fm(2.6 - at, f, 1.0, 2.2, 0.9, index_tau=0.25) + 0.25 * fm(2.6 - at, f, 14.0, 0.8, 0.25)
        tr = place(tr, v, at, 0.5 - 0.04 * i)
    pad = np.zeros(int(2.6 * SR))
    t = t_axis(2.6)
    for n in ["D3", "A3", "E4"]:
        f = note(n)
        pad += 0.15 * (np.sin(2 * np.pi * f * t) + 0.5 * np.sin(2 * np.pi * f * 1.003 * 2 * t))
    pad *= env_adsr(len(t), 0.6, 0.6, 0.6, 1.2)
    tr = place(tr, pad, 0.0)
    return reverb(tr, 0.3, 1.1, 1.4)


def flute(dur, f, breath=0.18, vib=5.2):
    t = t_axis(dur)
    vibrato = 1 + 0.006 * np.sin(2 * np.pi * vib * t) * np.clip(t / 0.3, 0, 1)
    ph = 2 * np.pi * np.cumsum(f * vibrato) / SR
    tone = np.sin(ph) + 0.12 * np.sin(2 * ph) + 0.04 * np.sin(3 * ph)
    noise = svf_lp(rng.standard_normal(len(t)), f * 2.5, q=4) * breath
    chiff = rng.standard_normal(len(t)) * np.exp(-t / 0.02) * 0.25
    return (tone + noise + chiff) * env_adsr(len(t), 0.05, 0.1, 0.8, min(0.35, dur * 0.5))


def boot_b():
    """Andes: a breathy pan-pipe phrase, pentatonic, for the chinchilla."""
    tr = np.zeros(1)
    phrase = [("E5", 0.0, 0.22), ("G5", 0.2, 0.22), ("A5", 0.4, 0.4),
              ("D6", 0.75, 0.25), ("B5", 0.98, 1.3)]
    for n, at, d in phrase:
        tr = place(tr, flute(d, note(n)), at, 0.45)
    t = t_axis(2.4)
    drone = (np.sin(2 * np.pi * note("E3") * t) + 0.5 * np.sin(2 * np.pi * note("B3") * t))
    tr = place(tr, 0.18 * drone * env_adsr(len(t), 0.4, 0.3, 0.7, 1.0), 0.0)
    return reverb(tr, 0.35, 1.3, 1.5)


def boot_c(bell_gain=0.22):
    """Glass pad: a slow detuned-saw chord opening up, a soft bell on top."""
    dur = 3.0
    t = t_axis(dur)
    x = np.zeros_like(t)
    for n in ["F3", "C4", "G4", "A4", "E5"]:
        f = note(n)
        for det in (-0.004, 0.0, 0.005):
            x += saw(t, f * (1 + det)) / 3
    fc = 300 + 2800 * np.clip(t / 1.4, 0, 1) ** 1.5 * np.exp(-np.clip(t - 1.6, 0, None) / 1.5)
    x = svf_lp(x * 0.12, fc, q=1.4) * env_adsr(len(t), 0.5, 0.7, 0.7, 1.3)
    bell = additive_bell(2.2, note("C6"), [(1, 1, 1), (2.76, 0.4, 2), (5.4, 0.2, 3.5), (8.9, 0.1, 5)], 0.9)
    x = place(x, bell_gain * bell, 0.9)
    return reverb(x, 0.35, 1.4, 1.5)


def chip_tone(dur, f, duty=0.25, tau=None, slide=0.0):
    t = t_axis(dur)
    fr = f * (1 + slide * t / max(dur, 1e-3))
    ph = np.cumsum(fr) / SR % 1.0
    y = np.where(ph < duty, 1.0, -1.0) - (2 * duty - 1)  # remove the pulse's DC
    e = np.exp(-t / tau) if tau else env_adsr(len(t), 0.002, 0.04, 0.7, 0.03)
    return y * e


def boot_d():
    """Chip fanfare: square-wave three-step flourish with an echo."""
    tr = np.zeros(1)
    seq = [("G4", 0, 0.09), ("C5", 0.1, 0.09), ("G5", 0.2, 0.09), ("F5", 0.32, 0.09),
           ("A5", 0.42, 0.09), ("C6", 0.52, 0.6)]
    for n, at, d in seq:
        tr = place(tr, chip_tone(d, note(n), 0.25), at, 0.3)
        tr = place(tr, chip_tone(d, note(n) / 2, 0.5), at, 0.15)
    tr = place(tr, 0.12 * chip_tone(0.4, note("C3"), 0.5, tau=0.3), 0.52)
    echo = np.zeros(len(tr) + int(0.4 * SR))
    echo = place(echo, tr, 0)
    echo = place(echo, tr, 0.18, 0.35)
    echo = place(echo, tr, 0.36, 0.12)
    return onepole_lp(echo, 7000)


# ================================================================ SESSION CLOSE
def close_a():
    """Sunset: the electric piano falling back down, matched to boot A."""
    tr = np.zeros(1)
    seq = [("C#6", 0.0), ("A5", 0.16), ("E5", 0.32), ("D4", 0.5)]
    for n, at in seq:
        f = note(n)
        tr = place(tr, fm(2.0 - at, f, 1.0, 1.6, 0.7, index_tau=0.2), at, 0.45)
    return reverb(tr, 0.3, 1.1, 1.3)


def close_b():
    """Andes, falling: the pipe phrase going to rest, matched to boot B."""
    tr = np.zeros(1)
    phrase = [("B5", 0.0, 0.25), ("A5", 0.22, 0.25), ("E5", 0.45, 0.3), ("D5", 0.72, 0.18), ("E5", 0.86, 1.1)]
    for n, at, d in phrase:
        tr = place(tr, flute(d, note(n), vib=4.5), at, 0.45)
    return reverb(tr, 0.38, 1.3, 1.5)


def close_c():
    """Glass pad closing: the chord's filter shutting as the pitch sinks a little."""
    dur = 2.2
    t = t_axis(dur)
    x = np.zeros_like(t)
    sink = 1 - 0.05 * np.clip(t / dur, 0, 1) ** 2
    for n in ["F3", "C4", "G4", "A4"]:
        f = note(n)
        for det in (-0.004, 0.005):
            ph = np.cumsum(f * (1 + det) * sink) / SR
            x += (2 * (ph % 1.0) - 1) / 2
    fc = 3000 * np.exp(-t / 0.6) + 150
    x = svf_lp(x * 0.15, fc, q=1.6) * env_adsr(len(t), 0.03, 0.2, 0.8, 1.2)
    return reverb(x, 0.3, 1.4, 1.2)


def close_d():
    """Chip power-down: a falling arpeggio sliding into a low blip."""
    tr = np.zeros(1)
    seq = [("C6", 0, 0.08), ("G5", 0.09, 0.08), ("E5", 0.18, 0.08), ("C5", 0.27, 0.08), ("G4", 0.36, 0.25)]
    for n, at, d in seq:
        tr = place(tr, chip_tone(d, note(n), 0.25), at, 0.3)
    tr = place(tr, 0.3 * chip_tone(0.35, note("C4"), 0.5, tau=0.15, slide=-0.6), 0.6)
    echo = place(np.zeros(len(tr) + int(0.3 * SR)), tr, 0)
    echo = place(echo, tr, 0.16, 0.3)
    return onepole_lp(echo, 7000)


# ================================================================ DIALOG (alert + error)
def wood(f, dur=0.5):
    """Marimba-ish bar: sine plus a 4x partial that dies fast, mallet click."""
    t = t_axis(dur)
    y = np.sin(2 * np.pi * f * t) * np.exp(-t / 0.18) + 0.35 * np.sin(2 * np.pi * f * 3.93 * t) * np.exp(-t / 0.03)
    y += rng.standard_normal(len(t)) * np.exp(-t / 0.003) * 0.3
    return y * np.clip(t / 0.001, 0, 1)


def dialog_a(kind):
    """FM 'bonk': rounded low tone; error is two falling steps."""
    if kind == "alert":
        x = fm(0.6, note("A4"), 2.0, 2.5, 0.22, index_tau=0.05)
        return reverb(x, 0.18, 0.8, 0.5)
    tr = np.zeros(1)
    tr = place(tr, fm(0.5, note("E4"), 1.0, 3.5, 0.2, index_tau=0.04, fb=0.6), 0.0, 0.6)
    tr = place(tr, fm(0.6, note("A#3"), 1.0, 3.5, 0.25, index_tau=0.05, fb=0.6), 0.13, 0.7)
    return reverb(tr, 0.15, 0.8, 0.5)


def dialog_b(kind):
    """Wood knock: marimba bars; alert is a rising pair, error a low double knock."""
    tr = np.zeros(1)
    if kind == "alert":
        tr = place(tr, wood(note("G5")), 0.0, 0.6)
        tr = place(tr, wood(note("D6")), 0.1, 0.5)
    else:
        tr = place(tr, wood(note("D4"), 0.4), 0.0, 0.8)
        tr = place(tr, wood(note("C#4"), 0.5), 0.12, 0.8)
    return reverb(tr, 0.16, 0.7, 0.5)


def dialog_c(kind):
    """Glass: soft bell partials; error rubs two notes a semitone apart."""
    partials = [(1, 1, 1), (2.0, 0.3, 1.8), (3.01, 0.25, 2.6), (4.2, 0.1, 4)]
    if kind == "alert":
        x = additive_bell(1.0, note("E6"), partials, 0.35)
        return reverb(x, 0.25, 0.9, 0.6)
    x = additive_bell(1.0, note("F5"), partials, 0.35) + 0.8 * additive_bell(1.0, note("E5"), partials, 0.35)
    return reverb(x, 0.22, 0.9, 0.6)


def dialog_d(kind):
    """Chip: square-wave blips; alert hops up, error buzzes down."""
    tr = np.zeros(1)
    if kind == "alert":
        tr = place(tr, chip_tone(0.06, note("A5"), 0.25), 0.0, 0.35)
        tr = place(tr, chip_tone(0.12, note("E6"), 0.25, tau=0.06), 0.07, 0.35)
    else:
        tr = place(tr, chip_tone(0.09, note("C4"), 0.5), 0.0, 0.3)
        tr = place(tr, chip_tone(0.22, note("F#3"), 0.5, tau=0.1, slide=-0.15), 0.1, 0.35)
    return onepole_lp(tr, 6000)


# ================================================================ BELL
def bell_a():
    """FM ding: a small clear bell (inharmonic 3.5 ratio)."""
    x = fm(1.2, note("A5"), 3.5, 3.0, 0.4, index_tau=0.12) + 0.3 * fm(1.2, note("A6"), 1.0, 0.5, 0.15)
    return reverb(x, 0.2, 0.8, 0.5)


def bell_b():
    """Kalimba: a plucked tine with a quick metallic overtone."""
    f = note("E5")
    t = t_axis(1.0)
    y = np.sin(2 * np.pi * f * t) * np.exp(-t / 0.35) + 0.5 * np.sin(2 * np.pi * f * 5.95 * t) * np.exp(-t / 0.025)
    y += 0.15 * np.sin(2 * np.pi * f * 2 * t) * np.exp(-t / 0.12)
    y *= np.clip(t / 0.0015, 0, 1)
    return reverb(y, 0.2, 0.7, 0.5)


def bell_c():
    """Desk bell: bright metallic ting, short and high."""
    partials = [(1, 1, 1), (2.32, 0.6, 1.4), (4.25, 0.35, 2.2), (6.63, 0.25, 3), (9.38, 0.15, 4)]
    x = additive_bell(1.0, note("C#6"), partials, 0.3)
    return reverb(x, 0.18, 0.8, 0.5)


def bell_d():
    """Chip beep: one clean square-wave beep with a tiny pitch drop."""
    return onepole_lp(chip_tone(0.15, note("B5"), 0.5, tau=0.12, slide=-0.03), 6000)


# ================================================================ ROUND 2
# The owner's picks from round one (2026-11-16) with the changes he asked for.
def boot_c_quiet_bell():
    """Glass pad, with the bell at a quarter of its round-one volume."""
    return boot_c(bell_gain=0.22 / 4)


def close_a_mellow(level):
    """Sunset, mellower: softer attack, less FM bite, slower fall, darker room."""
    tr = np.zeros(1)
    if level == 1:
        seq, index, attack, cutoff = [("C#6", 0.0), ("A5", 0.2), ("E5", 0.4), ("D4", 0.62)], 0.9, 0.012, 3500
    else:  # an octave lower for the top three notes, and softer still
        seq, index, attack, cutoff = [("C#5", 0.0), ("A4", 0.24), ("E4", 0.48), ("D3", 0.74)], 0.6, 0.02, 2200
    for n, at in seq:
        tr = place(tr, fm(2.4 - at, note(n), 1.0, index, 0.85, a=attack, index_tau=0.3), at, 0.45)
    return onepole_lp(reverb(tr, 0.34, 1.2, 1.4), cutoff)


def bell_b_short(tau, room):
    """Kalimba, faster and shorter: the tine dies quickly, with hardly any room."""
    f = note("E5")
    t = t_axis(6 * tau)
    y = np.sin(2 * np.pi * f * t) * np.exp(-t / tau) + 0.5 * np.sin(2 * np.pi * f * 5.95 * t) * np.exp(-t / 0.02)
    y += 0.15 * np.sin(2 * np.pi * f * 2 * t) * np.exp(-t / (tau / 3))
    y *= np.clip(t / 0.001, 0, 1)
    return reverb(y, room, 0.5, 0.15) if room else y


def main():
    for nm, fn in [("a-sunrise", boot_a), ("b-andes", boot_b), ("c-glass-pad", boot_c), ("d-chip-fanfare", boot_d)]:
        save("boot", nm, fn(), fn.__doc__)
    for nm, fn in [("a-sunset", close_a), ("b-andes-falling", close_b), ("c-glass-closing", close_c), ("d-chip-power-down", close_d)]:
        save("close", nm, fn(), fn.__doc__)
    for nm, fn in [("a-fm-bonk", dialog_a), ("b-wood", dialog_b), ("c-glass", dialog_c), ("d-chip", dialog_d)]:
        for kind in ("alert", "error"):
            save("dialog", f"{nm}-{kind}", fn(kind), fn.__doc__)
    for nm, fn in [("a-fm-ding", bell_a), ("b-kalimba", bell_b), ("c-desk-bell", bell_c), ("d-chip-beep", bell_d)]:
        save("bell", nm, fn(), fn.__doc__)

    save("round2", "boot-glass-pad-quiet-bell", boot_c_quiet_bell(), boot_c_quiet_bell.__doc__)
    save("round2", "close-sunset-mellow-1", close_a_mellow(1), "Sunset, mellower: softer attack, less bite, slower fall.")
    save("round2", "close-sunset-mellow-2", close_a_mellow(2), "Sunset, mellower still: an octave lower and darker.")
    save("round2", "dialog-wood-alert", dialog_b("alert"), "Wood knock, alert: unchanged from round one.")
    save("round2", "dialog-wood-error", dialog_b("error"), "Wood knock, error: unchanged from round one.")
    save("round2", "bell-kalimba-short-1", bell_b_short(0.10, 0.08), "Kalimba, shorter: about a third of a second.")
    save("round2", "bell-kalimba-short-2", bell_b_short(0.055, 0), "Kalimba, shortest: a dry quarter-second pluck.")

    # The final set, chosen by the owner on 2026-11-16.  The 8-bit copies in
    # docs/sounds/ are what the firmware will ship.
    repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs", "sounds")
    for nm, x, blurb in [
        ("boot", boot_c_quiet_bell(), "Boot chime: glass pad with a quiet bell."),
        ("close", close_a_mellow(2), "Session close: the mellow, low sunset."),
        ("alert", dialog_b("alert"), "Alert dialog: rising wood knock."),
        ("error", dialog_b("error"), "Error dialog: low double wood knock."),
        ("bell", bell_b_short(0.10, 0.08), "Bell: short kalimba pluck."),
    ]:
        save("final", nm, x, blurb)
        write_wav(os.path.join(repo, nm + ".wav"), finish(x), bits=8)

    with open(f"{OUT}/manifest.tsv", "w") as f:
        f.write("category\tname\tseconds\tbytes_8bit_22k\tdescription\n")
        for c, n, s, b in manifest:
            f.write(f"{c}\t{n}\t{s:.2f}\t{int(s*SR)}\t{b}\n")


if __name__ == "__main__":
    main()
