# SPDX-License-Identifier: GPL-3.0-or-later
"""Warn about local mood changes hidden by whole-file loudness matching.

At every 100 ms on the shared timeline, compare stereo RMS over the gameplay
fade duration (~0.37 s), including windows across the loop seam. Report the 95th
percentile absolute level difference. More than 6 dB warns: a passage would change
substantially within one short fade even if integrated LUFS meets the targets.
This is a provisional listening prompt, not a perceptual or quality verdict.

Also report mean framewise Hellinger distance between energy-normalized mel power
spectra (0 identical, 1 disjoint). Unlike equal-band log-mel distance, it emphasizes
bands carrying energy. This is informational, with no acceptance threshold.
"""
import numpy as np

from ..preview import GAME_FADE_SECONDS
from .contrast import PAIRS
from .result import CheckResult, INFO, PASS, WARN


def local_levels(samples, rate, window_s=GAME_FADE_SECONDS, step_s=0.1):
    """Forward, circular stereo RMS windows in dBFS; starts include time zero."""
    power = np.mean(np.asarray(samples, dtype=np.float64) ** 2, axis=1)
    window = max(1, round(window_s * rate))
    starts = np.arange(0, len(power), max(1, round(step_s * rate)))
    padded = np.resize(power, len(power) + window)
    total = np.concatenate(([0.0], np.cumsum(padded)))
    means = (total[starts + window] - total[starts]) / window
    return 10 * np.log10(np.maximum(means, 1e-12))


def spectral_distance(a, b):
    """Mean Hellinger distance of matching mel-power frame distributions."""
    n = min(a.shape[1], b.shape[1])
    def shape(m):
        # Subtract each frame's maximum to avoid over/underflow; normalization
        # removes this level and every other uniform gain from the comparison.
        p = 10.0 ** ((m[:, :n] - m[:, :n].max(axis=0)) / 10.0)
        return p / p.sum(axis=0)
    pa, pb = shape(a), shape(b)
    return float(np.sqrt(0.5 * ((np.sqrt(pa) - np.sqrt(pb)) ** 2).sum(axis=0)).mean())


def check(trio, spec):
    result = CheckResult('transition', description='local level steps and spectral changes between moods')
    levels = {m: local_levels(trio[m].samples, trio[m].sample_rate) for m in trio.present()}
    for a, b in PAIRS:
        if a not in levels or b not in levels:
            continue
        # Position comparison is meaningful only on an equal-length timeline.
        if trio[a].sample_rate != trio[b].sample_rate or trio[a].frames != trio[b].frames:
            continue  # format reports these errors
        audible = np.maximum(levels[a], levels[b]) > -60.0
        if not audible.any():
            continue
        jump = float(np.percentile(np.abs(levels[a][audible] - levels[b][audible]), 95))
        threshold = spec.qa.transition_level_warn_db
        result.add(f'{a}_{b}.level_p95', WARN if jump > threshold else PASS,
                   jump, f'<= {threshold:g} dB',
                   'absolute RMS difference at matching positions over the gameplay fade; listen in both directions', unit='dB')
        result.add(f'{a}_{b}.spectral_shape', INFO,
                   spectral_distance(trio[a].logmel, trio[b].logmel), '',
                   'mean energy-normalized mel Hellinger distance: 0 identical, 1 disjoint; not a quality score')
    return result
