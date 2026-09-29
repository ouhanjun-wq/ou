"""Synthesise the 30 s soundtrack for film.html (no samples, everything from maths).

120 BPM, one bar = 2 s, so every cut in the film (4, 8, 12, 16, 20, 24, 26 s) lands on a downbeat.
Harmony: D minor  i - VI - III - VII ...  then  V (A) -> I (D major) at the final reveal.

    python3 tools/score.py  ->  build/score.wav  (48 kHz, 24-bit stereo)
"""
import os
import wave

import numpy as np
from scipy.signal import butter, fftconvolve, sosfilt

SR = 48000
DUR = 30.0
N = int(SR * DUR)
BEAT = 0.5
rng = np.random.default_rng(2026)
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "build")


def hz(m):
    return 440.0 * 2 ** ((np.asarray(m, dtype=float) - 69) / 12)


def tt(n):
    return np.arange(n) / SR


def sos(kind, f, order=2):
    return butter(order, f, btype=kind, fs=SR, output="sos")


def lp(x, f, order=2):
    return sosfilt(sos("low", f, order), x)


def hp(x, f, order=2):
    return sosfilt(sos("high", f, order), x)


def bp(x, lo, hi, order=2):
    return sosfilt(sos("band", [lo, hi], order), x)


def sweep(x, cut, kind="low", fmin=80, fmax=16000, k=14):
    """Time-varying filter: run a bank of static filters and interpolate per sample in log-frequency."""
    bank = np.geomspace(fmin, fmax, k)
    ys = np.stack([sosfilt(sos(kind, f), x) for f in bank])
    pos = (np.log(np.clip(cut, fmin, fmax)) - np.log(fmin)) / (np.log(fmax) - np.log(fmin)) * (k - 1)
    i0 = np.clip(np.floor(pos).astype(int), 0, k - 2)
    fr = pos - i0
    idx = np.arange(len(x))
    return ys[i0, idx] * (1 - fr) + ys[i0 + 1, idx] * fr


def saw(freq, n, ph=0.0):
    """PolyBLEP band-limited sawtooth; freq may be a scalar or per-sample array."""
    dt = np.broadcast_to(np.asarray(freq, dtype=float) / SR, (n,))
    p = (ph + np.cumsum(dt)) % 1.0
    y = 2 * p - 1
    m = p < dt
    x = p[m] / dt[m]
    y[m] -= x + x - x * x - 1
    m = p > 1 - dt
    x = (p[m] - 1) / dt[m]
    y[m] -= x * x + x + x + 1
    return y


def env(n, a=0.005, r=0.2, hold=None):
    """Attack / hold / release envelope (linear attack, cosine release)."""
    e = np.ones(n)
    na = max(1, int(a * SR))
    e[:na] = np.linspace(0, 1, na)
    if hold is not None:
        nh = int(hold * SR)
        nr = max(1, int(r * SR))
        if nh + nr < n:
            e[nh + nr:] = 0
        seg = e[nh:nh + nr]
        e[nh:nh + nr] = seg * 0.5 * (1 + np.cos(np.linspace(0, np.pi, len(seg))))
    return e


class Bus:
    def __init__(self):
        self.x = np.zeros((2, N))

    def add(self, sig, at, pan=0.0, gain=1.0):
        """Mono or stereo signal starting at time `at` (s); equal-power pan in [-1, 1]."""
        i = int(round(at * SR))
        if sig.ndim == 1:
            a = (pan + 1) * np.pi / 4
            sig = np.stack([sig * np.cos(a), sig * np.sin(a)])
        j0, j1 = max(0, i), min(N, i + sig.shape[1])
        if j1 > j0:
            self.x[:, j0:j1] += gain * sig[:, j0 - i:j1 - i]


dry, verb, drums = Bus(), Bus(), Bus()

# ---------------------------------------------------------------- harmony
PADS = [
    (0.0, 4.0, [38, 50, 57, 64]),
    (4.0, 6.0, [50, 57, 62, 64, 65]),
    (6.0, 8.0, [46, 53, 58, 62, 65]),
    (8.0, 10.0, [53, 60, 65, 67, 69]),
    (10.0, 12.0, [48, 55, 60, 62, 64]),
    (12.0, 14.0, [50, 57, 62, 64, 65]),
    (14.0, 16.0, [46, 53, 58, 62, 65]),
    (16.0, 18.0, [53, 60, 65, 67, 69]),
    (18.0, 20.0, [48, 55, 60, 64, 67]),
    (20.0, 22.0, [46, 53, 58, 62, 65, 69]),
    (22.0, 24.0, [48, 55, 60, 64, 67]),
    (24.0, 25.0, [45, 52, 57, 62, 64]),
    (25.0, 25.92, [45, 52, 57, 61, 64]),
    (26.0, 30.0, [38, 50, 57, 62, 66, 69, 76]),
]
ROOTS = [(4, 6, 38), (6, 8, 34), (8, 10, 41), (10, 12, 36), (12, 14, 38), (14, 16, 34), (16, 18, 41), (18, 20, 36),
         (20, 22, 34), (22, 24, 36), (24, 25.92, 33), (26, 30, 38)]


def chord_at(t):
    for a, b, notes in PADS:
        if a <= t < b:
            return notes
    return PADS[-1][2]


# ---------------------------------------------------------------- sidechain curve (kick ducking)
KICKS = [4 + i * BEAT for i in range(int((25.0 - 4) / BEAT))]
duck = np.ones(N)
T = tt(N)
for k in KICKS:
    i = int(k * SR)
    d = T[i:] - k
    duck[i:] = np.minimum(duck[i:], 1 - 0.62 * np.exp(-d * 9))
duck = lp(duck, 60)

# ---------------------------------------------------------------- supersaw pads
cut = np.interp(T, [0, 3.9, 4.0, 8, 16, 20, 24, 25.9, 26, 30], [350, 1400, 2600, 2200, 3000, 3200, 2600, 9000, 5200, 2400])
pad = np.zeros((2, N))
for a, b, notes in PADS:
    atk = 1.6 if a == 0 else (0.02 if a in (4.0, 26.0) else 0.18)
    rel = 2.5 if a == 26 else (0.08 if b == 25.92 else 0.5)
    n = int((b - a + rel) * SR)
    e = env(n, atk, rel, hold=b - a)
    for m in notes:
        for ch in (0, 1):
            voice = np.zeros(n)
            for d in (-13, -6, 0, 6, 13) if ch == 0 else (-11, -4, 2, 8, 15):
                voice += saw(hz(m) * 2 ** (d / 1200), n, rng.random())
            i = int(a * SR)
            j = min(N, i + n)
            pad[ch, i:j] += voice[:j - i] * e[:j - i] / (5 * len(notes) ** 0.6)
pad = np.stack([sweep(pad[c], cut) for c in (0, 1)])
pad *= duck
dry.add(pad, 0, gain=0.19)
verb.add(pad, 0, gain=0.12)

# ---------------------------------------------------------------- bass
bass = np.zeros(N)
for a, b, m in ROOTS:
    n = int((b - a + 0.05) * SR)
    x = tt(n)
    f = hz(m)
    s = np.sin(2 * np.pi * f * x) + 0.35 * lp(saw(f, n), 420)
    s *= env(n, 0.01, 0.05 if b == 25.92 else 0.3, hold=b - a)
    i = int(a * SR)
    j = min(N, i + n)
    bass[i:j] += s[:j - i]
bass *= duck ** 1.4
bass = np.tanh(bass * 1.6) / 1.6
dry.add(bass, 0, gain=0.34)


# ---------------------------------------------------------------- instruments
def pluck(m, dur=0.45, bright=1.0, decay=1.0):
    f = float(hz(m))
    n = int(dur * SR)
    x = tt(n)
    y = np.zeros(n)
    for h in range(1, 40):
        if h * f > 14000:
            break
        y += np.sin(2 * np.pi * h * f * x + rng.random() * 6.28) * (1 / h ** (1.25 / bright)) * np.exp(-x * (3 + 1.8 * h * (f / 440) ** 0.5) * decay)
    return y * env(n, 0.002, 0.05, hold=dur - 0.06)


def bell(m, dur=1.6, idx=3.0, ratio=3.5):
    f = float(hz(m))
    n = int(dur * SR)
    x = tt(n)
    I = idx * np.exp(-x * 5)
    y = np.sin(2 * np.pi * f * x + I * np.sin(2 * np.pi * f * ratio * x)) * np.exp(-x * 2.6)
    y += 0.3 * np.sin(2 * np.pi * f * 2.001 * x) * np.exp(-x * 4)
    return y * env(n, 0.001, 0.2, hold=dur - 0.2)


def piano(m, dur=2.0, vel=1.0):
    f = float(hz(m))
    n = int(dur * SR)
    x = tt(n)
    y = np.zeros(n)
    B = 0.0004
    for h in range(1, 18):
        fh = h * f * np.sqrt(1 + B * h * h)
        if fh > 12000:
            break
        y += np.sin(2 * np.pi * fh * x) * (vel / h ** 1.4) * np.exp(-x * (0.9 + 0.5 * h))
    ham = hp(rng.standard_normal(n), 1500) * np.exp(-x * 90) * 0.05
    return (y + ham) * env(n, 0.003, 0.4, hold=dur - 0.4)


def kick(n=int(0.5 * SR), punch=1.0):
    x = tt(n)
    f = 46 + 120 * np.exp(-x * 30)
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = np.sin(ph) * np.exp(-x * 6.5)
    y += hp(rng.standard_normal(n), 3000) * np.exp(-x * 400) * 0.35 * punch
    return np.tanh(y * 1.8) / 1.2


def snare(n=int(0.35 * SR), tone=1.0):
    x = tt(n)
    nz = bp(rng.standard_normal(n), 1200, 7000) * np.exp(-x * 17)
    tn = np.sin(2 * np.pi * 190 * x) * np.exp(-x * 30) * 0.6 * tone
    return nz + tn


def clap(n=int(0.3 * SR)):
    x = tt(n)
    nz = bp(rng.standard_normal(n), 900, 5500)
    e = np.zeros(n)
    for o in (0.0, 0.011, 0.022):
        i = int(o * SR)
        e[i:] = np.maximum(e[i:], np.exp(-(x[: n - i]) * 90))
    e = np.maximum(e, 0.6 * np.exp(-np.maximum(0, x - 0.03) * 16) * (x > 0.03))
    return nz * e


def hat(n=int(0.08 * SR), open_=False):
    x = tt(n)
    y = hp(rng.standard_normal(n), 7500, 4)
    return y * np.exp(-x * (11 if open_ else 55))


def crash(dur=2.6, bright=5000):
    n = int(dur * SR)
    x = tt(n)
    y = hp(rng.standard_normal(n), bright, 2)
    ring = sum(np.sin(2 * np.pi * f * x + rng.random() * 6) for f in (3120, 4235, 5560, 6970, 8130)) * 0.08
    return (y + hp(ring, 2000)) * np.exp(-x * 1.9) * env(n, 0.001, 0.3, hold=dur - 0.3)


def boom(dur=3.0):
    n = int(dur * SR)
    x = tt(n)
    f = 28 + 62 * np.exp(-x * 2.4)
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-x * 1.25)
    y += lp(rng.standard_normal(n), 180) * np.exp(-x * 2.2) * 0.9
    return np.tanh(y * 2.2) / 1.4


def whoosh(dur=0.6, f0=300, f1=9000, rev=False):
    n = int(dur * SR)
    x = tt(n)
    p = x / dur
    c = f0 * (f1 / f0) ** p
    y = sweep(rng.standard_normal(n), c, kind="low", fmin=100, fmax=14000, k=10)
    e = np.sin(np.pi * p) ** 2
    return y * e


def riser(dur):
    n = int(dur * SR)
    x = tt(n)
    p = x / dur
    nz = sweep(rng.standard_normal(n), 250 * (60 ** p), kind="low", fmin=100, fmax=16000, k=12)
    tone = sum(saw(hz(57 + o) * 2 ** (p * 1.0), n, rng.random()) for o in (0, 7, 12, 19)) / 4
    tone = sweep(tone, 400 * (25 ** p), fmin=100, fmax=12000, k=10)
    return (0.8 * nz + 0.5 * tone) * (p ** 2.2)


# ---------------------------------------------------------------- 0-4 s  intro
for at, m in [(0.45, 62), (1.2, 69), (1.75, 76), (2.35, 77), (2.95, 74)]:
    s = piano(m, 3.2, 0.8)
    dry.add(s, at, pan=(m - 69) / 30, gain=0.16)
    verb.add(s, at, pan=(m - 69) / 30, gain=0.22)
for i in range(22):
    at = 0.2 + rng.random() * 3.4
    m = int(rng.choice([86, 89, 91, 93, 96, 98]))
    s = bell(m, 1.4, idx=1.5, ratio=2.01)
    verb.add(s, at, pan=rng.uniform(-0.9, 0.9), gain=0.03 + 0.03 * at / 4)
r = riser(2.0)
dry.add(r, 1.95, gain=0.22)
verb.add(r, 1.95, gain=0.12)
rc = crash(2.0, 3500)[::-1]
verb.add(rc, 4.0 - len(rc) / SR, pan=0, gain=0.25)

# ---------------------------------------------------------------- impacts: 4 s and 26 s
for at, big in [(4.0, 1.0), (26.0, 1.25)]:
    b = boom(3.4)
    dry.add(b, at, gain=0.62 * big)
    drums.add(kick(int(0.7 * SR), 1.3), at, gain=0.85 * big)
    c = crash(3.4, 3000)
    for pan in (-0.6, 0.6):
        dry.add(c, at + (0.004 if pan > 0 else 0), pan=pan, gain=0.12 * big)
        verb.add(c, at, pan=pan, gain=0.2 * big)

# ---------------------------------------------------------------- drums 4-26
for k in KICKS:
    g = 0.55 if k < 8 else 0.8
    drums.add(kick(), k, gain=g)
for bar in range(8, 24, 2):
    for off in (0.5, 1.5):
        at = bar + off
        drums.add(clap(), at, pan=0.05, gain=0.33)
        drums.add(snare(tone=0.6), at, gain=0.16)
        verb.add(clap(), at, gain=0.08)
for i in range(int(8 / 0.25), int(24 / 0.25)):
    at = i * 0.25
    off = (i % 2) == 1
    if 20 <= at < 24 and not off:
        continue
    g = 0.11 if off else 0.045
    if 16 <= at < 20:
        g *= 1.2
    drums.add(hat(open_=(i % 8 == 7)), at, pan=0.25 if off else -0.2, gain=g)
# build: snare roll that accelerates into the drop
at, step = 24.0, 0.25
while at < 25.9:
    p = (at - 24) / 1.9
    drums.add(snare(int(0.18 * SR)), at, pan=0.1 * np.sin(at * 13), gain=0.08 + 0.3 * p ** 1.5)
    at += step
    if at >= 24.5:
        step = 0.125
    if at >= 25.25:
        step = 0.0625
    if at >= 25.62:
        step = 0.03125

# cut whooshes (visual light sweep travels left -> right)
for c in (8, 12, 16, 20, 24):
    w = whoosh(0.55, 400, 9000)
    n = len(w)
    panL = np.linspace(-0.9, 0.9, n)
    a = (panL + 1) * np.pi / 4
    dry.add(np.stack([w * np.cos(a), w * np.sin(a)]), c - 0.36, gain=0.09)
    verb.add(crash(1.8, 6000), c, gain=0.07)

# ---------------------------------------------------------------- 4-8 s  reason: neural pings
pent = [74, 77, 79, 81, 84, 86, 89, 91, 93]
for i in range(16):
    at = 4.5 + i * 0.25
    if at >= 8:
        break
    m = pent[int(rng.integers(0, len(pent)))]
    s = bell(m, 0.8, idx=2.2, ratio=3.0)
    g = 0.07 if i % 2 == 0 else 0.035
    dry.add(s, at, pan=(-0.7 if i % 2 else 0.7), gain=g)
    verb.add(s, at, gain=g * 0.9)

# ---------------------------------------------------------------- arpeggios
PATTERN = [0, 1, 2, 3, 4, 3, 2, 1, 0, 2, 4, 5, 4, 2, 1, 3]


def arp_notes(chord):
    base = sorted(set(chord))
    tones = [m for m in base if m >= 57] or [m + 12 for m in base]
    tones = sorted(tones)
    ext = tones + [m + 12 for m in tones]
    return ext


arp = np.zeros(N)
for i in range(int(8 / 0.125), int(25.9 / 0.125)):
    at = i * 0.125
    if 12 <= at < 16 or 20 <= at < 24:
        continue
    ext = arp_notes(chord_at(at))
    m = ext[PATTERN[i % 16] % len(ext)] + 12
    s = pluck(m, 0.32, bright=1.4 if at >= 24 else 1.0)
    j = int(at * SR)
    arp[j:j + len(s)] += s[: max(0, min(len(s), N - j))]
arp_cut = np.interp(T, [8, 12, 16, 20, 24, 25.9], [1800, 2600, 2400, 3400, 1600, 11000])
arp = sweep(arp, arp_cut, fmin=200, fmax=14000, k=12) * (0.55 + 0.45 * duck)
arpL = arp
arpR = np.concatenate([np.zeros(int(0.1875 * SR)), arp[: N - int(0.1875 * SR)]])  # dotted-8th ping-pong echo
dry.add(np.stack([arpL, 0.55 * arpR]), 0, gain=0.2)
verb.add(np.stack([arpL, arpR]), 0, gain=0.07)

# math: glassy FM bells in 8ths
for i in range(int(12 / 0.25), int(16 / 0.25)):
    at = i * 0.25
    ext = arp_notes(chord_at(at))
    m = ext[[0, 2, 4, 5, 3, 1, 4, 2][i % 8] % len(ext)] + 12
    s = bell(m, 1.1, idx=2.6, ratio=3.5)
    dry.add(s, at, pan=np.sin(i * 0.9) * 0.6, gain=0.07)
    verb.add(s, at, gain=0.06)

# code: "tests passed" / "deployed" chimes
for at, ms in [(11.0, [81, 88]), (11.48, [86, 93])]:
    for k, m in enumerate(ms):
        s = bell(m, 1.5, idx=1.2, ratio=2.0)
        dry.add(s, at + k * 0.03, pan=0.3, gain=0.08)
        verb.add(s, at, gain=0.08)

# language: a bright tick for each new language
for at, m in zip([17.0, 17.5, 18.0, 18.5, 19.0, 19.5], [81, 84, 79, 84, 88, 91]):
    s = bell(m, 1.0, idx=1.0, ratio=2.0)
    dry.add(s, at, pan=np.sin(at * 3) * 0.6, gain=0.06)
    verb.add(s, at, gain=0.06)

# ---------------------------------------------------------------- choir ("aah" formants)
def choir(notes, dur, atk=0.4, rel=1.0):
    n = int((dur + rel) * SR)
    x = tt(n)
    src = np.zeros(n)
    for m in notes:
        vib = 1 + 0.004 * np.sin(2 * np.pi * 5.2 * x + rng.random() * 6)
        for d in (-8, 0, 8):
            src += saw(hz(m + 12) * 2 ** (d / 1200) * vib, n, rng.random())
    src += 0.15 * rng.standard_normal(n)
    y = 1.0 * bp(src, 650, 850) + 0.6 * bp(src, 1000, 1200) + 0.25 * bp(src, 2300, 2600)
    return y * env(n, atk, rel, hold=dur) / (3 * len(notes))


for a, b, notes in PADS:
    if 16 <= a < 25.9 or a == 26.0:
        c = choir(notes[1:], b - a, atk=0.3 if a != 26 else 0.05, rel=0.6 if a != 26 else 2.0)
        verb.add(c, a, gain=0.5 if a != 26 else 0.7)
        dry.add(c, a, gain=0.2)

# ---------------------------------------------------------------- 20-24 s create: melody
for at, m, d in [(20.0, 69, 0.5), (20.5, 74, 0.5), (21.0, 77, 1.0), (22.0, 76, 0.5), (22.5, 72, 0.5), (23.0, 79, 0.5), (23.5, 81, 0.5)]:
    s = piano(m, 2.2, 1.0)
    dry.add(s, at, pan=0.1, gain=0.2)
    verb.add(s, at, gain=0.2)
    s2 = bell(m + 12, 1.2, idx=0.8, ratio=2.0)
    verb.add(s2, at, gain=0.05)

# ---------------------------------------------------------------- 24-26 s build
r = riser(1.9)
dry.add(r, 24.0, gain=0.3)
verb.add(r, 24.0, gain=0.12)
rc = crash(1.5, 3000)[::-1]
dry.add(rc, 25.92 - len(rc) / SR, gain=0.14)

# ---------------------------------------------------------------- 26-30 s finale shimmer
for at, ms in [(27.45, [86, 90, 93, 98]), (28.35, [81, 86, 90])]:
    for k, m in enumerate(ms):
        s = bell(m, 2.4, idx=1.4, ratio=2.0)
        dry.add(s, at + k * 0.07, pan=-0.5 + k * 0.33, gain=0.06)
        verb.add(s, at + k * 0.07, gain=0.1)
s = piano(50, 3.5, 1.2) + piano(62, 3.5, 0.9) + piano(69, 3.5, 0.7)
dry.add(s, 26.0, gain=0.16)
verb.add(s, 26.0, gain=0.2)

# ---------------------------------------------------------------- reverb
def ir(sec=3.2, rt=2.8):
    n = int(sec * SR)
    x = tt(n)
    out = []
    for _ in (0, 1):
        w = rng.standard_normal(n) * np.exp(-x * 6.9 / rt)
        dark = lp(w, 2500)
        mix = np.exp(-x * 1.6)
        y = w * mix + dark * (1 - mix)
        y[: int(0.018 * SR)] = 0
        out.append(y / np.sqrt(np.sum(y ** 2)))
    return out


irL, irR = ir()
wet = np.stack([fftconvolve(verb.x[0], irL)[:N], fftconvolve(verb.x[1], irR)[:N]])
wet = hp(wet, 120)

# drums get a light saturation and their own short room
dr = np.tanh(drums.x * 1.2) / 1.2
irs = ir(0.8, 0.45)
room = np.stack([fftconvolve(dr[0], irs[0])[:N], fftconvolve(dr[1], irs[1])[:N]])

mix = dry.x + 0.9 * wet + dr + 0.12 * room

# the breath before the drop, then the final fade
gate = np.ones(N)
g0, g1 = int(25.92 * SR), int(26.0 * SR)
gate[g0:g1] = 0
fade = int(0.004 * SR)
gate[g0 - fade:g0] = np.linspace(1, 0, fade)
end = np.interp(T, [0, 28.6, 29.95, 30], [1, 1, 0, 0])
mix *= gate * (0.5 * (1 - np.cos(np.pi * end)))
mix[:, : int(0.01 * SR)] *= np.linspace(0, 1, int(0.01 * SR))

# master: gentle glue + normalise
mix = hp(mix, 25)
mix = np.tanh(mix * 1.1) / 1.1
mix *= 0.89 / np.max(np.abs(mix))

os.makedirs(OUT, exist_ok=True)
pcm = (np.clip(mix.T, -1, 1) * (2 ** 23 - 1)).astype(np.int32)
b24 = np.ascontiguousarray(pcm, dtype="<i4").view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
with wave.open(os.path.join(OUT, "score.wav"), "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(3)
    w.setframerate(SR)
    w.writeframes(b24)
print("score ->", os.path.join(OUT, "score.wav"))

if os.environ.get("SCORE_DEBUG"):
    np.savez(os.path.join(OUT, "stems.npz"), pad=pad, bass=bass, arp=arp, wet=wet, dr=dr, dry=dry.x)
