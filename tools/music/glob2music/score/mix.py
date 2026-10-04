# SPDX-License-Identifier: GPL-3.0-or-later
"""Mixing rendered stems of a score into one un-mastered loop per mood.

Pipeline role: after a backend has rendered every ``PerformedPart`` to a stem (with
pre-roll and tail) and folded it into one loop (``loop.fold_tail``), ``mix_mood`` sets
levels, filters, pans, adds a shared reverb and runs the bus processing. The result is
the raw mood a recipe puts into its ``Trio``; ``master.finish`` then sets loudness and
true peak, exactly as for every other set. Nothing here normalises to a target or
limits.

Levels. Each player's stem is first referenced to its own level (``measure_levels``):
the 90th percentile of 400 ms RMS windows for sustained parts (a loud passage), the
99.5th percentile of 50 ms windows for struck and plucked parts (their attacks). The
reference is measured once, in the first mood that uses the player (building, combat,
calm), and frozen in the set's ``levels.toml``: the same factor then applies in every
mood, so the velocity differences between moods survive, and re-renders do not drift.
On top of the reference, a part sits at ``ROLE_DB[role]`` relative to the lead, plus
``MOOD_ROLE_DB[mood][role]`` (combat leans on low strings and drums, calm on the
tune), plus the set's own adjustments and the instrument's trim.

Space. Every stem is high-passed by role (``stem_highpass_hz``) and placed with a
balance pan that keeps the sample's own stereo image. One synthetic small-hall impulse
response (``make_ir``: early reflections plus a band-wise decaying, decorrelated noise
tail; deterministic, so it is an own-work input with no licence question) is applied
to the sum of the reverb sends by *circular* convolution, so the hall tail of the last
bar rings into the first, as it does when the game wraps.

Bus. The sum is levelled to -16 LUFS so the compressor threshold means the same in
every mood, then a 32 Hz high-pass, -1.5 dB at 280 Hz (mud), +1.5 dB shelf above
8.5 kHz (air) and a gentle compressor (-13 dB, 1.8:1, 25/260 ms) run circularly.
The filters and compressor are pedalboard's (``requirements-samples.txt``); every
stateful stage runs through ``master.circular`` so the seam stays clean.
"""
import numpy as np
import pyloudnorm as pyln
import scipy.signal as ss

from .. import master
from ..spec import SAMPLE_RATE

#: Level of each role relative to the lead line, dB. Sustained parts are referenced to
#: a loud passage, struck and plucked parts to their attacks (which read hotter), hence
#: the lower numbers for those roles.
ROLE_DB = {'lead': 0.0, 'counter': -3.0, 'pad': -8.0, 'harp': -7.0, 'bass': -4.0, 'pizz': -9.0,
           'perc': -9.0, 'perc_soft': -17.0, 'perc_snare': -13.0, 'sparkle': -12.0, 'lead_dbl': -6.5,
           'ostinato': -8.0, 'low_drum': -4.0, 'bass_sus': -7.0}
#: Per-mood balance on top of ``ROLE_DB``: calm keeps the bass and bells gentle,
#: building lifts the rhythm section, combat leans on low strings, drums and ostinati.
MOOD_ROLE_DB = {
    'calm': {'sparkle': 3.0, 'bass': -3.0},
    'building': {'perc': 2.0, 'bass': 3.5, 'pizz': 1.0},
    'combat': {'bass': 6.0, 'perc': 5.0, 'perc_snare': 2.0, 'ostinato': 3.0, 'pizz': -2.0, 'harp': -3.5,
               'perc_soft': -2.0, 'sparkle': -3.0, 'pad': 1.0, 'counter': -1.0},
}
#: Reference level every part is brought to before the role offsets, dBFS.
REFERENCE_DB = -20.0
#: Wet level of the shared reverb.
REVERB_WET = 0.55
#: Bus pre-level before the compressor, LUFS.
BUS_LEVEL_LUFS = -16.0
#: Order in which moods are searched for a player's level reference.
LEVEL_MOOD_ORDER = ('building', 'combat', 'calm')


def stem_highpass_hz(role, instrument):
    """High-pass corner for a stem: melodic and bright parts lose their rumble at
    120 Hz, basses keep 30 Hz, low drums and sustained basses 26 Hz, the rest 70 Hz.
    An instrument may fix its own corner (timpani 26 Hz, horn 60 Hz)."""
    if instrument.highpass_hz:
        return instrument.highpass_hz
    if role in ('low_drum', 'bass_sus'):
        return 26
    if role in ('lead', 'counter', 'sparkle', 'pizz', 'harp', 'lead_dbl', 'perc_soft'):
        return 120
    return 30 if role == 'bass' else 70


def pan(y, pos):
    """Constant-power balance, ``pos`` -1 (left) .. 1 (right). For wider positions a
    share (``0.35 * |pos|``) of the far channel folds into the near one, so a stereo
    sample moves instead of only losing one side."""
    th = (pos + 1) * np.pi / 4
    gl, gr = np.cos(th) * np.sqrt(2), np.sin(th) * np.sqrt(2)
    out = y.copy()
    m = abs(pos) * 0.35
    if pos > 0:
        out[:, 1] = y[:, 1] + m * y[:, 0]
        out[:, 0] = y[:, 0] * (1 - m)
    elif pos < 0:
        out[:, 0] = y[:, 0] + m * y[:, 1]
        out[:, 1] = y[:, 1] * (1 - m)
    out[:, 0] *= gl
    out[:, 1] *= gr
    return out


def _pedalboard(plugins, pad_s):
    import pedalboard as pb
    board = pb.Pedalboard(plugins(pb))
    return lambda y: master.circular(
        y, lambda z: board(z.T.astype(np.float32), SAMPLE_RATE).T.astype(np.float64), pad_s=pad_s)


def highpass_stem(y, hz):
    """pedalboard's high-pass on one stem, circularly (0.5 s of history is plenty)."""
    return _pedalboard(lambda pb: [pb.HighpassFilter(cutoff_frequency_hz=hz)], 0.5)(y)


def make_ir(seed=3, rt_low=2.3, rt_mid=1.9, rt_high=0.9, length=3.6, predelay=0.018):
    """Synthetic small-hall impulse response, stereo, unit energy per channel.

    Three bands (below 300 Hz, 300 Hz-3 kHz, above 3 kHz) of seeded noise decay with
    their own RT60, so the tail darkens as it fades; a 60 ms build-up follows the
    pre-delay; 14 sparse early reflections sit in the first 75 ms. Fully determined by
    the arguments (the seed is part of the design, not of the build).
    """
    rng = np.random.default_rng(seed)
    n = int(length * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    ir = np.zeros((n, 2))
    bands = [(None, 300, rt_low), (300, 3000, rt_mid), (3000, None, rt_high)]
    for c in range(2):
        noise = rng.standard_normal(n)
        tail = np.zeros(n)
        for lo, hi, rt in bands:
            if lo is None:
                sos = ss.butter(2, hi, 'low', fs=SAMPLE_RATE, output='sos')
            elif hi is None:
                sos = ss.butter(2, lo, 'high', fs=SAMPLE_RATE, output='sos')
            else:
                sos = ss.butter(2, [lo, hi], 'band', fs=SAMPLE_RATE, output='sos')
            tail += ss.sosfilt(sos, noise) * np.exp(-6.91 * t / rt)
        tail *= np.clip((t - predelay - 0.012) / 0.06, 0, 1)
        for _ in range(14):
            d = predelay + rng.uniform(0.004, 0.075)
            g = rng.uniform(0.15, 0.5) * np.exp(-d * 18) * rng.choice([-1, 1])
            tail[int(d * SAMPLE_RATE)] += g * 3
        ir[:, c] = tail
    ir /= np.sqrt((ir ** 2).sum() / 2)
    fade = int(0.05 * SAMPLE_RATE)
    ir[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return ir


def bus(y):
    """The bus chain described above: level to -16 LUFS, then EQ and compression."""
    meter = pyln.Meter(SAMPLE_RATE)
    y = y * 10 ** ((BUS_LEVEL_LUFS - meter.integrated_loudness(y)) / 20)
    chain = _pedalboard(lambda pb: [
        pb.HighpassFilter(cutoff_frequency_hz=32),
        pb.PeakFilter(cutoff_frequency_hz=280, gain_db=-1.5, q=0.9),
        pb.HighShelfFilter(cutoff_frequency_hz=8500, gain_db=1.5, q=0.7),
        pb.Compressor(threshold_db=-13.0, ratio=1.8, attack_ms=25, release_ms=260),
    ], master.DEFAULT_PAD_S)
    return chain(y)


def level_reference(stem, kind):
    """A stem's reference level in dBFS (see the module docstring)."""
    if kind == 'sustain':
        w, pct = int(0.4 * SAMPLE_RATE), 90
    else:
        w, pct = int(0.05 * SAMPLE_RATE), 99.5
    stem = np.asarray(stem, dtype=np.float64).reshape(len(stem), -1)
    n = len(stem) // w
    r = np.sqrt((stem[:n * w] ** 2).reshape(n, w * stem.shape[1]).mean(1))
    if n == 0 or r.max() <= 0.0:
        # A silent (or shorter-than-one-window) stem has no level to match; report
        # silence rather than failing on an empty percentile.
        return -240.0
    r = r[r > r.max() * 0.01]
    return float(20 * np.log10(np.percentile(r, pct) + 1e-12))


def measure_levels(stems, kinds):
    """``{player: reference dB}`` from ``stems[mood][player]`` (raw renders, before
    folding) and ``kinds[player]``, taking each player's first mood in
    ``LEVEL_MOOD_ORDER``."""
    refs = {}
    for mood in LEVEL_MOOD_ORDER:
        for name, stem in stems.get(mood, {}).items():
            if name not in refs:
                refs[name] = level_reference(stem, kinds[name])
    return refs


def mix_mood(mood, stems, levels, ir, adjust=None):
    """Mix one mood.

    ``stems`` is an ordered list of ``(PerformedPart, Instrument, folded_loop)``;
    ``levels`` the frozen references; ``adjust`` the set's extra dB for this mood, keyed
    by player name or by role (a player name takes precedence over its role). Returns the bus output, float64 ``(frames, 2)``, un-mastered.
    """
    adjust = adjust or {}
    n = len(stems[0][2])
    dry = np.zeros((n, 2))
    send = np.zeros((n, 2))
    for part, inst, y in stems:
        db = (REFERENCE_DB - levels[part.name] + ROLE_DB[part.role] + MOOD_ROLE_DB[mood].get(part.role, 0.0)
              + adjust.get(part.name, adjust.get(part.role, 0.0)) + inst.gain_db)
        y = highpass_stem(y * 10 ** (db / 20), stem_highpass_hz(part.role, inst))
        y = pan(y, inst.pan if part.pan is None else part.pan)
        dry += y
        send += y * inst.send
    return bus(dry + master.circular_convolve(send, ir) * REVERB_WET)
