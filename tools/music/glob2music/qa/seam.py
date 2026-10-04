# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ``seam``: does each file loop without a click, a gap or a jolt?

What it measures, per mood, always treating the file as a circle (the last frame is
followed by the first, as in the game):

* ``step``: the sample step across the wrap, ``max |y[0] - y[-1]|`` over channels,
  as a percentile rank among all the file's own sample-to-sample steps. A
  discontinuity (a click) is a step larger than nearly anything inside the file.
* ``flux``: the spectral flux across the wrap -- the mean absolute log-magnitude
  difference between the 2048-sample window ending at the wrap and the one starting
  there -- as a percentile rank among the same measure at every 512-sample position in
  the file. A seam that jumps to unrelated material (different chord, timbre or
  texture) is more abrupt than the file's own transitions, onsets included.
* ``gap``: the length of near-silence (50 ms frames 20 dB or more under the file's
  median level) running across the wrap. A fade-out, a trimmed reverb tail or a rest
  left at the end of the source shows here even when the sample step is tiny.

Why it matters: every mood loops forever, so a seam defect repeats every minute or
so for the whole game, and after a crossfade the listener may hit the seam of any of
the three files. Judging each measure against the file's *own* distribution, rather
than a fixed number, keeps loud percussive combat and sparse calm comparable: a seam
only has to be as smooth as the music around it.

Thresholds: ``step`` warns above rank 99 and fails above 99.9. Approved sets rank
at most 96.7 (orchestral-dawn combat, whose seam lands on a transient); a synthetic
click ranks 100, and so does the original's calm file, whose wrap steps by 0.27 of
full scale (an audible click the original has always had; it waives the rule).
``flux`` warns above rank 99.6 and fails above 99.95: a seam on a phrase start or
downbeat legitimately ranks high (approved sets reach 98.2, and woodland building,
whose phrase restarts after a rest, warns at 99.69), so only a wrap more abrupt than
practically every transition inside the file is judged. ``gap`` warns
above 0.25 s and fails above 2 s. A rest at the seam can be musical: woodland's
source phrase ends in a 0.6-1.25 s rest that the maintainer accepted, which its
set.toml waives; nothing else in the corpus exceeds 0.15 s. A level-jump rank was tried and dropped:
loops that restart on a strong downbeat legitimately rank at 99.99.
"""
import numpy as np

from .result import CheckResult, band

NAME = 'seam'
_WIN = 2048
_HOP = 512


def rank(values, x):
    """Percentile rank (0-100) of ``x`` among ``values`` (strictly-below share)."""
    values = np.asarray(values)
    return float(100.0 * np.mean(values < x)) if len(values) else float('nan')


def step_rank(y):
    """(wrap step, percentile rank among in-file steps) for a (frames, ch) loop."""
    steps = np.abs(np.diff(y, axis=0)).max(axis=1)
    wrap = float(np.abs(y[0] - y[-1]).max())
    return wrap, rank(steps, wrap)


def flux_profile(mono):
    """Spectral flux across every 512-sample boundary of a circular signal.

    Returns ``flux`` where ``flux[k]`` compares the window ``[p - 2048, p)`` with
    ``[p, p + 2048)`` for ``p = k * 512``; ``flux[0]`` is the wrap.
    """
    n = len(mono)
    z = np.concatenate([mono[-_WIN:], mono, mono[:_WIN]]).astype(np.float64)
    starts = np.arange(0, n + _WIN, _HOP)          # window starts in padded coordinates
    starts = starts[starts + _WIN <= len(z)]
    window = np.hanning(_WIN)
    spec = np.empty((len(starts), _WIN // 2 + 1), dtype=np.float32)
    for c in range(0, len(starts), 2048):          # chunked to bound memory (~30 MB)
        idx = starts[c:c + 2048, None] + np.arange(_WIN)[None, :]
        spec[c:c + 2048] = np.log(np.abs(np.fft.rfft(z[idx] * window, axis=1)) + 1e-6)
    # window starting at p (padded p + WIN) vs the one starting at p - WIN (padded p)
    k = _WIN // _HOP
    flux = np.abs(spec[k:] - spec[:-k]).mean(axis=1)
    return flux[:(n + _HOP - 1) // _HOP]


def wrap_gap(mono, sample_rate, below_db=20.0, frame_s=0.05):
    """Seconds of continuous near-silence touching the wrap.

    50 ms frames more than ``below_db`` under the file's median frame level count as
    silent; the run is followed backwards from the last frame and forwards from the
    first. Returns ``(gap_s, longest_gap_elsewhere_s)``.
    """
    f = int(frame_s * sample_rate)
    n = len(mono) // f
    lv = 10 * np.log10(np.mean(np.asarray(mono[:n * f], dtype=np.float64).reshape(n, f) ** 2, axis=1) + 1e-12)
    low = lv < np.median(lv) - below_db
    end = 0
    while end < n and low[n - 1 - end]:
        end += 1
    start = 0
    while start < n - end and low[start]:
        start += 1
    longest, run = 0, 0
    for v in low[start:n - end]:
        run = run + 1 if v else 0
        longest = max(longest, run)
    return (end + start) * frame_s, longest * frame_s


def check(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME, description='click, spectral jump and gap at the loop wrap')
    for mood in trio.present():
        a = trio[mood]
        if a.frames < 4 * _WIN:
            continue
        wrap, r = step_rank(a.stereo)
        cr.add(f'{mood}.step', band(r, t.seam_step_warn_pct, t.seam_step_fail_pct), r,
               f'rank <= {t.seam_step_warn_pct:g} (fail > {t.seam_step_fail_pct:g})',
               f'wrap step {wrap:.4f}', unit='pct')
        flux = flux_profile(a.mono)
        r = rank(flux[1:], flux[0])
        cr.add(f'{mood}.flux', band(r, t.seam_flux_warn_pct, t.seam_flux_fail_pct), r,
               f'rank <= {t.seam_flux_warn_pct:g} (fail > {t.seam_flux_fail_pct:g})',
               f'wrap flux {flux[0]:.3f}, in-file median {np.median(flux[1:]):.3f}', unit='pct')
        gap, elsewhere = wrap_gap(a.mono, a.sample_rate)
        cr.add(f'{mood}.gap', band(gap, t.seam_gap_warn_s, t.seam_gap_fail_s), gap,
               f'<= {t.seam_gap_warn_s:g} s (fail > {t.seam_gap_fail_s:g})',
               f'near-silence across the wrap; longest elsewhere {elsewhere:.2f} s', unit='s')
    return cr
