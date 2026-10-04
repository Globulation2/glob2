# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks ``noise`` and ``balance``: spectral hygiene of each mood.

``noise`` -- what it measures, per mood, on a full-rate 2048/1024 STFT of the mono mix:

* ``hf_floor`` (judged): the 10th percentile, over frames, of the energy above 6 kHz,
  relative to the median full-band frame level (dB). Music has little energy up
  there in its quiet moments; hiss, aliasing and raw oscillator buzz are *always*
  there, so they lift this floor. This is the "scratchy, noisy" complaint about the
  unfiltered synth renders, measured.
* ``hf_flatness`` and ``hf10k`` (info): the median 6-16 kHz spectral flatness of the
  louder frames (1 = white noise) and the share of energy above 10 kHz. Kept for
  diagnosis; neither separates good from bad sets on the corpus (bright acoustic
  cymbals are as flat as synth fizz), so neither is judged.

Why it matters: the game's music plays for hours under sound effects at modest
volume. Broadband HF noise is fatiguing far beyond its level, and it survives every
mood change because all three moods share the same synth chain.

Threshold: ``hf_floor`` warns above -35 dB and fails above -32 dB. The two rejected
synth sets (unfiltered Surge renders of Moss Lanterns and Glass Garden) measured
-26 to -30 dB in every mood; with its filters switched on, glass-garden measures
-48 to -81 dB. Every other approved mood measures -38.8 dB or lower, except
apple-cider combat at -33.5 dB, whose always-on cymbal wash warns: the one approved
mood between the warn and fail lines. The ACE-Step folk sets, rejected for not
fitting the game, also failed here (-26 to -30 dB in building/combat): their
"generative sheen" is measurable even though it was not the stated reason.

``balance`` (warn only) -- tonal balance:

* ``lowmid``: share of energy in 150-500 Hz, where a crowded mix turns to mud;
  warns above 0.42 (approved sets <= 0.37, the muddy ACE-Step folk calm 0.48).
* ``centroid``: mean frame spectral centroid (librosa, 22.05 kHz). Calm warns above
  1600 Hz (approved calm moods 690-1530 Hz, the original's 1140 Hz; Toy Soldiers,
  rejected as bright, 2410 Hz). Any mood warns above 2900 Hz: approved building and
  combat moods measure 1090-2270 Hz, except apple-cider combat at 2910 Hz, which
  warns.

Balance is a matter of taste within wide limits, so it never fails a set.
"""
import numpy as np
import librosa

from .analysis import SR_ANALYSIS
from .result import CheckResult, INFO, PASS, WARN, band

NAME_NOISE = 'noise'
NAME_BALANCE = 'balance'


def hf_stats(freqs, power):
    """(hf_floor_db, hf_flatness, hf10k_db) for a (bins, frames) power spectrogram."""
    total = power.sum(axis=0)
    hf = power[freqs >= 6000].sum(axis=0)
    floor_db = 10 * np.log10(np.percentile(hf, 10) / (np.median(total) + 1e-30) + 1e-30)
    band_ = power[(freqs >= 6000) & (freqs <= 16000)]
    flat = np.exp(np.log(band_).mean(axis=0)) / band_.mean(axis=0)
    loud = total > np.percentile(total, 20)
    flatness = float(np.median(flat[loud])) if loud.any() else float('nan')
    hf10k_db = 10 * np.log10(power[freqs >= 10000].sum() / power.sum() + 1e-30)
    return float(floor_db), flatness, float(hf10k_db)


def frame_centroid(mood_audio):
    """Mean per-frame spectral centroid (Hz) at the analysis rate."""
    return float(np.mean(librosa.feature.spectral_centroid(S=mood_audio.mag22, sr=SR_ANALYSIS)))


def check_noise(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME_NOISE, description='broadband HF noise floor (hiss, fizz, aliasing)')
    for mood in trio.present():
        f, p = trio[mood].power_full
        floor_db, flat, hf10k = hf_stats(f, p)
        cr.add(f'{mood}.hf_floor', band(floor_db, t.hf_floor_warn_db, t.hf_floor_fail_db), floor_db,
               f'<= {t.hf_floor_warn_db:g} dB (fail > {t.hf_floor_fail_db:g})',
               '10th-pct >6 kHz energy vs median frame', unit='dB')
        cr.add(f'{mood}.hf_flatness', INFO, flat, '', '6-16 kHz flatness, louder frames')
        cr.add(f'{mood}.hf10k', INFO, hf10k, '', 'energy share above 10 kHz', unit='dB')
    return cr


def check_balance(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME_BALANCE, description='low-mid mud and brightness (warn only)')
    for mood in trio.present():
        a = trio[mood]
        lm = a.band_share(150, 500)
        cr.add(f'{mood}.lowmid', WARN if lm > t.lowmid_share_warn else PASS, lm,
               f'<= {t.lowmid_share_warn:g}', 'energy share 150-500 Hz')
        c = frame_centroid(a)
        limit = t.calm_centroid_max_hz if mood == 'calm' else t.centroid_max_hz
        cr.add(f'{mood}.centroid', WARN if c > limit else PASS, c, f'<= {limit:g} Hz',
               'mean frame spectral centroid', unit='Hz')
    return cr
