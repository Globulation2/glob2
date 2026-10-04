# SPDX-License-Identifier: GPL-3.0-or-later
"""Stems and mixes: decoding, re-balancing per mood and cutting one shared loop.

Pipeline role: an adapted set starts from someone else's finished music. Either the
composer published instrument tracks (stems), or ``adapt.demucs`` estimates them from
the stereo mix. Either way the three moods are re-balanced mixes of the *same*
material, so they share one timeline by construction and the game can crossfade
between them at any position. This module holds the steps the adapted recipes share:

1. ``decode`` the pinned source files to float64 stereo at 44.1 kHz.
2. Re-balance per mood:

   * ``remix`` -- for *estimated* stems: start from the original mix and add
     ``(gain - 1) * stem``. Whatever the separator left unassigned (its residual)
     stays in every mood, and a mood with no changes is the untouched mix.
   * ``sum_stems`` -- for *real* stems that add up to the master: a weighted sum.
   * ``level_under`` -- set an added layer's level relative to the music it joins.

3. ``cut_loop`` -- cut the loop region out of every layer at identical frames, with a
   short seam crossfade from the material that *followed* the loop end in the source,
   so tails of the last bar carry over the wrap. (A long stem loop can instead lose
   an inner block of bars with ``glob2music.loop.splice``.)
4. Shape each mood with loop-aware tools: ``glob2music.master`` filters, plus
   ``tilt_shelf``, ``widen`` and ``level_ride`` here.

Every function takes and returns ``(frames, 2)`` float64 arrays at
``glob2music.spec.SAMPLE_RATE``. Filters run circularly (``glob2music.master``), so
nothing clicks at the seam; before the cut they may run over the whole track, because
the cut's crossfade comes from the source's real continuation.
"""
from pathlib import Path
import shutil
import subprocess
import zipfile

import numpy as np

from .. import master
from ..audio import db_to_gain
from ..loop import seam_crossfade
from ..spec import SAMPLE_RATE

SR = SAMPLE_RATE


# ----------------------------------------------------------------------------- inputs

def decode(path):
    """Decode an audio file with ffmpeg to float64 ``(frames, 2)`` at 44.1 kHz.

    ffmpeg rather than libsndfile: it removes the MP3 encoder delay (LAME gapless
    info) the way the approved sets were made, and reads WAV and MP3 alike. Mono
    sources are duplicated to stereo.
    """
    if not shutil.which('ffmpeg'):
        raise RuntimeError('ffmpeg is required to decode adapted sources')
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'f32le', '-ac', '2',
                          '-ar', str(SR), '-'], capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).astype(np.float64)


def join_split_archive(parts, dest):
    """Concatenate the parts of a split zip (``x.zip.001``, ``.002``, ...) into ``dest``.

    OpenGameArt caps upload sizes, so large stem packs arrive split; joined, the
    parts form one ordinary zip. The join happens once (an existing ``dest`` is
    reused) and is verified with ``ZipFile.testzip`` before it is kept.
    """
    dest = Path(dest)
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = dest.with_suffix(dest.suffix + '.part')
        with open(tmp, 'wb') as out:
            for p in parts:
                with open(p, 'rb') as src:
                    shutil.copyfileobj(src, out)
        with zipfile.ZipFile(tmp) as z:
            bad = z.testzip()
        if bad:
            tmp.unlink()
            raise ValueError(f'{dest.name}: corrupt member {bad} after joining the parts')
        tmp.replace(dest)
    return dest


# ----------------------------------------------------------------------------- re-balancing

def rms_db(y):
    """RMS level in dBFS of a whole signal."""
    return float(20 * np.log10(np.sqrt(np.mean(np.square(y))) + 1e-12))


def remix(mix, stems, gains_db, lowpass_hz=None):
    """Re-balance a mix with *estimated* stems: ``mix + sum((g - 1) * stem)``.

    ``gains_db`` maps stem names to a gain in dB, or ``None`` to remove the stem;
    stems not named keep their level. ``lowpass_hz`` maps stem names to a low-pass
    corner: that stem is replaced by its filtered, re-gained copy. Because each change
    is a difference from the mix, the separator's residual is preserved and its
    errors only matter in proportion to how far a stem is moved.
    """
    lowpass_hz = lowpass_hz or {}
    y = np.array(mix, dtype=np.float64)
    for name, g in gains_db.items():
        stem = stems[name]
        if name in lowpass_hz and g is not None:
            y += master.lowpass(stem, lowpass_hz[name]) * db_to_gain(g) - stem
        else:
            y += ((db_to_gain(g) if g is not None else 0.0) - 1.0) * stem
    return y


def sum_stems(stems, gains_db, lowpass_hz=None):
    """Mix *real* stems: ``sum(g * stem)`` over the stems named in ``gains_db``.

    ``None`` mutes a stem, and stems absent from ``gains_db`` are muted too, so each
    mood spells out its whole arrangement. ``lowpass_hz`` low-passes chosen stems.
    """
    lowpass_hz = lowpass_hz or {}
    out = None
    for name, g in gains_db.items():
        if g is None:
            continue
        x = master.lowpass(stems[name], lowpass_hz[name]) if name in lowpass_hz else stems[name]
        out = x * db_to_gain(g) if out is None else out + x * db_to_gain(g)
    if out is None:
        raise ValueError('every stem is muted')
    return out


def level_under(layer, reference, below_db):
    """Scale ``layer`` so its RMS sits ``below_db`` dB under the RMS of ``reference``.

    Added layers (synthesised percussion, say) are levelled against the music they
    join rather than in absolute terms, so the balance survives changes to either.
    Returns the scaled layer; add it to the mix yourself.
    """
    return np.asarray(layer, dtype=np.float64) * db_to_gain(rms_db(reference) - rms_db(layer) - below_db)


# ----------------------------------------------------------------------------- the loop cut

def cut_loop(y, start, end, crossfade_s, pre_s):
    """Cut the source region ``[start, end)`` (both moved ``pre_s`` earlier) as one loop.

    The cut points sit ``pre_s`` before the musical boundaries, so the seam crossfade
    finishes before the downbeat transient. The loop's first ``crossfade_s`` are an
    equal-power blend *from* the audio that followed the loop end in the source *into*
    the loop start (``loop.seam_crossfade``): when the game wraps from the last frame to
    the first, it hears the true continuation of the last bar, tails included, fading
    into bar 1. Apply the same arguments to every layer of a set.
    """
    y = np.asarray(y, dtype=np.float64)
    shift = int(round(pre_s * SR))
    a, b = start - shift, end - shift
    n = max(1, int(round(crossfade_s * SR)))
    if a < 0 or b + n > len(y):
        raise ValueError('the loop cut needs material before start and after end')
    return seam_crossfade(y[a:b], y[b:b + n], n)


# ----------------------------------------------------------------------------- colour and level

def tilt_shelf(y, hz, gain_db, kind='high'):
    """A soft shelf made by adding a filtered copy: ``y + (g - 1) * filtered(y)``.

    ``kind='high'`` lifts (or cuts) the region above ``hz`` through a 2nd-order
    Butterworth high-pass; ``'low'`` the region below through a low-pass. Gentler
    around the corner than an RBJ shelf; the approved adapted sets use it for their
    combat brightness lift. Loop-aware.
    """
    band = master.highpass(y, hz) if kind == 'high' else master.lowpass(y, hz)
    return np.asarray(y, dtype=np.float64) + (db_to_gain(gain_db) - 1.0) * band


def widen(y, amount, above_hz=300.0):
    """Mid/side widening: scale the side signal above ``above_hz`` by ``amount``.

    The low end stays mono (bass and low drums remain centred); 1.3 is a clear but
    natural widening. Loop-aware.
    """
    y = np.asarray(y, dtype=np.float64)
    mid = (y[:, 0] + y[:, 1]) / 2
    side = (y[:, 0] - y[:, 1]) / 2
    side = side + (amount - 1.0) * master.highpass(side, above_hz)
    return np.stack([mid + side, mid - side], axis=1)


def level_ride(y, window_s=4.0, max_boost_db=12.0, max_cut_db=6.0, amount=0.75):
    """Ride a loop's level slowly towards its 75th-percentile loudness.

    For calm moods built from a sparse layer (Curious Critters' melody opens 20+ dB
    under its middle): calm must stay audible under game sound effects. The power is
    averaged over ``window_s`` circularly; the gain is ``amount`` times the shortfall
    from the reference, limited to ``+max_boost_db`` / ``-max_cut_db``. Only slow
    changes are made, so note attacks and phrasing within a few seconds are untouched,
    and ``amount < 1`` keeps some of the original light and shade.
    """
    y = np.asarray(y, dtype=np.float64)
    w = int(window_s * SR)
    power = np.mean(y ** 2, axis=1)
    padded = np.cumsum(np.concatenate([power[-w:], power, power[:w]]))
    env = (padded[w + w // 2:w + w // 2 + len(y)] - padded[w - w // 2:w - w // 2 + len(y)]) / w
    level = 10 * np.log10(env + 1e-12)
    gain = np.clip((np.percentile(level, 75) - level) * amount, -max_cut_db, max_boost_db)
    return y * db_to_gain(gain)[:, None]
