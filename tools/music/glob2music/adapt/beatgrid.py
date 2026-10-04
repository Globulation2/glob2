# SPDX-License-Identifier: GPL-3.0-or-later
"""Beat grid of a finished loop: where to place added percussion.

Pipeline role: when a recipe layers new material over a recording (Curious Critters'
synthesised percussion), the hits must land on the recording's beats, and the
pattern must wrap seamlessly: beat ``N`` of the loop has to be beat ``0`` again.

``fit_loop_grid`` therefore forces an integer number of beats into the loop length,
which fixes the period for each candidate count, and fits the phase to the loop's
onset envelope; the best (count, phase) pair wins. A constant tempo is assumed, which
holds for DAW-produced game music. As a sanity check the free-running librosa beat
tracker is compared with the fit (``Grid.deviation_ms``; Curious Critters: about 6-8
ms median, well inside the 37 ms resolution of the game's 0.37 s mood crossfade).
"""
from dataclasses import dataclass

import numpy as np

from ..spec import SAMPLE_RATE

SR = SAMPLE_RATE
_FEATURE_SR = 22050          # librosa analysis rate
_HOP = 256                   # onset-envelope hop at that rate (~11.6 ms)


@dataclass(frozen=True)
class Grid:
    """A constant-tempo beat grid in frames of the loop it was fitted to.

    ``beat(k)`` is the frame of beat ``k`` (fractional ``k`` gives subdivisions:
    ``k + 0.5`` is the off-beat); beats ``0, beats_per_bar, ...`` are downbeats.
    """

    period_s: float
    phase_s: float
    beats: int
    beats_per_bar: int = 4
    deviation_ms: float = float('nan')

    @property
    def bpm(self):
        return 60.0 / self.period_s

    @property
    def bars(self):
        return self.beats // self.beats_per_bar

    def beat(self, k):
        """Frame index of beat ``k``."""
        return int(round((self.phase_s + k * self.period_s) * SR))


def _mono22(y):
    import librosa
    mono = np.asarray(y, dtype=np.float32).mean(axis=1)
    return librosa.resample(mono, orig_sr=SR, target_sr=_FEATURE_SR)


def fit_loop_grid(loop, min_beats, max_beats, beats_per_bar=4, phase_step_s=0.0025, latency_s=0.012):
    """Fit a grid with an integer beat count in ``[min_beats, max_beats]`` to a loop.

    Each candidate count ``N`` fixes the period (loop length / ``N``); each phase, in
    ``phase_step_s`` steps, is scored by the mean onset strength at its ``N`` beats.
    The winning grid is then moved ``latency_s`` earlier: onset strength peaks a
    little after a note begins, and placed hits should coincide with attacks.
    The count need not be a multiple of ``beats_per_bar``; check
    ``grid.beats % grid.beats_per_bar`` before placing bar-based patterns.
    """
    import librosa
    y22 = _mono22(loop)
    env = librosa.onset.onset_strength(y=y22, sr=_FEATURE_SR, hop_length=_HOP)
    fps = _FEATURE_SR / _HOP
    seconds = len(loop) / SR
    best = None
    for n in range(int(min_beats), int(max_beats) + 1):
        period = seconds / n
        for phase in np.arange(0.0, period, phase_step_s):
            idx = np.round((phase + np.arange(n) * period) * fps).astype(int)
            score = env[idx[idx < len(env)]].mean()
            if best is None or score > best[0]:
                best = (score, n, period, phase)
    _, n, period, phase = best
    _, tracked = librosa.beat.beat_track(onset_envelope=env, sr=_FEATURE_SR, hop_length=_HOP, units='time')
    fitted = phase + np.arange(n) * period
    deviation = float(np.median([np.abs(fitted - t).min() for t in tracked]) * 1000) if len(tracked) else float('nan')
    return Grid(period_s=period, phase_s=phase - latency_s, beats=n, beats_per_bar=beats_per_bar,
                deviation_ms=deviation)
