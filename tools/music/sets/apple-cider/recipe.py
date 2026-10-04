# SPDX-License-Identifier: GPL-3.0-or-later
"""Apple Cider: Zane Little Music's cozy CC0 track, re-balanced from Demucs stems.

The source is one finished stereo mix. ``adapt.demucs`` estimates drums, bass,
other and vocals (htdemucs); each mood is the original mix with those stems moved
(``stems.remix``), so whatever Demucs did not assign stays in every mood and combat
is the untouched mix plus extra drums and bass. The loop is a 40-bar region of the
track, cut at identical frames for every mood.
"""
from glob2music.adapt import demucs, stems
from glob2music.audio import Trio

# Loop region in source frames, chosen by a loop search: on a fitted beat grid
# (99.31 bpm, first downbeat 0.547 s) bars 12-52 matched best across the wrap
# (per-stem activity and chroma), and the end was refined by 47 ms by
# cross-correlating drums + bass (a constant-tempo fit drifts that much in 40 bars).
LOOP_START, LOOP_END = 1303051, 5568257          # 29.548 s .. 126.264 s, 96.717 s
SEAM = dict(crossfade_s=0.010, pre_s=0.012)

MOODS = {
    # gains in dB relative to the stem's level in the mix (None = removed)
    'calm': dict(gains=dict(drums=-20, bass=-5, other=-1, vocals=-1), lowpass=dict(drums=900, other=7000)),
    'building': dict(gains=dict(drums=-7, bass=-1.5)),
    'combat': dict(gains=dict(drums=3.5, bass=1.5), shelf_db=1.5),
}


def build(ctx):
    mix = stems.decode(ctx.source('apple-cider'))
    parts = demucs.separate(mix, 'htdemucs', cache_dir=ctx.work_dir / 'demucs')
    moods = {}
    for mood, cfg in MOODS.items():
        y = stems.remix(mix, parts, cfg['gains'], cfg.get('lowpass'))
        y = stems.cut_loop(y, LOOP_START, LOOP_END, **SEAM)
        if cfg.get('shelf_db'):
            y = stems.tilt_shelf(y, 4000, cfg['shelf_db'])
        moods[mood] = y
    return Trio(**moods, meta={'loop_frames': [LOOP_START, LOOP_END], 'separation': 'htdemucs shifts=2'})
