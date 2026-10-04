# SPDX-License-Identifier: GPL-3.0-or-later
"""Small NumPy instruments for synth sets: a modal kalimba, droplet "bloops" and an
organic percussion kit, rendered as a backend for ``score.pipeline.build_trio``.

Pipeline role: symbolic-synth sets play their melodic parts on Surge XT
(``backends/surge.py``) and these parts here. ``DspBackend.render`` takes the same
``PerformedPart`` objects and returns stems with the same pre-roll/tail layout as the
Surge backend, so a set can combine both (see ``sets/glass-garden/recipe.py``).

Every voice is a pure function of ``(pitch, velocity, rng)``: one ``numpy`` generator
per part varies each hit slightly (pitch, decay, noise grain), so repeated notes never
sound machine-identical, and these voices render bit-identically on every build. (That
holds for this module only; see ``backends/surge.py`` for the Surge parts.) Envelopes
and filters are the shared drum-synthesis helpers of ``adapt/percussion.py``.

Voices, chosen to sound wooden, skin-like or watery rather than electronic:

=============  ================================================================
kalimba        modal tine: fundamental, weak octave, inharmonic 5.93x and 9.7x
               modes that die fast, a damped thumb knock; velocity = brightness
bloop          water-droplet sine that springs up into its pitch, then sags
frame_drum     low skin drum tuned to A1 with a falling pitch and a skin noise
taiko          deep drum on D1 with a long boom
tom            pitched membrane (modes 1, 1.59, 2.14) at the note's pitch
snap           woody finger-snap: tuned click plus a 2-3 burst noise flam
shaker         two-stroke band-passed noise (alternating strokes differ)
log_drum       wooden slit drum: three bar-like modes at the note's pitch
=============  ================================================================

The noise voices (snap, shaker) are deliberately band-limited below 7.5 kHz, so they
add no background hiss.
"""
import numpy as np

from ..adapt.percussion import band as _band, env as _env, low as _low
from ..spec import SAMPLE_RATE

SR = SAMPLE_RATE
PREROLL_S = 0.5          # same layout as backends/surge.py
TAIL_S = 3.0


def mtof(pitch):
    return 440.0 * 2 ** ((pitch - 69) / 12.0)


def kalimba(pitch, vel, rng):
    f = mtof(pitch) * (1 + rng.normal(0, 0.0007))
    v = vel / 127.0
    n = int(2.6 * SR)
    t = np.arange(n) / SR
    d0 = 1.5 * (440 / f) ** 0.35                      # low tines ring longer
    y = np.sin(2 * np.pi * f * t) * _env(n, 0.002, d0)
    y += 0.10 * np.sin(2 * np.pi * 2.01 * f * t + 0.3) * _env(n, 0.002, d0 * 0.45)
    bright = 0.15 + 0.35 * v
    y += bright * 0.55 * np.sin(2 * np.pi * 5.93 * f * t) * _env(n, 0.001, 0.09)
    y += bright * 0.18 * np.sin(2 * np.pi * 9.7 * f * t + 1.0) * _env(n, 0.001, 0.035)
    k = int(0.012 * SR)
    knock = _band(rng.normal(0, 1, k) * np.exp(-np.arange(k) / (0.003 * SR)), 700, 2600)
    y[:k] += knock * 0.25 * v
    return y * (0.35 + 0.65 * v) ** 1.3


def bloop(pitch, vel, rng):
    f = mtof(pitch)
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    glide = -7 * np.exp(-t / 0.018) - 0.4 * (t / 0.45)          # semitones
    ph = 2 * np.pi * np.cumsum(f * 2 ** (glide / 12.0)) / SR
    return (np.sin(ph) + 0.12 * np.sin(2 * ph)) * _env(n, 0.003, 0.09 + 0.05 * rng.random()) * vel / 127.0 * 0.8


def _membrane(f0, bend, bend_s, n, decays, ratios, gains, attack=0.001):
    t = np.arange(n) / SR
    ph = 2 * np.pi * np.cumsum(f0 * (1 + bend * np.exp(-t / bend_s))) / SR
    return sum(g * np.sin(r * ph) * _env(n, attack, d) for r, g, d in zip(ratios, gains, decays))


def frame_drum(pitch, vel, rng):
    n = int(0.7 * SR)
    y = _membrane(55.0 * (1 + rng.normal(0, 0.01)), 1.1, 0.035, n, (0.26, 0.09), (1, 1.59), (1, 0.25))
    y += _low(rng.normal(0, 1, n), 1400) * _env(n, 0.0005, 0.025) * 0.5
    return y * (vel / 127.0) ** 1.2


def taiko(pitch, vel, rng):
    n = int(2.0 * SR)
    y = _membrane(36.7, 0.6, 0.05, n, (0.7, 0.25), (1, 1.5), (1, 0.3), attack=0.002)
    y += _low(rng.normal(0, 1, n), 900) * _env(n, 0.001, 0.12) * 0.6
    return y * vel / 127.0 * 0.9


def tom(pitch, vel, rng):
    n = int(0.9 * SR)
    y = _membrane(mtof(pitch) * (1 + rng.normal(0, 0.006)), 0.12, 0.06, n, (0.32, 0.14, 0.08),
                  (1, 1.593, 2.135), (1, 0.35, 0.18))
    y += _band(rng.normal(0, 1, n), 300, 2500) * _env(n, 0.0005, 0.02) * 0.4
    return y * (vel / 127.0) ** 1.2 * 0.8


def snap(pitch, vel, rng):
    n = int(0.25 * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    offsets = [0, 0.007 + 0.003 * rng.random(), 0.016 + 0.004 * rng.random()][:2 + (rng.random() < 0.5)]
    for k, dt in enumerate(offsets):
        i = int(dt * SR)
        m = n - i
        burst = _band(rng.normal(0, 1, m), 900, 3000) * _env(m, 0.0003, 0.012 if k < 2 else 0.05)
        y[i:] += burst * (1.0 if k == 0 else 0.6)
    y += 0.5 * np.sin(2 * np.pi * 1750 * (1 + rng.normal(0, 0.02)) * t) * _env(n, 0.0002, 0.018)
    y += 0.35 * np.sin(2 * np.pi * 210 * t) * _env(n, 0.0005, 0.03)
    return y * (vel / 127.0) ** 1.1 * 0.7


def shaker(pitch, vel, rng, stroke=0):
    n = int(0.16 * SR)
    band = [3600, 7500] if stroke else [3000, 6500]
    noise = _band(rng.normal(0, 1, n), *band)
    e = _env(n, 0.012 if stroke else 0.006, 0.035 + 0.02 * rng.random())
    return noise * e * (vel / 127.0) ** 1.3 * 0.33


def log_drum(pitch, vel, rng):
    n = int(0.4 * SR)
    t = np.arange(n) / SR
    f = mtof(pitch) * (1 + rng.normal(0, 0.004))
    y = (np.sin(2 * np.pi * f * t) * _env(n, 0.0005, 0.07)
         + 0.4 * np.sin(2 * np.pi * 2.57 * f * t) * _env(n, 0.0005, 0.025)
         + 0.15 * np.sin(2 * np.pi * 4.1 * f * t) * _env(n, 0.0005, 0.012))
    return y * (vel / 127.0) ** 1.2 * 0.6


VOICES = {'kalimba': kalimba, 'bloop': bloop, 'frame_drum': frame_drum, 'taiko': taiko, 'tom': tom,
          'snap': snap, 'shaker': shaker, 'log_drum': log_drum}

#: The ``kit`` voice: one percussion part whose note numbers pick the drum (GM-like
#: numbers), with the pitch each drum sounds at and its pan in the kit.
KIT = {35: ('taiko', 35, 0.0), 36: ('frame_drum', 36, 0.0), 39: ('snap', 39, -0.18),
       41: ('tom', 38, -0.35), 45: ('tom', 45, 0.05), 48: ('tom', 50, 0.35),
       70: ('shaker', 70, 0.38), 76: ('log_drum', 81, 0.25), 77: ('log_drum', 74, -0.25)}


class DspBackend:
    """Renders ``PerformedPart`` objects with ``VOICES`` (or the ``kit``).

    ``instruments`` maps an instrument key to ``(model.Instrument, voice name)``; the
    voice ``'kit'`` plays ``KIT``. Each part draws from one generator seeded with the sum
    of its name's character codes, note by note in onset order: the seeding of the
    approved Glass Garden render. Each note is
    panned within the stem (by pitch for kalimba, at random for bloops, by ``KIT`` plus a
    little jitter for drums). Shakers alternate two strokes by event index. CC11 is not
    applied here; a set that wants a fader ride applies it in its mix.
    """

    preroll_s = PREROLL_S
    tail_s = TAIL_S

    def __init__(self, instruments):
        self.voices = {k: v for k, (_, v) in instruments.items()}
        self.instruments = {k: inst for k, (inst, _) in instruments.items()}

    def render_part(self, part, loop_seconds):
        """One ``PerformedPart`` -> stereo stem ``(frames, 2)``."""
        voice = self.voices[part.instrument]
        rng = np.random.default_rng(sum(map(ord, part.name)))
        n = int(round((PREROLL_S + loop_seconds + TAIL_S) * SR))
        out = np.zeros((n, 2))
        for k, (on, _, pitch, vel) in enumerate(part.events):
            if voice == 'kalimba':
                y = kalimba(pitch, vel, rng)
                spread = 0.45 * np.sin(pitch * 1.7)
            elif voice == 'bloop':
                y = bloop(pitch, vel, rng)
                spread = rng.uniform(-0.65, 0.65)
            else:
                drum, sounding, pan = KIT[pitch] if voice == 'kit' else (voice, pitch, 0.0)
                y = shaker(sounding, vel, rng, k % 2) if drum == 'shaker' else VOICES[drum](sounding, vel, rng)
                spread = pan + rng.normal(0, 0.03)
            i = int(round((on + PREROLL_S) * SR))
            j = min(n, i + len(y))
            if i >= n or j <= 0:
                continue
            th = (spread + 1) * np.pi / 4
            out[max(i, 0):j, 0] += np.cos(th) * y[max(0, -i):j - i]
            out[max(i, 0):j, 1] += np.sin(th) * y[max(0, -i):j - i]
        return out

    def render(self, performed, loop_seconds, work_dir=None):
        """``{mood: [PerformedPart]}`` -> ``{mood: {part name: stem}}`` (no caching:
        these voices render in well under a second per part)."""
        return {mood: {p.name: self.render_part(p, loop_seconds) for p in parts}
                for mood, parts in performed.items()}
