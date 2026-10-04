# SPDX-License-Identifier: GPL-3.0-or-later
"""Woodland: JC Sounds' Level theme remixed from the composer's own stems.

The OpenGameArt download is a three-part split zip; joined, it holds every cue of the
pack as stems. This recipe uses the eight "Main Loop" stems of the Level theme (a
designed 78-bar loop at 130 bpm), shortens the loop to 60 bars with one bar-line
splice, and mixes the stems three ways. set.toml lists every change.
"""
from glob2music.adapt import stems
from glob2music.audio import Trio
from glob2music.loop import splice
from glob2music.sources import extract
from glob2music.spec import SAMPLE_RATE as SR

BAR = SR * 60 / 130 * 4                       # frames per 4/4 bar at 130 bpm
LOOP_FRAMES = round(6350400 * 48000 / 44100)                          # 78 bars, the composer's loop
# Bars 32-49 are removed: of every 14-30-bar block, this one joined best (per-stem
# activity and chroma matched on both sides of the cut; see set.toml).
CUT_START, CUT_END = int(round(32 * BAR)), int(round(50 * BAR))

PACK = 'Woodland Music Pack Vol 1/Level/Fantasy Music Pack Vol 1_ Level_130bpm_Main Loop - '
STEMS = {   # our name -> file name suffix inside the pack
    'choir': 'Pack 1_Exploration Choir.mp3',
    'edrums': 'Pack 1_Exploration E Drums 1.mp3',
    'perc': 'Pack 1_Exploration Perc.mp3',
    'strings': 'Pack 1_Exploration Strings.mp3',
    'wwind': 'Pack 1_Exploration WWind.mp3',
    'xperiments': 'Pack 1_Exploration Xperiments.mp3',
    'xinst': 'Pack 1_Exploration Xtra Inst.mp3',
    'xperc': 'Pack 1_Exploration Xtra Perc.mp3',
}

# Gains in dB per stem (None = muted) and per-stem low-pass corners, per mood.
MOODS = {
    'calm': dict(gains=dict(choir=0, edrums=-19, perc=None, strings=-4, wwind=0, xperiments=-3, xinst=-1,
                            xperc=None),
                 lowpass=dict(edrums=500, strings=4000, xperiments=2500)),
    'building': dict(gains=dict(choir=-1, edrums=-3, perc=-9, strings=-1, wwind=0, xperiments=-1, xinst=0,
                                xperc=-6),
                     lowpass=dict(perc=5000)),
    'combat': dict(gains=dict(choir=0, edrums=3, perc=5, strings=0, wwind=-2, xperiments=0, xinst=0, xperc=4),
                   lowpass={}, shelf_db=2.0),
}


def load_stems(ctx):
    """Join the split archive and decode the eight Level "Main Loop" stems."""
    parts = [extract(ctx.source(f'woodland-part-{i}'), f'Woodland Music _Part {i}/Woodland Music Pack Vol 1.zip.00{i}')
             for i in (1, 2, 3)]
    pack = stems.join_split_archive(parts, ctx.work_dir / 'woodland-pack-vol-1.zip')
    out = {}
    for name, suffix in STEMS.items():
        out[name] = stems.decode(extract(pack, PACK + suffix, cache_dir=ctx.work_dir / 'pack'))
        if len(out[name]) != LOOP_FRAMES:
            raise ValueError(f'{name}: {len(out[name])} frames, expected the 78-bar loop of {LOOP_FRAMES}')
    return out


def build(ctx):
    layers = load_stems(ctx)
    moods = {}
    for mood, cfg in MOODS.items():
        y = stems.sum_stems(layers, cfg['gains'], cfg['lowpass'])
        if cfg.get('shelf_db'):
            y = stems.tilt_shelf(y, 4000, cfg['shelf_db'])
        moods[mood] = splice(y, CUT_START, CUT_END)
    return Trio(**moods, meta={'loop': '78-bar composer loop, bars 32-49 spliced out',
                               'splice_frames': [CUT_START, CUT_END]})
