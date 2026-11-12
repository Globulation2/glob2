# SPDX-License-Identifier: GPL-3.0-or-later
"""The slate ashlar terrain material.

Wear: slate weathers by splitting along its cleavage. Rims flake away in thin
stepped layers that expose darker, bluer fresh stone; slabs crack along the
grain; a few have lost a whole top lamina; crusty lichen rosettes grow on the
quiet stretches; and the upland ground (grit, dark soil, grey-green moss)
takes back the joints and lost slabs.
"""
import math

import numpy as np

from ._rock_paths import SLATE_GROUND, layout_pair, rosettes, stamped
from ._paving import decay_fields, dress, per_stone, rectangle_geometry, stone_colour, stone_fates, stone_field
from terrain_core import (N, NATIVE_PX, RECIPES, Style, clamp01, fbm, flat, lattice_noise, layout_field, mix, ramp,
                          recipe, shade, smoothstep)

UNIT = N // 8  # an eighth of a tile: 4 native px
SHAPES = [((3, 2), 3), ((2, 2), 2), ((4, 2), 2), ((3, 1), 2), ((2, 1), 1)]  # (1, 1) only as the fallback


def ashlar(size, rng):
    """Random ashlar: rectangles of a few modular sizes packed on a periodic
    quarter-tile grid, long side either way."""
    per = size // UNIT
    taken = [[False] * per for _ in range(per)]
    rects = []
    for gy in range(per):
        for gx in range(per):
            if taken[gy][gx]:
                continue
            pool = [s for s, w in SHAPES for _ in range(w)]
            rng.shuffle(pool)
            pool.append((1, 1))
            for w, h in pool:
                if rng.random() < 0.5:
                    w, h = h, w
                cells = [((gx + i) % per, (gy + j) % per) for j in range(h) for i in range(w)]
                if all(not taken[y][x] for x, y in cells):
                    for x, y in cells:
                        taken[y][x] = True
                    # Half a module off the grid, so no joint follows a tile edge.
                    rects.append((gx * UNIT + UNIT / 2, gy * UNIT + UNIT / 2, w * UNIT, h * UNIT,
                                  rng.uniform(1.0, 3.0)))
                    break
    return rects


@recipe(name="slate_ashlar", group="paths", label="Slate ashlar", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={**SLATE_GROUND, "dark": (58, 62, 70), "mid": (94, 100, 108), "light": (132, 138, 144),
                 "warm": (112, 102, 94), "cool": (78, 92, 112), "stain": (120, 92, 70), "fresh": (62, 72, 90),
                 "lichen_pale": (178, 180, 170), "lichen_ochre": (170, 156, 96)},
        style=Style(luma=96, std=20, grain_max=14, match=0.8), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_slate_ashlar(ctx):
    p, size = RECIPES["slate_ashlar"].palette, ctx.size
    rects = ashlar(size, ctx.shared_rng)
    centres, ids, edge, _, _ = rectangle_geometry(size, rects, gap=NATIVE_PX)
    decay = decay_fields(ctx, reclaim=(0.8, 0.95))
    reach = 32 + 2 * UNIT
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.03, cracked=0.0, sink=0.55, heave=0.3)
    along_x = [r[2] >= r[3] for r in rects]
    grain_h = layout_field(ctx, lambda rng: fbm(rng, 2, 3, cells_y=40, size=size, array=True))
    grain_v = layout_field(ctx, lambda rng: fbm(rng, 40, 3, cells_y=2, size=size, array=True))
    lengthwise = np.array(along_x)[ids]
    grain = np.where(lengthwise, grain_h, grain_v)
    bed = stone_field(ctx, centres, reach, lambda rng: fbm(rng, 2, 2, size=size, array=True))
    texture = clamp01(0.55 * grain + 0.45 * np.array(bed)[ids])
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["dark"]), (0.55, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.45, own=0.6, staining=0.35)

    # Rim delamination: along stretches of each rim, thin layers have flaked
    # off in steps one to five native px deep, showing fresh darker stone.
    def flake_depth(rng):
        reach_field = fbm(rng, 10, 2, size=size, array=True)
        where = fbm(rng, 5, 2, size=size, array=True)
        return (1.0 + 5.0 * reach_field) * NATIVE_PX * smoothstep(0.45, 0.6, where)

    def flaked_of(depth):
        return np.where((depth > 0.5) & (edge > 0), smoothstep(0.5, -0.5, edge - depth), 0.0)
    d1c, d1 = layout_pair(ctx, flake_depth)
    d2c, d2 = layout_pair(ctx, flake_depth)
    flaked = np.maximum(flaked_of(d1), flaked_of(0.5 * d2))
    rgb = mix(rgb, p["fresh"], flaked, 0.45)
    relief = -(flaked_of(d1) + flaked_of(0.5 * d2)) * 0.5
    relief_c = -(flaked_of(d1c) + flaked_of(0.5 * d2c)) * 0.5
    rgb = shade(rgb, relief, 0.5, reference_height=relief_c)

    # Whole lost laminae on a few slabs: a darker fresh face with a crisp step.
    lamina_c, lamina = layout_pair(ctx, lambda rng: fbm(rng, 3, 3, size=size, array=True))
    chosen = per_stone(fates, lambda f: 1.0 if f.stain > 0.72 else 0.0)[ids]
    chosen_c = per_stone(common, lambda f: 1.0 if f.stain > 0.72 else 0.0)[ids]
    peeled = smoothstep(0.58, 0.6, lamina) * chosen
    rgb = mix(rgb, p["fresh"], peeled, 0.5)
    rgb = shade(rgb, -0.6 * peeled, 0.25, reference_height=-0.6 * smoothstep(0.58, 0.6, lamina_c) * chosen_c)

    # Cleavage splits: about one slab in five has split right across along its
    # grain, one side settled a little lower than the other.
    pixel = np.arange(size * size)
    px, py = pixel % size + 0.5, pixel // size + 0.5
    cx = np.array([c[0] for c in centres])[ids]
    cy = np.array([c[1] for c in centres])[ids]
    dx = (px - cx + size / 2) % size - size / 2
    dy = (py - cy + size / 2) % size - size / 2

    def splits(stones):
        split = per_stone(stones, lambda st: st.kink >= 0.6 and not st.lost)[ids] != 0
        split &= edge > 0
        angle = [(0.0 if along_x[k] else math.pi / 2) + 0.12 * (st.tone - 0.5) for k, st in enumerate(stones)]
        sin = np.array([math.sin(a) for a in angle])[ids]
        cos = np.array([math.cos(a) for a in angle])[ids]
        off = np.array([(st.cast * 0.3) * min(r[2], r[3]) for st, r in zip(stones, rects)])[ids]
        d = -dx * sin + dy * cos - off
        mask = np.where(split, 1 - smoothstep(0.25 * NATIVE_PX, 0.85 * NATIVE_PX, np.abs(d)), 0.0)
        side = np.where(split & (d > 0), -0.15, 0.0)
        return mask, side
    split, step = splits(fates)
    _, step_c = splits(common)
    rgb = shade(rgb, step, 0.6, reference_height=step_c)
    rgb = mix(rgb, p["crack"], split, 0.8)

    # Crusty lichen rosettes, pale grey and a few ochre, hugging the rims of
    # quiet, damp, unwalked stretches.
    body, rim = stamped(ctx, lambda rng: rosettes(ctx, rng, 90, 0.8, 2.2))
    quiet = smoothstep(0.3, 0.7, decay.damp) * (1 - decay.wear) * np.where((0 < edge) & (edge < 3 * NATIVE_PX), 1.0, 0.0)
    tint = layout_field(ctx, lambda rng: lattice_noise(rng, 3, size=size, array=True))
    lichen_c = mix(flat(p["lichen_pale"], size, array=True), p["lichen_ochre"], smoothstep(0.55, 0.7, tint), 1.0)
    rgb = mix(rgb, lichen_c, body * quiet * 0.35, 1.0)
    rgb = mix(rgb, lichen_c, rim * quiet, 0.7)

    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 40, size=size, array=True))
    rgb = rgb * (0.95 + 0.1 * fine)
    crack_heading = [0.0 if a else math.pi / 2 for a in along_x]
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, UNIT,
                joint=(0.0, 1.8), creep=1.2, chipping=1.4, bevel=1.6, bevel_strength=0.32, crack_width=1.0,
                crack_along=crack_heading, moss=0.35, weeds=0.4, lichen=0.4, grit=0.55, dust=0.16, lip=0.24)
    return rgb, None
