# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ``alignment``: do the three moods share one timeline?

What it measures, for each pair of moods (calm/building, building/combat,
calm/combat):

* ``lag``: the time offset that best aligns their onset-strength envelopes (librosa
  onset strength at a 2.9 ms hop, circular cross-correlation over +-250 ms, refined
  to sub-hop precision by parabolic interpolation). Two arrangements of one timeline
  put their note onsets on the same grid, so the peak sits at 0 ms.
* ``drift``: the same lag measured in four equal windows; a steady lag means a fixed
  offset, a changing one means the moods run at slightly different tempi. Window
  estimates are noisy when a mood has few onsets (sparse calm moods), so drift is
  judged only when every window's correlation peak reaches ``drift_min_peak_r``.
* ``harmony``: the mean correlation of 0.5 s chroma blocks of the *harmonic* parts
  (HPSS removes drums, which would otherwise smear the chroma). Moods that share a
  timeline play the same chord at the same moment.

Why it matters: ``SoundMixer`` crossfades into the next mood at the current playback
position over only 0.37 s (32768 interleaved samples). If the moods are offset, both
copies of every note sound during the fade (a flam), and the new mood enters off the
beat; if they disagree harmonically, the fade passes through a clash. Both are heard
at every mood change in every game.

Thresholds: lag warns above 8 ms and fails above 10 ms. Flams below ~10 ms fuse
perceptually; approved pairs measure at most 5.2 ms, except moss-lanterns
calm/building at 8.1 ms (its humanised renders differ per mood), which warns, and an
exact copy offset by 15 ms or more fails. Drift warns above a 12 ms spread and fails above 20 ms when judged. When the onset
correlation peak is below 0.15 the moods share too little rhythm for the lag to mean
anything, and the lag is reported as ``info`` instead of judged. Harmony warns below
0.50 and fails below 0.25. Approved pairs score >= 0.80, except curious-critters
(approved), which warns at 0.40 between calm and building and 0.38 between calm and
combat, where calm is a sparse melody and the other moods a bass-heavy groove, so a low score is a prompt to listen rather than proof
of a clash; a pair in unrelated keys scores near 0. The original's combat file is a
drum track with no harmony (0.03) and waives the rule.
"""
import numpy as np

from ..spec import MOODS
from .analysis import SR_ANALYSIS
from .result import CheckResult, INFO, band

NAME = 'alignment'
PAIRS = (('calm', 'building'), ('building', 'combat'), ('calm', 'combat'))
_HOP_S = 64 / SR_ANALYSIS
_MAX_LAG_S = 0.25


def _z(x):
    x = np.asarray(x, dtype=np.float64)
    return (x - x.mean()) / (x.std() + 1e-12)


def circular_lag(a, b, max_lag):
    """Lag (frames, fractional) maximising corr(a[t], b[t + lag]) over |lag| <= max_lag,
    and the peak correlation. Positive lag: ``b`` is late relative to ``a``."""
    n = min(len(a), len(b))
    a, b = _z(a[:n]), _z(b[:n])
    c = np.fft.irfft(np.conj(np.fft.rfft(a)) * np.fft.rfft(b), n) / n
    lags = np.arange(-max_lag, max_lag + 1)
    v = c[lags % n]
    i = int(np.argmax(v))
    frac = 0.0
    if 0 < i < len(v) - 1:                            # parabolic refinement
        den = v[i - 1] - 2 * v[i] + v[i + 1]
        if den < 0:
            frac = 0.5 * (v[i - 1] - v[i + 1]) / den
    return float(lags[i] + frac), float(v[i])


def windowed_lags(a, b, max_lag, windows=4):
    n = min(len(a), len(b))
    w = n // windows
    out = []
    for k in range(windows):
        lag, r = circular_lag(a[k * w:(k + 1) * w], b[k * w:(k + 1) * w], max_lag)
        out.append((lag, r))
    return out


def chroma_agreement(ca, cb, block=21):
    """Mean Pearson correlation of ~0.5 s chroma blocks (block x 512-sample hops)."""
    n = min(ca.shape[1], cb.shape[1]) // block * block
    if n == 0:
        return float('nan')
    ba = ca[:, :n].reshape(12, -1, block).mean(axis=2)
    bb = cb[:, :n].reshape(12, -1, block).mean(axis=2)
    ba = ba - ba.mean(axis=0)
    bb = bb - bb.mean(axis=0)
    num = (ba * bb).sum(axis=0)
    den = np.sqrt((ba ** 2).sum(axis=0) * (bb ** 2).sum(axis=0))
    ok = den > 1e-12
    return float(np.mean(num[ok] / den[ok])) if ok.any() else float('nan')


def check(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME, description='shared timeline: onset lag, tempo drift, harmony')
    if not all(m in trio.moods for m in MOODS):
        return cr
    max_lag = int(round(_MAX_LAG_S / _HOP_S))
    for x, y in PAIRS:
        pair = f'{x}_{y}'
        a, b = trio[x], trio[y]
        lag, peak = circular_lag(a.onset_env_fine, b.onset_env_fine, max_lag)
        lag_ms = lag * _HOP_S * 1000
        if peak < t.align_min_peak_r:
            cr.add(f'{pair}.lag', INFO, lag_ms, f'|lag| <= {t.align_lag_warn_ms:g} ms',
                   f'onset peak r {peak:.2f} too weak to judge', unit='ms')
        else:
            cr.add(f'{pair}.lag', band(abs(lag_ms), t.align_lag_warn_ms, t.align_lag_fail_ms), lag_ms,
                   f'|lag| <= {t.align_lag_warn_ms:g} ms (fail > {t.align_lag_fail_ms:g})',
                   f'onset peak r {peak:.2f}', unit='ms')
            wl = windowed_lags(a.onset_env_fine, b.onset_env_fine, max_lag)
            lags_ms = [lg * _HOP_S * 1000 for lg, _ in wl]
            spread = max(lags_ms) - min(lags_ms)
            detail = 'window lags ' + ', '.join(f'{v:+.1f} (r {r:.2f})' for v, (_, r) in zip(lags_ms, wl)) + ' ms'
            if min(r for _, r in wl) >= t.drift_min_peak_r:
                cr.add(f'{pair}.drift', band(spread, t.drift_warn_ms, t.drift_fail_ms), spread,
                       f'spread <= {t.drift_warn_ms:g} ms (fail > {t.drift_fail_ms:g})', detail, unit='ms')
            else:
                cr.add(f'{pair}.drift', INFO, spread, '', detail + '; window peaks too weak to judge', unit='ms')
        cc = chroma_agreement(a.chroma_harmonic, b.chroma_harmonic)
        cr.add(f'{pair}.harmony', band(cc, t.chroma_corr_warn, t.chroma_corr_fail, higher_is_worse=False), cc,
               f'>= {t.chroma_corr_warn:g} (fail < {t.chroma_corr_fail:g})', 'mean 0.5 s chroma correlation')
    return cr
