# SPDX-License-Identifier: GPL-3.0-or-later
"""Making material loop: folding render tails, seam crossfades and loop-point search.

Pipeline role: recipes produce audio that must become *one seamless loop*. There are
three common situations, each with a helper here:

* **Rendered scores** (symbolic sets): render the score with a pre-roll and a long
  tail, then ``fold_tail`` adds everything after the loop end (reverb, releases)
  back onto the loop start, exactly what the listener hears when the game wraps.
  Alternatively render several passes and keep a middle one, blending its start
  with the true continuation using ``seam_crossfade``.
* **Existing recordings** (adapted and generated sets): ``find_loop_points`` searches
  a finished mix for a start/end pair whose surroundings match (chroma, log-mel and
  onset features, then re-ranked by envelope repetition), ``refine_loop_end`` makes
  it sample-accurate, and ``crossfade_loop`` cuts it with an equal-power seam.
* **Removing material** inside a loop: ``splice`` cuts a span with a short
  equal-power crossfade timed to finish just before the next downbeat transient.

All three moods of a trio must be cut at the *same* frames: run the search on one
reference mix (usually the fullest mood) and apply the same points to every mood.
"""
import numpy as np
import scipy.signal as ss

from .spec import SAMPLE_RATE


def _as2d(y):
    y = np.asarray(y, dtype=np.float64)
    return (y[:, None], True) if y.ndim == 1 else (y, False)


def fold_tail(y, loop_frames, preroll_frames=0):
    """Fold a render with pre-roll and tail into one loop of ``loop_frames``.

    ``y`` holds ``preroll_frames`` of lead-in, then the loop, then any tail. The tail
    (possibly longer than a loop) is added onto the loop start; anything sounding in
    the pre-roll (notes played early by humanised timing) is added onto the loop end,
    because in the game it precedes the wrap.
    """
    y, flat = _as2d(y)
    need = preroll_frames + loop_frames
    if len(y) < need:
        y = np.vstack([y, np.zeros((need - len(y), y.shape[1]))])
    out = y[preroll_frames:need].copy()
    tail = y[need:]
    for k in range(0, len(tail), loop_frames):
        seg = tail[k:k + loop_frames]
        out[:len(seg)] += seg
    if preroll_frames:
        pre = y[:preroll_frames]
        if preroll_frames > loop_frames:
            raise ValueError('pre-roll longer than the loop')
        out[loop_frames - preroll_frames:] += pre
    return out[:, 0] if flat else out


def equal_power(n):
    """(fade_out, fade_in) gain curves of length ``n`` with constant summed power."""
    t = np.linspace(0.0, np.pi / 2, n)
    return np.cos(t), np.sin(t)


def seam_crossfade(loop, continuation, crossfade_frames):
    """Blend the loop's first frames with the audio that *follows* its last frame.

    ``continuation`` is what the source plays right after the loop end (for a
    multi-pass render, the start of the next pass). Over ``crossfade_frames`` the loop
    start fades from that continuation into itself, so the wrap continues the end
    exactly and free-running oscillator phases cannot click.
    """
    loop, flat = _as2d(loop)
    cont, _ = _as2d(continuation)
    n = crossfade_frames
    if len(cont) < n or len(loop) < n:
        raise ValueError('crossfade longer than the material')
    out = loop.copy()
    fade_out, fade_in = equal_power(n)
    out[:n] = cont[:n] * fade_out[:, None] + loop[:n] * fade_in[:, None]
    return out[:, 0] if flat else out


def crossfade_loop(source, start, end, crossfade_frames):
    """Cut ``source[start:end]`` as a loop whose last ``crossfade_frames`` blend into the
    audio *before* ``start``, so the end leads into the start without a seam."""
    source, flat = _as2d(source)
    c = crossfade_frames
    if start < c:
        raise ValueError('need crossfade_frames of material before start')
    y = source[start:end].copy()
    fade_out, fade_in = equal_power(c)
    y[-c:] = source[end - c:end] * fade_out[:, None] + source[start - c:start] * fade_in[:, None]
    return y[:, 0] if flat else y


def splice(y, cut_a, cut_b, crossfade_s=0.012, pre_s=0.010):
    """Remove ``y[cut_a:cut_b]`` with an equal-power crossfade of ``crossfade_s`` that
    ends ``pre_s`` after the cut, so it completes before the downbeat transients,
    which usually sit a few ms after the grid line."""
    y, flat = _as2d(y)
    n = int(crossfade_s * SAMPLE_RATE)
    s = int(pre_s * SAMPLE_RATE)
    a0, b0 = cut_a - n + s, cut_b - n + s
    fade_out, fade_in = equal_power(n)
    mid = y[a0:a0 + n] * fade_out[:, None] + y[b0:b0 + n] * fade_in[:, None]
    out = np.concatenate([y[:a0], mid, y[b0 + n:]])
    return out[:, 0] if flat else out


# ----------------------------------------------------------------------------- loop search

_HOP = 1024     # feature hop at 44.1 kHz


def _features(mono):
    import librosa
    y = librosa.resample(np.asarray(mono, dtype=np.float32), orig_sr=SAMPLE_RATE, target_sr=22050)
    hop = _HOP // 2
    ch = librosa.feature.chroma_cqt(y=y, sr=22050, hop_length=hop)
    mel = librosa.power_to_db(librosa.feature.melspectrogram(y=y, sr=22050, hop_length=hop, n_mels=48))
    on = librosa.onset.onset_strength(y=y, sr=22050, hop_length=hop)[None]
    n = min(ch.shape[1], mel.shape[1], on.shape[1])

    def z(m):
        m = m[:, :n]
        return (m - m.mean(1, keepdims=True)) / (m.std(1, keepdims=True) + 1e-6)

    f = np.vstack([2.0 * z(ch), z(mel), 2.0 * z(on)])
    return f / (np.linalg.norm(f, axis=0, keepdims=True) + 1e-9)


def find_loop_points(mix, min_s, max_s, context_s=2.0, top=40, margin_s=(2.0, 2.0), repetition_weight=1.0):
    """Rank candidate loops ``[start, end)`` of a finished mix, best first.

    For every start frame ``a`` and loop length ``L`` between ``min_s`` and ``max_s``
    it scores how well the music around ``a + L`` matches the music around ``a``
    (cosine similarity of chroma + log-mel + onset features averaged over
    ``+-context_s``), i.e. how naturally playback would continue after jumping back.
    The best distinct candidates are re-ranked by the envelope repetition of the
    resulting loop (lower is better, see ``glob2music.qa.repetition``).

    Returns a list of dicts ``{score, seam_similarity, repetition, start, end}`` with
    sample positions; refine ``end`` with ``refine_loop_end`` before cutting.
    """
    from .qa.repetition import envelope_repetition
    mix, _ = _as2d(mix)
    feats = _features(mix.mean(1))
    nf = feats.shape[1]
    fps = SAMPLE_RATE / _HOP
    w = int(context_s * fps)
    lmin, lmax = int(min_s * fps), int(max_s * fps)
    a_lo = int(margin_s[0] * fps) + w
    kernel = np.ones(2 * w + 1) / (2 * w + 1)
    found = []
    for length in range(lmin, lmax + 1):
        hi = nf - length - w - int(margin_s[1] * fps)
        if hi <= a_lo:
            break
        d = np.einsum('ij,ij->j', feats[:, :nf - length], feats[:, length:])
        s = np.convolve(d, kernel, mode='same')
        best = a_lo + int(np.argmax(s[a_lo:hi]))
        found.append((float(s[best]), best, length))
    found.sort(reverse=True)
    picked = []
    for sc, a, length in found:
        if all(abs(length - l2) > fps or abs(a - a2) > fps for _, a2, l2 in picked):
            picked.append((sc, a, length))
        if len(picked) >= top:
            break
    out = []
    for sc, a, length in picked:
        start, end = a * _HOP, (a + length) * _HOP
        r = envelope_repetition(mix[start:end].mean(1), SAMPLE_RATE)['r']
        out.append({'score': sc - repetition_weight * 0.5 * max(0.0, r - 0.5), 'seam_similarity': sc,
                    'repetition': r, 'start': start, 'end': end})
    out.sort(key=lambda c: -c['score'])
    return out


def refine_loop_end(mix, start, end, search_ms=25.0, window_s=0.5):
    """Move ``end`` by up to ``search_ms`` so the audio after it best matches the audio
    after ``start`` (normalised cross-correlation). Returns ``(end, correlation)``."""
    mix, _ = _as2d(mix)
    s, w = int(search_ms * SAMPLE_RATE / 1000), int(window_s * SAMPLE_RATE)
    x = mix[start - w // 2:start + w].mean(1)
    seg = mix[end - s - w // 2:end + s + w].mean(1)
    c = ss.correlate(seg, x, mode='valid')
    norm = np.sqrt(ss.correlate(seg ** 2, np.ones_like(x), mode='valid')) * np.linalg.norm(x) + 1e-9
    k = int(np.argmax(c / norm))
    return end - s + k, float((c / norm)[k])
