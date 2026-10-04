# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic synthetic "music" for the glob2music tests (no files, no network).

``make_trio`` renders a through-composed, seamlessly looping trio on one shared
timeline: a chord pad changes every bar, a plucked melody picks fresh notes every
bar (so nothing repeats exactly), and the moods add density and noise-burst drums on
the same 8th-note grid. It is crude, but it has what the checks look at -- onsets on
a grid, harmony that agrees between moods, a percussive ladder and a clean seam --
so a test can break exactly one property and assert that exactly that check reacts.
"""
import numpy as np

from glob2music.loop import fold_tail
from glob2music.spec import SAMPLE_RATE

SR = SAMPLE_RATE
BPM = 120.0
BEAT = 60.0 / BPM
SCALE = [0, 2, 3, 5, 7, 8, 10]          # natural minor
CHORDS = [(0, 3, 7), (5, 8, 12), (7, 10, 14), (3, 7, 10)]


def _hz(semitone, base=220.0):
    return base * 2 ** (semitone / 12)


def _pluck(freq, seconds, bright=1.0):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    env = np.exp(-t * 4.0) * np.minimum(1.0, t / 0.004)
    tone = sum((0.6 ** k) * bright ** k * np.sin(2 * np.pi * freq * (k + 1) * t + k)
               for k in range(4))
    return tone * env


def _pad(freqs, seconds):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    env = np.minimum(1.0, t / 0.3) * np.minimum(1.0, (seconds - t) / 0.3)
    return env * sum(np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t) for f in freqs) / len(freqs)


def _drum(rng, seconds=0.12):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    body = np.sin(2 * np.pi * 70 * t * (1 - t)) * np.exp(-t * 30)
    noise = rng.standard_normal(n) * np.exp(-t * 60) * 0.5
    attack = np.minimum(1.0, t / 0.002)          # 2 ms attack: no click at the onset
    return (body + noise) * attack


def render_mood(mood, seconds=60.0, seed=1):
    """One mood of the shared timeline, as float64 (frames, 2), seamless."""
    rng = np.random.default_rng(seed)                 # same seed: same notes in every mood
    drum_rng = np.random.default_rng(seed + 1000)
    n = int(seconds * SR)
    tail = int(3 * SR)
    y = np.zeros(n + tail)

    def add(sig, at_s, gain):
        i = int(round(at_s * SR))
        j = min(len(y), i + len(sig))
        y[i:j] += gain * sig[:j - i]

    bars = int(seconds / (4 * BEAT))
    for bar in range(bars):
        # Draw every random choice for the bar up front, identically for all moods,
        # so the moods stay one timeline while each bar differs from the others.
        transpose = int(rng.integers(-2, 3))
        arc = 0.55 + 0.45 * rng.random()                  # slow dynamic arc per bar
        melody = rng.choice(SCALE, size=8) + 12 + transpose
        velocity = 0.3 + 0.7 * rng.random(8)
        rest = rng.random(8) < 0.3
        accents = 0.4 + 0.6 * rng.random(8)
        root = CHORDS[bar % len(CHORDS)]
        chord = [_hz(s + transpose) for s in root]
        add(_pad(chord, 4 * BEAT), bar * 4 * BEAT, arc * (0.25 if mood == 'calm' else 0.2))
        density = 2 if mood == 'calm' else 1
        for k in range(8):
            if k % density == 0 and not rest[k]:
                add(_pluck(_hz(melody[k]), 0.6), (bar * 8 + k) * BEAT / 2, 0.18 * velocity[k] * arc)
        if mood != 'calm':
            add(_pluck(_hz(root[0] + transpose - 12), 1.5, 0.5), bar * 4 * BEAT, 0.3 * arc)
        if mood == 'combat':
            for k in range(8):
                if not rest[(k + 3) % 8]:
                    add(_drum(drum_rng), (bar * 8 + k) * BEAT / 2, 0.5 * accents[k] * arc)
    loop = fold_tail(y, n)
    return np.stack([loop, loop * 0.98], axis=1)


def make_trio(seconds=60.0, seed=1, master=True):
    """A ``Trio`` (mastered to the pipeline's targets unless ``master`` is False)."""
    from glob2music.audio import Trio
    from glob2music.master import finish
    trio = Trio(**{m: render_mood(m, seconds, seed) for m in ('calm', 'building', 'combat')})
    return finish(trio) if master else trio
