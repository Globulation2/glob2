# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ``repetition``: is the file through-composed, or a short idea on repeat?

What it measures, per mood:

* ``envelope``: the mono mix is resampled to
  4 kHz, cut into 50 ms RMS frames, and correlated with itself at every lag from 2 s
  to half the file; the best Pearson r (and its lag) is reported. r near 1.0 means the
  loudness contour repeats exactly: the file is a shorter loop played several times.
* ``copy``: at the best few envelope lags of at least 4 s, the fraction of 23 ms
  log-mel frames that match the frame one lag later to within ``exact_copy_db`` (mean
  absolute dB). This tells an *exact* copy (identical audio, same rendering) from a
  steady rhythm whose envelope repeats while the notes change.

Why it matters: the game loops each mood for as long as the mood lasts, often tens of
minutes. A 20 s idea repeated inside an 80 s file becomes a 20 s loop to the ear and
fatigues quickly; this was the main reason the earlier generated sets were rejected.

Thresholds: calm and building warn at r >= 0.80 and fail at r >= 0.90 (the brief's
rule). Combat warns at 0.90 and fails at 0.95, because a steady drum ostinato
legitimately correlates (the original combat scores 0.85). Approved sets score
0.27-0.72, except curious-critters (building 0.86, which warns; combat 0.85, under
combat's bands: a steady downtempo groove over changing material). The rejected
generated sets score 0.94-1.00 in every mood. ``copy`` warns at 25% and fails at 50%
of the file being an exact copy; approved sets stay at or below 6.2%, while the worst
mood of each rejected generated set reaches 52-91%.
"""
import numpy as np
import scipy.signal as ss

from .result import CheckResult, band

NAME = 'repetition'
_ENV_SR = 4000
_FRAME = 200            # 50 ms at 4 kHz


def rms_envelope(mono, sample_rate):
    """50 ms RMS envelope at 4 kHz."""
    x = ss.resample_poly(np.asarray(mono, dtype=np.float64), _ENV_SR, sample_rate) if sample_rate != _ENV_SR else mono
    n = len(x) // _FRAME * _FRAME
    return np.sqrt(np.mean(x[:n].reshape(-1, _FRAME) ** 2, axis=1))


def lag_correlations(e, min_lag=40):
    """Pearson r of ``e[:-l]`` with ``e[l:]`` for l = min_lag .. len(e)//2 (vectorised)."""
    n = len(e)
    lags = np.arange(min_lag, n // 2)
    if not len(lags):
        return lags, np.array([])
    c = np.concatenate([[0.0], np.cumsum(e)])
    c2 = np.concatenate([[0.0], np.cumsum(e * e)])
    m = n - lags
    sa, sb = c[m], c[n] - c[lags]                    # sums of e[:m] and e[l:]
    qa, qb = c2[m], c2[n] - c2[lags]
    # cross terms sum e[i] * e[i + l]: one FFT autocorrelation
    size = 1 << int(np.ceil(np.log2(2 * n)))
    f = np.fft.rfft(e, size)
    ac = np.fft.irfft(f * np.conj(f), size)[:n]
    sab = ac[lags]
    cov = sab - sa * sb / m
    va = qa - sa * sa / m
    vb = qb - sb * sb / m
    r = cov / np.sqrt(np.maximum(va * vb, 1e-30))
    return lags, r


def envelope_repetition(mono, sample_rate):
    """``{'r', 'lag_s', 'lags', 'rs'}``: best envelope self-correlation over 2 s .. n/2."""
    e = rms_envelope(mono, sample_rate)
    lags, r = lag_correlations(e)
    if not len(r):
        return {'r': float('nan'), 'lag_s': float('nan'), 'lags': lags, 'rs': r}
    i = int(np.argmax(r))
    return {'r': float(r[i]), 'lag_s': float(lags[i] * _FRAME / _ENV_SR), 'lags': lags, 'rs': r}


def copy_fraction(logmel, frame_s, lag_candidates_s, tol_db):
    """Largest fraction of frames that equal the frame ``lag`` later (circularly)
    within ``tol_db`` mean absolute difference, over the candidate lags.
    Frames more than 60 dB under the file peak are floored so silence does not count
    as a match of meaningful material."""
    m = np.maximum(logmel, logmel.max() - 60.0)
    loud = m.mean(axis=0) > (m.mean(axis=0).max() - 40.0)
    best, best_lag = 0.0, float('nan')
    for lag_s in lag_candidates_s:
        k = int(round(lag_s / frame_s))
        if k <= 0 or k >= m.shape[1]:
            continue
        d = np.abs(m - np.roll(m, -k, axis=1)).mean(axis=0)
        frac = float(np.mean((d < tol_db) & loud))
        if frac > best:
            best, best_lag = frac, lag_s
    return best, best_lag


def check(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME, description='internal self-repetition (envelope r, exact copies)')
    for mood in trio.present():
        a = trio[mood]
        res = envelope_repetition(a.mono, a.sample_rate)
        warn, fail = ((t.repetition_combat_warn, t.repetition_combat_fail) if mood == 'combat'
                      else (t.repetition_warn, t.repetition_fail))
        cr.add(f'{mood}.envelope', band(res['r'], warn - 1e-12, fail - 1e-12), res['r'],
               f'r < {warn:g} (fail >= {fail:g})', f'best lag {res["lag_s"]:.2f} s')
        # candidate lags for the copy test: the 5 best envelope lags of at least 4 s
        lags_s = res['lags'] * _FRAME / _ENV_SR
        far = lags_s >= 4.0
        cands = lags_s[far][np.argsort(res['rs'][far])[-5:]] if far.any() else []
        frac, lag = copy_fraction(a.logmel, 512 / 22050, cands, t.exact_copy_db)
        cr.add(f'{mood}.copy', band(frac, t.exact_copy_warn, t.exact_copy_fail), frac,
               f'< {t.exact_copy_warn:g} (fail > {t.exact_copy_fail:g})',
               f'share of frames repeating exactly at lag {lag:.2f} s' if frac else 'no exact copies')
    return cr
