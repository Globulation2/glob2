# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesised hand percussion for layering under adapted music (original DSP, no samples).

Pipeline role: when a source's own drums are too faint to carry a combat mood (the
Curious Critters case), the recipe adds a small kit locked to the source's beat grid
(``adapt.beatgrid``). Everything here is generated from oscillators and noise, so the
added layers are the adapter's own work and add nothing to the licence chain.

Voices (each returns a stereo ``(frames, 2)`` hit at a given velocity and pan):

* ``taiko``    -- big low drum: a pitch-gliding two-mode membrane plus a soft skin
  slap, gently saturated. Weight without a click.
* ``timpani``  -- tuned kettle drum: five inharmonic modes with long decays; tune it
  to the piece's root and fifth.
* ``frame_drum`` -- hand frame drum: open "doum" (low, round) or rim "tek" (short,
  bright).
* ``shaker``   -- band-passed noise grain.

Placement: ``Track`` accumulates hits into a loop-length buffer *circularly* (a hit
near the end rings on into the start, as when the game wraps), and ``Humaniser``
adds the small, seeded timing and velocity variations that keep a pattern from
sounding machine-locked. Patterns themselves are artistic choices and live in the
set's recipe.

Keep it warm: low-pass the kit and give it a room (``adapt.room``) before mixing,
and set its level relative to the music (``stems.level_under``) rather than in
absolute terms.
"""
import numpy as np
import scipy.signal as ss

from ..spec import SAMPLE_RATE

SR = SAMPLE_RATE


def env(n, attack_s, decay_s):
    """An ``n``-sample hit envelope: a linear attack over ``attack_s`` seconds, then
    an exponential decay with time constant ``decay_s`` seconds."""
    t = np.arange(n) / SR
    return np.clip(t / max(attack_s, 1e-4), 0, 1) * np.exp(-t / decay_s)


def band(x, lo, hi):
    """``x`` through a 2nd-order Butterworth band-pass from ``lo`` to ``hi`` Hz (one
    shot, not loop-aware: for short hits that start from silence)."""
    return ss.sosfilt(ss.butter(2, [lo, hi], 'band', fs=SR, output='sos'), x)


def low(x, hz):
    """``x`` through a 2nd-order Butterworth low-pass at ``hz`` (one shot, for hits)."""
    return ss.sosfilt(ss.butter(2, hz, 'low', fs=SR, output='sos'), x)


def pan(mono, position):
    """Constant-power pan of a mono hit; ``position`` -1 (left) .. +1 (right).

    Every voice ends here, so this is also where the hit's fixed-length buffer gets a
    30 ms raised-cosine fade-out: the longest voices are cut while still ringing (the
    taiko at about -18 dB of its peak) and must not end in a step.
    """
    mono = np.array(mono, dtype=np.float64)
    fade = min(len(mono), int(0.03 * SR))
    mono[len(mono) - fade:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, fade)))
    a = (position + 1) * np.pi / 4
    return np.stack([mono * np.cos(a), mono * np.sin(a)], axis=1)


def taiko(rng, velocity=1.0, f0=72.0, position=0.0):
    """Low drum: membrane gliding down to ``f0`` within ~35 ms, plus a 1.4 kHz slap."""
    n = int(1.1 * SR)
    t = np.arange(n) / SR
    phase = 2 * np.pi * np.cumsum(f0 * (1 + 0.9 * np.exp(-t / 0.035))) / SR
    body = np.sin(phase) * env(n, 0.002, 0.42) + 0.35 * np.sin(1.58 * phase) * env(n, 0.002, 0.16)
    slap = low(rng.standard_normal(n), 1400) * env(n, 0.0005, 0.018) * 0.6
    return pan(np.tanh(1.6 * (body + slap)) / np.tanh(1.6) * velocity, position)


def timpani(rng, velocity=1.0, f0=87.31, position=0.0):
    """Kettle drum tuned to ``f0`` (87.31 Hz = F2): inharmonic modes 1, 1.504, 1.742,
    2.0 and 2.245 with a tiny pitch settle, plus a short mallet noise."""
    n = int(2.0 * SR)
    t = np.arange(n) / SR
    x = np.zeros(n)
    for ratio, amp, decay in ((1.0, 1.0, 0.9), (1.504, 0.5, 0.6), (1.742, 0.25, 0.45), (2.0, 0.3, 0.5),
                              (2.245, 0.15, 0.35)):
        x += amp * np.sin(2 * np.pi * f0 * ratio * t * (1 + 0.004 * np.exp(-t / 0.05))) * env(n, 0.003, decay)
    x += band(rng.standard_normal(n), 200, 2500) * env(n, 0.0005, 0.02) * 0.4
    return pan(x * 0.6 * velocity, position)


def frame_drum(rng, velocity=1.0, open_stroke=True, position=0.0):
    """Frame drum: open "doum" (160 Hz membrane, two modes) or rim "tek"."""
    if open_stroke:
        n = int(0.5 * SR)
        t = np.arange(n) / SR
        phase = 2 * np.pi * np.cumsum(160 * (1 + 0.25 * np.exp(-t / 0.02))) / SR
        x = np.sin(phase) * env(n, 0.001, 0.16) + 0.4 * np.sin(1.59 * phase) * env(n, 0.001, 0.07)
        x += band(rng.standard_normal(n), 300, 3000) * env(n, 0.0005, 0.012) * 0.5
    else:
        n = int(0.25 * SR)
        t = np.arange(n) / SR
        x = band(rng.standard_normal(n), 900, 5000) * env(n, 0.0005, 0.03)
        x += 0.5 * np.sin(2 * np.pi * 420 * t) * env(n, 0.0005, 0.04)
    return pan(x * 0.7 * velocity, position)


def shaker(rng, velocity=1.0, position=0.0):
    """A 90 ms grain of 3.5-9 kHz noise with an 8 ms attack."""
    n = int(0.09 * SR)
    x = band(rng.standard_normal(n), 3500, 9000) * env(n, 0.008, 0.025)
    return pan(x * 0.35 * velocity, position)


class Humaniser:
    """Seeded timing and velocity variation for hits on a grid.

    ``time(frame, ms)`` jitters a frame by a Gaussian of ``ms`` standard deviation;
    ``velocity(v, spread)`` scales ``v`` by ``1 + N(0, spread)``, clipped to 0.2-1.4.
    The same ``rng`` drives the voices' noise too, so one seeded generator makes a
    whole kit reproducible.
    """

    def __init__(self, rng):
        self.rng = rng

    def time(self, frame, ms=4.0):
        return int(frame + int(self.rng.normal(0.0, ms) * SR / 1000))

    def velocity(self, v, spread=0.12):
        return float(np.clip(v * (1 + self.rng.normal(0.0, spread)), 0.2, 1.4))


class Track:
    """A loop-length stereo buffer that hits are added into, wrapping at the end."""

    def __init__(self, frames):
        self.audio = np.zeros((int(frames), 2))

    def add(self, hit, frame):
        """Add ``hit`` starting at ``frame`` (taken modulo the loop length)."""
        n = len(self.audio)
        frame %= n
        m = len(hit)
        if m > n:
            raise ValueError('hit longer than the loop')
        end = frame + m
        if end <= n:
            self.audio[frame:end] += hit
        else:
            k = n - frame
            self.audio[frame:] += hit[:k]
            self.audio[:m - k] += hit[k:]
        return self
