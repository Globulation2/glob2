# SPDX-License-Identifier: GPL-3.0-or-later
"""Curious Critters: Matthew Pablo's playful CC BY 3.0 piece, re-arranged for contrast.

The source is one finished mix with faint drums, so moving Demucs stems alone gave
three moods that sounded too alike (the first adaptation's percussive share went only
0.02 -> 0.03 -> 0.09). This arrangement makes each mood a different texture on the
same 44-bar timeline:

* calm     -- only the sustained/melodic layer (htdemucs_6s "other", plus its faint
  "vocals" estimate), darkened, over a long reverb bed made from that same layer, with
  slow gain riding so its sparse opening stays audible. Ostinato, bass and drums go.
* building -- the original mix with its own percussion at -6 dB, plus a light
  synthesised pulse (quarter-note shaker, frame drum on beats 2 and 4).
* combat   -- the original mix with drums +6 dB, bass +3 dB and sub-bass +4 dB, plus a
  soft synthesised kit on the beat grid: taiko on beats 1 and 3, timpani (F2/C2) on
  1, frame-drum and shaker eighths, and three taiko swells every 8 bars. Kept warm
  and downtempo (no sixteenths, no fills), with mild widening and a small high shelf.

All added percussion and reverbs are synthesised here (``adapt.percussion``,
``adapt.room``); they are our own work.

The percussion "performance" (humanised timing and velocity, noise in each hit) and
the room presets are pinned to the values the maintainer approved by ear, rather
than drawn from ``ctx.rng``: re-rolling them would make a different performance.
They are still deterministic, so every build reproduces the approved set.
"""
import numpy as np

from glob2music import master
from glob2music.adapt import beatgrid, demucs, percussion, room, stems
from glob2music.adapt.percussion import Humaniser, Track
from glob2music.audio import Trio, db_to_gain

# Loop region in source frames, found with glob2music.loop.find_loop_points (min 55 s,
# max 118 s; feature similarity 0.959 across the wrap, waveform correlation 0.996
# after refine_loop_end). It is 44 bars of 4/4 at 101 bpm.
LOOP_START, LOOP_END = 719872, 5330724            # 16.324 s .. 120.878 s, 104.554 s
SEAM = dict(crossfade_s=0.030, pre_s=0.005)
GRID_BEATS = 176                                  # what fit_loop_grid must find
F2, C2 = 87.31, 65.41                             # timpani tuning (source key: F major)
PERFORMANCE_SEED = 2026                           # the approved humanisation of both kits
BED_ROOM, KIT_ROOM = 7, 1                         # approved room presets (adapt.room)


def phrase_position(bar):
    """0..3 within the source's 4-bar phrases, which start at bar 1 (bar 0 closes the
    phrase that wraps round the seam)."""
    return (bar - 1) % 4


def building_kit(grid, frames, rng):
    """A steady, unhurried pulse: shaker on every beat, open frame drum on 2 and 4, and
    a rim tick on the last off-beat of each phrase."""
    hum, kit = Humaniser(rng), Track(frames)
    for bar in range(grid.bars):
        k0 = bar * grid.beats_per_bar
        for q in range(4):
            kit.add(percussion.shaker(rng, hum.velocity(0.55 if q % 2 == 0 else 0.4), position=0.35),
                    hum.time(grid.beat(k0 + q), 3))
        for q in (1, 3):
            kit.add(percussion.frame_drum(rng, hum.velocity(0.45), True, position=-0.25), hum.time(grid.beat(k0 + q)))
        if phrase_position(bar) == 3:
            kit.add(percussion.frame_drum(rng, hum.velocity(0.35), False, position=0.2), hum.time(grid.beat(k0 + 3.5)))
    return room.add_room(master.lowpass(kit.audio, 8000), 0.2, KIT_ROOM, decay_s=1.2)


def combat_kits(grid, frames, rng):
    """(low drums, hand percussion) for combat, each low-passed and given a room."""
    hum, low, hands = Humaniser(rng), Track(frames), Track(frames)
    for bar in range(grid.bars):
        k0 = bar * grid.beats_per_bar
        pp = phrase_position(bar)
        low.add(percussion.taiko(rng, hum.velocity(1.0), f0=70, position=-0.1), hum.time(grid.beat(k0), 3))
        low.add(percussion.taiko(rng, hum.velocity(0.55), f0=66, position=0.1), hum.time(grid.beat(k0 + 2), 3))
        low.add(percussion.timpani(rng, hum.velocity(0.7), f0=F2 if bar % 2 == 0 else C2), hum.time(grid.beat(k0), 2))
        for e in range(8):               # frame drum: doum on the beats, tek off the beat
            if e % 2 == 0:
                hit = percussion.frame_drum(rng, hum.velocity(0.6 if e in (2, 6) else 0.45), True, position=-0.3)
            else:
                hit = percussion.frame_drum(rng, hum.velocity(0.5 if pp % 2 else 0.4), False, position=0.35)
            hands.add(hit, hum.time(grid.beat(k0 + e / 2)))
        for e in range(8):               # soft shaker eighths
            hands.add(percussion.shaker(rng, hum.velocity(0.4 if e % 2 == 0 else 0.25), position=0.5),
                      hum.time(grid.beat(k0 + e / 2), 3))
        if pp == 3 and (bar // 4) % 2 == 1:   # every 8 bars: three soft taiko swells on beat 4
            for q in range(3):
                hit = percussion.taiko(rng, hum.velocity(0.4 + 0.1 * q), f0=74 - 2 * q, position=-0.3 + 0.3 * q)
                low.add(hit, hum.time(grid.beat(k0 + 3 + q / 3), 2))
    low_audio = room.add_room(master.lowpass(low.audio, 2500), 0.18, KIT_ROOM, decay_s=1.6)
    hands_audio = room.add_room(master.lowpass(hands.audio, 5500), 0.25, KIT_ROOM, decay_s=1.3)
    return low_audio, hands_audio


def build(ctx):
    mix = stems.decode(ctx.source('curious-critters'))
    four = demucs.separate(mix, 'htdemucs', cache_dir=ctx.work_dir / 'demucs')
    six = demucs.separate(mix, 'htdemucs_6s', cache_dir=ctx.work_dir / 'demucs')
    cut = lambda y: stems.cut_loop(y, LOOP_START, LOOP_END, **SEAM)   # noqa: E731
    loop_mix = cut(mix)
    drums, bass = cut(four['drums']), cut(four['bass'])
    frames = len(loop_mix)

    grid = beatgrid.fit_loop_grid(loop_mix, 150, 199)
    if grid.beats != GRID_BEATS:
        raise ValueError(f'beat grid found {grid.beats} beats, expected {GRID_BEATS}')
    ctx.log.info('grid: %d beats at %.3f bpm (median deviation %.1f ms)', grid.beats, grid.bpm, grid.deviation_ms)

    # calm: the melodic layer only, darker, over a reverb bed of itself
    calm = cut(six['other']) + db_to_gain(-6) * cut(six['vocals'])
    calm = master.lowpass(master.highpass(calm, 160), 2800)
    bed = room.add_room(master.lowpass(calm, 1500), 1.0, BED_ROOM, decay_s=4.5, predelay_s=0.03)
    calm = stems.level_ride(calm + db_to_gain(-3) * bed)

    # one generator plays both kits, building first, as in the approved performance
    performance = np.random.default_rng(PERFORMANCE_SEED)

    # building: the source's own percussion pulled back, plus a light pulse
    building = loop_mix + (db_to_gain(-6) - 1) * drums
    building = building + stems.level_under(building_kit(grid, frames, performance), building, 11)

    # combat: heavier rhythm section and a soft synthesised kit
    core = (loop_mix + (db_to_gain(6) - 1) * drums + (db_to_gain(3) - 1) * bass
            + (db_to_gain(4) - 1) * master.lowpass(bass, 90, order=4))
    low, hands = combat_kits(grid, frames, performance)
    combat = core + stems.level_under(low, core, 4) + stems.level_under(hands, core, 3.5)
    combat = master.lowpass(stems.tilt_shelf(stems.widen(combat, 1.3), 3500, 2.0), 12000)

    return Trio(calm=calm, building=building, combat=combat,
                meta={'loop_frames': [LOOP_START, LOOP_END], 'grid_bpm': round(grid.bpm, 3), 'grid_beats': grid.beats,
                      'separation': 'htdemucs + htdemucs_6s, shifts=2'})
