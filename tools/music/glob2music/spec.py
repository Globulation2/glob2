# SPDX-License-Identifier: GPL-3.0-or-later
"""The trio format and every quality threshold: the single source of numbers.

Globulation 2 plays in-game music as a *trio*: ``data/zik/<set_id>/a1.ogg`` (calm),
``a2.ogg`` (building) and ``a3.ogg`` (combat). ``SoundMixer::openMusicSet`` accepts a
set only when all three are 44100 Hz stereo Ogg Vorbis files with one logical stream
and the same PCM frame count, because the mixer switches mood by crossfading into
the next file *at the current playback position* and loops each file forever. Its
crossfade lasts ``FADE_SAMPLE_COUNT`` = 32768 interleaved samples, about 0.37 s, so
the moods must agree on the beat to within a few milliseconds.

Everything else in the pipeline reads its numbers from here:

* ``master.finish`` masters to ``TrioSpec.target_lufs`` and ``master_ceiling_dbtp``;
* ``audio.write_trio`` encodes at ``vorbis_quality``;
* every check in ``glob2music.qa`` reads its pass/warn/fail bands from
  ``TrioSpec.qa`` (a ``QAThresholds``).

The QA thresholds were calibrated on the maintainer-approved and rejected sets listed
in ``tools/music/qa/corpus.toml``; ``docs/assets/music-pipeline.md`` summarises the
measured values behind every number below. Change a threshold only together with a
fresh calibration run (``python3 -m glob2music calibrate``), and say in the commit
which corpus set moved across the line.

Sets may not override thresholds. A set that legitimately breaks a rule (the
original soundtrack's inverted loudness ladder, say) *waives* that rule in its
``set.toml`` with a written reason; see ``manifest.py``.
"""
from dataclasses import dataclass, field

SAMPLE_RATE = 44100
CHANNELS = 2

#: Mood names in file order. ``a1.ogg`` is calm, ``a2.ogg`` building, ``a3.ogg`` combat.
MOODS = ('calm', 'building', 'combat')
FILENAMES = {'calm': 'a1.ogg', 'building': 'a2.ogg', 'combat': 'a3.ogg'}


@dataclass(frozen=True)
class QAThresholds:
    """Pass/warn/fail bands for ``glob2music.qa``. See each check's module for the
    reasoning; ``python3 -m glob2music calibrate`` for the measurements.

    Naming: ``*_warn`` crosses into warn, ``*_fail`` into fail. Percentile ranks are
    in percent (0..100) of the file's own internal distribution.
    """

    # -- format ---------------------------------------------------------------
    # Lengths outside 50-120 s are refused: shorter loops repeat audibly within a
    # session, longer files cost download size (the browser bundle budget) and
    # decode memory on mobile for no audible gain.
    min_seconds: float = 50.0
    max_seconds: float = 120.0

    # -- loudness -------------------------------------------------------------
    # Integrated loudness may deviate this far from the mood's target.
    lufs_tolerance: float = 1.0
    # Hard ceiling on the decoded true peak (4x oversampled), after Vorbis encoding.
    true_peak_max_dbtp: float = -1.0
    # calm <= building <= combat, allowing this much inversion between neighbours.
    ladder_slack_lu: float = 0.3

    # -- seam -----------------------------------------------------------------
    # Sample step across the wrap (last frame -> first frame), as a percentile rank of
    # the file's own sample steps.
    seam_step_warn_pct: float = 99.0
    seam_step_fail_pct: float = 99.9
    # Spectral flux across the wrap as a percentile rank of the flux at every
    # in-file position (512-sample grid).
    seam_flux_warn_pct: float = 99.6
    seam_flux_fail_pct: float = 99.95
    # Near-silence running across the wrap, seconds.
    seam_gap_warn_s: float = 0.25
    seam_gap_fail_s: float = 2.0

    # -- repetition -----------------------------------------------------------
    # Best 50 ms RMS-envelope self-correlation over lags 2 s .. half the file.
    repetition_warn: float = 0.80
    repetition_fail: float = 0.90
    # Combat legitimately rides a steady drum ostinato (the original scores 0.85),
    # so its bands are higher; the rejected generated sets' looped combat scores
    # 0.95-1.00.
    repetition_combat_warn: float = 0.90
    repetition_combat_fail: float = 0.95
    # Exact-copy detector: the fraction of the file that is a near-exact copy
    # (log-mel frames within this distance) of material at least 4 s away.
    exact_copy_db: float = 1.0
    exact_copy_warn: float = 0.25
    exact_copy_fail: float = 0.50

    # -- alignment ------------------------------------------------------------
    # Onset-envelope lag between moods. The mixer's crossfade is 0.37 s; past
    # ~10 ms two copies of a note flam audibly during the fade.
    align_lag_warn_ms: float = 8.0
    align_lag_fail_ms: float = 10.0
    # Peak onset correlation below which the lag is reported but not judged.
    align_min_peak_r: float = 0.15
    # Lag spread across four windows (tempo drift), judged only when every window's
    # onset correlation peak reaches drift_min_peak_r.
    drift_warn_ms: float = 12.0
    drift_fail_ms: float = 20.0
    drift_min_peak_r: float = 0.5
    # Mean 0.5 s chroma correlation of the harmonic parts of two moods.
    chroma_corr_warn: float = 0.50
    chroma_corr_fail: float = 0.25

    # -- contrast -------------------------------------------------------------
    # Mean |log-mel difference| (dB, level-matched) between two moods.
    mood_distance_warn_db: float = 2.0
    mood_distance_fail_db: float = 0.75
    # Combat must step up from building, by rhythm or by percussion (score 1.0 =
    # 10% more onsets or +0.10 percussive share).
    escalation_warn: float = 1.0
    escalation_fail: float = 0.6
    # Combat should be at least as percussive as calm (HPSS percussive energy share).
    percussive_ladder_slack: float = 0.02
    # Calm stays calm: percussive share and onset rate ceilings (warn only).
    calm_percussive_max: float = 0.15
    calm_onset_rate_max: float = 5.0

    # -- noise ----------------------------------------------------------------
    # 10th-percentile energy above 6 kHz relative to the median frame level, dB.
    hf_floor_warn_db: float = -35.0
    hf_floor_fail_db: float = -32.0

    # -- balance (warn only) --------------------------------------------------
    # Low-mid (150-500 Hz) energy share: above this the mix tends to sound "muddy".
    lowmid_share_warn: float = 0.42
    # Mean frame spectral centroid ceilings, Hz: calm, and any mood.
    calm_centroid_max_hz: float = 1600.0
    centroid_max_hz: float = 2900.0

    # -- audibility (warn only) -----------------------------------------------
    # Quietest 3 s window relative to the file's mean power.
    quietest_window_warn_db: float = -10.0

    # -- dropout (warn only) --------------------------------------------------
    # A dropout is a fall of at least ``dropout_drop_db`` from substantial material
    # (within ``dropout_context_db`` of the file median) that stays down for 100 ms.
    dropout_drop_db: float = 25.0
    dropout_context_db: float = 6.0
    dropout_warn_count: int = 1


@dataclass(frozen=True)
class TrioSpec:
    """The deliverable format, the mastering targets and the QA thresholds."""

    sample_rate: int = SAMPLE_RATE
    channels: int = CHANNELS
    #: Integrated loudness targets (LUFS, ITU-R BS.1770-4) per mood: calm sits
    #: under game sound effects without vanishing; combat is the loudest.
    target_lufs: dict = field(default_factory=lambda: {'calm': -18.0, 'building': -17.0, 'combat': -16.0})
    #: The limiter's true-peak ceiling, 0.5 dB under the -1 dBTP QA ceiling. Vorbis q2
    #: encoding can still overshoot it by about 1 dB (1.03 dB at most on the shipped
    #: sets), so ``manifest.encode_within_ceiling`` measures each decoded file and
    #: trims any mood above -1.2 dBTP (by at most 0.73 dB on the shipped sets).
    master_ceiling_dbtp: float = -1.5
    #: libvorbis VBR quality (-q:a). 2 is ~80 kb/s stereo. The maintainer chose it
    #: after a blind listening test against q3 and q6-7 (~190 kb/s): it roughly
    #: halves the shipped size with no audible loss under game sound (the nine sets
    #: beside the original total 25.7 MB in data/zik). Opus would save ~25% more but
    #: needs an engine change; see docs/assets/music-pipeline.md.
    vorbis_quality: float = 2.0
    qa: QAThresholds = field(default_factory=QAThresholds)


DEFAULT_SPEC = TrioSpec()
