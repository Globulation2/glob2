# SPDX-License-Identifier: GPL-3.0-or-later
"""Loop-aware mastering: circular filters, loudness targets and a true-peak limiter.

Pipeline role: every recipe's ``Trio`` passes through ``finish(trio, spec)`` before
``audio.write_trio`` encodes it, so all sets share one loudness ladder, one peak
ceiling and one seam treatment. Recipes may also use the filters and
``circular_convolve`` while mixing.

Why "loop-aware": the game wraps each file from its last frame straight back to its
first, forever. Any stateful processor (IIR filter, compressor, limiter, reverb) run
over the file once starts from silence at frame 0, although in the game frame 0 is
preceded by the end of the loop. That mismatch is a click or a level jump at the seam
on every pass. Every processor here therefore runs *circularly*: the loop is padded
with its own tail before its start (and its own head after its end, for look-ahead),
processed, and the padding discarded, so the state entering frame 0 is the state the
end of the loop leaves behind. FIR convolution is done exactly, with the tail folded
back onto the start (``circular_convolve``).
"""
import math

import numpy as np
import pyloudnorm as pyln
import scipy.signal as ss
from scipy.ndimage import minimum_filter1d, uniform_filter1d

from .audio import Trio, db_to_gain
from .spec import DEFAULT_SPEC, MOODS, SAMPLE_RATE

#: Default circular padding. Long enough for every IIR filter here to settle (a
#: 20 Hz 2nd-order high-pass decays by >100 dB within 0.5 s) and for the limiter's
#: release, with a wide margin.
DEFAULT_PAD_S = 4.0


def _as2d(y):
    y = np.asarray(y, dtype=np.float64)
    return (y[:, None], True) if y.ndim == 1 else (y, False)


def circular(y, fn, pad_s=DEFAULT_PAD_S, sample_rate=SAMPLE_RATE):
    """Apply ``fn`` (array -> same-length array) to a loop with circular padding.

    ``fn`` sees ``[tail of y, y, head of y]`` and its output's middle section is
    returned, so filter state and look-ahead both wrap across the seam. If the pad
    exceeds the loop, the loop is tiled to make it.
    """
    y, flat = _as2d(y)
    n = len(y)
    p = int(round(pad_s * sample_rate))
    if p > n:
        reps = int(math.ceil(p / n))
        tiled = np.concatenate([y] * reps)
        head, tail = tiled[:p], tiled[-p:]
    else:
        head, tail = y[:p], y[n - p:]
    z = fn(np.concatenate([tail, y, head]))
    out = np.asarray(z)[p:p + n]
    return out[:, 0] if flat else out


def circular_sosfilt(y, sos, pad_s=DEFAULT_PAD_S, zero_phase=False):
    """Run an SOS IIR filter over a loop circularly (optionally forward-backward)."""
    filt = ss.sosfiltfilt if zero_phase else ss.sosfilt
    return circular(y, lambda z: filt(sos, z, axis=0), pad_s)


def highpass(y, hz, order=2, zero_phase=False):
    """Butterworth high-pass, loop-aware."""
    return circular_sosfilt(y, ss.butter(order, hz, 'high', fs=SAMPLE_RATE, output='sos'), zero_phase=zero_phase)


def lowpass(y, hz, order=2, zero_phase=False):
    """Butterworth low-pass, loop-aware. The usual fix for 'scratchy' synths."""
    return circular_sosfilt(y, ss.butter(order, hz, 'low', fs=SAMPLE_RATE, output='sos'), zero_phase=zero_phase)


def _rbj(kind, hz, gain_db, q=0.7071):
    """RBJ Audio-EQ-Cookbook biquad coefficients as one SOS row."""
    a_ = 10 ** (gain_db / 40)
    w0 = 2 * math.pi * hz / SAMPLE_RATE
    c, s = math.cos(w0), math.sin(w0)
    alpha = s / (2 * q)
    sa = 2 * math.sqrt(a_) * alpha
    if kind == 'lowshelf':
        b = [a_ * ((a_ + 1) - (a_ - 1) * c + sa), 2 * a_ * ((a_ - 1) - (a_ + 1) * c), a_ * ((a_ + 1) - (a_ - 1) * c - sa)]
        a = [(a_ + 1) + (a_ - 1) * c + sa, -2 * ((a_ - 1) + (a_ + 1) * c), (a_ + 1) + (a_ - 1) * c - sa]
    elif kind == 'highshelf':
        b = [a_ * ((a_ + 1) + (a_ - 1) * c + sa), -2 * a_ * ((a_ - 1) + (a_ + 1) * c), a_ * ((a_ + 1) + (a_ - 1) * c - sa)]
        a = [(a_ + 1) - (a_ - 1) * c + sa, 2 * ((a_ - 1) - (a_ + 1) * c), (a_ + 1) - (a_ - 1) * c - sa]
    elif kind == 'peak':
        b = [1 + alpha * a_, -2 * c, 1 - alpha * a_]
        a = [1 + alpha / a_, -2 * c, 1 - alpha / a_]
    else:
        raise ValueError(kind)
    return np.array([[b[0] / a[0], b[1] / a[0], b[2] / a[0], 1.0, a[1] / a[0], a[2] / a[0]]])


def shelf(y, hz, gain_db, kind='high', q=0.7071):
    """RBJ low or high shelf (``kind`` 'low'/'high'), loop-aware."""
    return circular_sosfilt(y, _rbj(kind + 'shelf', hz, gain_db, q))


def peak_eq(y, hz, gain_db, q=1.0):
    """RBJ peaking EQ, loop-aware (e.g. -1.5 dB at 280 Hz against mud)."""
    return circular_sosfilt(y, _rbj('peak', hz, gain_db, q))


def circular_convolve(x, ir):
    """Convolve a loop with an impulse response, folding the tail onto the start.

    Exact circular convolution per channel: the reverb of the loop's last notes rings
    into its first frames, as it does when the game wraps. ``ir`` is (taps,) or
    (taps, channels); a mono IR is applied to every channel.
    """
    x, flat = _as2d(x)
    ir = np.asarray(ir, dtype=np.float64)
    if ir.ndim == 1:
        ir = np.repeat(ir[:, None], x.shape[1], axis=1)
    n = len(x)
    out = np.zeros_like(x)
    for ch in range(x.shape[1]):
        full = ss.fftconvolve(x[:, ch], ir[:, ch % ir.shape[1]])
        pad = (-len(full)) % n
        out[:, ch] = np.concatenate([full, np.zeros(pad)]).reshape(-1, n).sum(axis=0)
    return out[:, 0] if flat else out


# ----------------------------------------------------------------------------- metering

def integrated_lufs(y, sample_rate=SAMPLE_RATE):
    """ITU-R BS.1770-4 integrated loudness (pyloudnorm), LUFS."""
    y, _ = _as2d(y)
    return float(pyln.Meter(sample_rate).integrated_loudness(y))


def true_peak_dbtp(y, oversample=4):
    """True peak (dBTP) from a 4x polyphase-oversampled copy, as BS.1770 Annex 2."""
    y, _ = _as2d(y)
    up = ss.resample_poly(y, oversample, 1, axis=0)
    return float(20 * np.log10(np.abs(up).max() + 1e-12))


def loop_true_peak_dbtp(y, oversample=4, pad=64):
    """True peak including the inter-sample peaks *across the seam* (the oversampling
    filter sees the loop's tail before its head)."""
    y, _ = _as2d(y)
    z = np.concatenate([y[-pad:], y, y[:pad]])
    up = ss.resample_poly(z, oversample, 1, axis=0)[pad * oversample:(pad + len(y)) * oversample]
    return float(20 * np.log10(np.abs(up).max() + 1e-12))


# ----------------------------------------------------------------------------- limiting

def _release(g, coeff):
    """Instant attack, one-pole release, sample by sample (numba when available)."""
    out = np.empty_like(g)
    cur = 1.0
    for i in range(len(g)):
        v = g[i]
        cur = v if v < cur else coeff * cur + (1.0 - coeff) * v
        out[i] = cur
    return out


try:  # numba ships with librosa; a pure-Python loop over 4M samples takes ~2 s
    from numba import njit
    _release = njit(cache=False)(_release)
except ImportError:  # pragma: no cover
    pass


def limit(y, ceiling_dbtp=DEFAULT_SPEC.master_ceiling_dbtp, lookahead_s=0.003, release_s=0.08, oversample=4):
    """Circular look-ahead true-peak limiter.

    The required gain at each frame is ``ceiling / peak``, where the peak is taken from
    the 4x-oversampled signal (so inter-sample overs count). A minimum filter of
    half-width ``lookahead`` makes the gain reach its floor *before* the peak; a
    moving average of width ``lookahead`` then turns that floor's hard edges into
    linear ramps. The average never exceeds the requirement: each frame inside its
    window took the minimum over a span that contains the frame being averaged for,
    so every term is already at or below that frame's need. The attack is therefore a
    ramp over ``lookahead`` rather than a one-sample gain step. Release is a one-pole
    rise. The whole chain runs circularly, so the
    gain curve is continuous across the seam. A final static trim catches the small
    overshoot gain modulation itself can cause, so the result never exceeds the ceiling.
    """
    y, flat = _as2d(y)
    ceiling = 10 ** (ceiling_dbtp / 20)
    look = max(1, int(round(lookahead_s * SAMPLE_RATE)))
    coeff = math.exp(-1.0 / (release_s * SAMPLE_RATE))

    def proc(z):
        up = np.abs(ss.resample_poly(z, oversample, 1, axis=0)).max(axis=1)
        peak = up[:len(z) * oversample].reshape(len(z), oversample).max(axis=1)
        peak = np.maximum(peak, np.abs(z).max(axis=1))
        need = np.minimum(1.0, ceiling / (peak + 1e-12))
        g = uniform_filter1d(minimum_filter1d(need, 2 * look + 1), look)
        g = _release(g, coeff)
        return z * g[:, None]

    out = circular(y, proc, pad_s=max(1.0, 10 * release_s))
    tp = loop_true_peak_dbtp(out, oversample)
    if tp > ceiling_dbtp:
        out = out * 10 ** ((ceiling_dbtp - tp) / 20)
    return out[:, 0] if flat else out


def normalise(y, target_lufs):
    """Static gain to ``target_lufs`` integrated loudness."""
    return np.asarray(y, dtype=np.float64) * db_to_gain(target_lufs - integrated_lufs(y))


def master_loop(y, target_lufs, ceiling_dbtp=DEFAULT_SPEC.master_ceiling_dbtp, highpass_hz=None,
                iterations=6, tolerance_lu=0.05):
    """Bring one loop to ``target_lufs`` with its true peak under ``ceiling_dbtp``.

    The input gain is solved iteratively: apply gain ``g``, limit if the true peak
    exceeds the ceiling, measure loudness, correct ``g`` by the shortfall, repeat until
    within ``tolerance_lu`` (usually 2-3 rounds). Limiting always starts from the
    un-limited input, so rounds never stack limiter passes. If the material is so
    peaky that the target cannot be reached, a final static trim keeps the ceiling and
    the report records the loudness actually reached. Returns ``(loop, report)``.
    """
    x = np.asarray(y, dtype=np.float64)
    if highpass_hz:
        x = highpass(x, highpass_hz)
    gain_db = target_lufs - integrated_lufs(x)
    rounds, limited = 0, False
    for rounds in range(1, iterations + 1):
        applied_db = gain_db
        out = x * db_to_gain(applied_db)
        limited = loop_true_peak_dbtp(out) > ceiling_dbtp
        if limited:
            out = limit(out, ceiling_dbtp)
        shortfall = target_lufs - integrated_lufs(out)
        if abs(shortfall) <= tolerance_lu:
            break
        gain_db += shortfall
    tp = loop_true_peak_dbtp(out)
    trim = min(0.0, ceiling_dbtp - tp)
    if trim < 0:
        out = out * db_to_gain(trim)
    report = {'lufs': round(integrated_lufs(out), 2), 'true_peak_dbtp': round(loop_true_peak_dbtp(out), 2),
              'input_gain_db': round(float(applied_db), 2), 'limited': bool(limited), 'rounds': rounds,
              'final_trim_db': round(float(trim), 2), 'target_lufs': target_lufs}
    return out, report


def finish(trio, spec=DEFAULT_SPEC, highpass_hz=20.0):
    """Master a recipe's raw ``Trio`` for the game. Returns a new ``Trio``.

    Each mood is high-passed at ``highpass_hz`` (DC and sub-sonic rumble waste
    headroom; ``None`` or ``0`` disables it, e.g. ``highpass_hz = 0`` in a set.toml
    whose recipe already high-passes), normalised to ``spec.target_lufs[mood]`` and
    true-peak limited under ``spec.master_ceiling_dbtp``, all circularly. The per-mood
    report lands in ``trio.meta['master']``. ``finish`` deliberately does no tonal EQ
    or compression: those are artistic, per-set decisions that belong in the recipe.
    A set that needs a different per-mood target gets it through its own spec
    (``manifest.set_spec``), so mastering and QA agree on the target.
    """
    if not isinstance(trio, Trio):
        raise TypeError('finish expects a Trio')
    out, reports = {}, {}
    for mood in MOODS:
        out[mood], reports[mood] = master_loop(trio[mood], spec.target_lufs[mood], spec.master_ceiling_dbtp,
                                               highpass_hz=highpass_hz)
    return Trio(sample_rate=trio.sample_rate, meta={**trio.meta, 'master': reports}, **out)
