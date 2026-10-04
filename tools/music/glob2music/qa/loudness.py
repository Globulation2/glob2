# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ``loudness``: integrated loudness per mood, true peak and the mood ladder.

What it measures: ITU-R BS.1770-4 integrated loudness (pyloudnorm, the same gating as
ffmpeg's ``ebur128``) of each decoded file; its true peak from a 4x-oversampled copy,
including inter-sample peaks across the loop seam; and the order calm <= building <=
combat.

Why it matters for the adaptive mixer: the game switches mood by crossfading at a
fixed gain, so the files' own loudness *is* the mix. Calm plays most of a session
under unit and building sound effects and must not vanish or tire the ear; combat
should rise, not jump. Targets of -18/-17/-16 LUFS give a gentle 1 LU step per mood,
and sets of different origin are interchangeable from the settings menu without the
player reaching for the volume. True peak above -1 dBTP risks clipping in the game's
16-bit mixing and in lossy re-encoding (Android/web bundles).

Thresholds: the targets and the -1 dBTP ceiling come from the soundtrack brief; the
+-1 LU tolerance is wide enough for Vorbis encoding (which itself moves loudness by
under 0.1 LU) plus the build's static true-peak trims after encoding, and narrow
enough to keep the ladder intact. Every pipeline-mastered corpus mood lies within
0.4 LU of its target, except woodland combat at -0.8 LU, which carries a 0.73 dB
encoding trim. The original soundtrack
(-13.9/-13.4/-21.8 LUFS) predates the rule and waives it; the rejected generated sets
miss it by up to 10 LU (calm at -28 LUFS) and fail.
"""
from .. import master
from ..spec import MOODS
from .result import CheckResult, FAIL, PASS, SKIP

NAME = 'loudness'


def check(trio, spec):
    t = spec.qa
    cr = CheckResult(NAME, description='integrated LUFS per mood, true peak, calm <= building <= combat')
    lufs = {}
    for mood in trio.present():
        a = trio[mood]
        if a.seconds < 0.5:
            cr.add(f'{mood}.target', SKIP, None, '', 'too short to gate')
            continue
        lufs[mood] = master.integrated_lufs(a.stereo, a.sample_rate)
        target = spec.target_lufs[mood]
        dev = lufs[mood] - target
        cr.add(f'{mood}.target', PASS if abs(dev) <= t.lufs_tolerance else FAIL, lufs[mood],
               f'{target:g} +- {t.lufs_tolerance:g} LUFS', f'{dev:+.1f} LU from target', unit='LUFS')
        tp = master.loop_true_peak_dbtp(a.stereo)
        cr.add(f'{mood}.true_peak', PASS if tp <= t.true_peak_max_dbtp else FAIL, tp,
               f'<= {t.true_peak_max_dbtp:g} dBTP', unit='dBTP')
    if all(m in lufs for m in MOODS):
        steps = [lufs['building'] - lufs['calm'], lufs['combat'] - lufs['building']]
        worst_step = min(steps)
        ok = worst_step >= -t.ladder_slack_lu
        cr.add('ladder', PASS if ok else FAIL, worst_step, f'each step >= -{t.ladder_slack_lu:g} LU',
               f'calm {lufs["calm"]:.1f} / building {lufs["building"]:.1f} / combat {lufs["combat"]:.1f} LUFS',
               unit='LU')
    return cr
