# SPDX-License-Identifier: GPL-3.0-or-later
"""The slate ashlar terrain material.

Wear: slate weathers by splitting along its cleavage. Rims flake away in thin
stepped layers that expose darker, bluer fresh stone; slabs crack along the
grain; a few have lost a whole top lamina; crusty lichen rosettes grow on the
quiet stretches; and the upland ground (grit, dark soil, grey-green moss)
takes back the joints and lost slabs.
"""
import math

from ._rock_paths import SLATE_GROUND, layout_pair, rosettes, stamped
from ._paving import decay_fields, dress, rectangle_geometry, stone_colour, stone_fates, stone_field
from terrain_core import (N, NATIVE_PX, RECIPES, Style, clamp01, fbm, lattice_noise, layout_field, mix, ramp, recipe,
                          shade, smoothstep)

UNIT = N // 4  # a quarter tile: 8 native px
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
                                  rng.uniform(2.0, 6.0)))
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
    grain_h = layout_field(ctx, lambda rng: fbm(rng, 2, 3, cells_y=40, size=size))
    grain_v = layout_field(ctx, lambda rng: fbm(rng, 40, 3, cells_y=2, size=size))
    grain = [h if along_x[k] else v for h, v, k in zip(grain_h, grain_v, ids)]
    bed = stone_field(ctx, centres, reach, lambda rng: fbm(rng, 2, 2, size=size))
    texture = [clamp01(0.55 * g + 0.45 * bed[k]) for g, k in zip(grain, ids)]
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["dark"]), (0.55, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.45, own=0.6, staining=0.35)

    # Rim delamination: along stretches of each rim, thin layers have flaked
    # off in steps one to five native px deep, showing fresh darker stone.
    def flake_depth(rng):
        reach_field = fbm(rng, 10, 2, size=size)
        where = fbm(rng, 5, 2, size=size)
        return [(1.0 + 5.0 * r) * NATIVE_PX * smoothstep(0.45, 0.6, w) for r, w in zip(reach_field, where)]

    def flaked_of(depth):
        return [smoothstep(0.5, -0.5, e - d) if d > 0.5 and e > 0 else 0.0 for e, d in zip(edge, depth)]
    d1c, d1 = layout_pair(ctx, flake_depth)
    d2c, d2 = layout_pair(ctx, flake_depth)
    flaked = [max(a, b) for a, b in zip(flaked_of(d1), flaked_of([0.5 * d for d in d2]))]
    rgb = mix(rgb, p["fresh"], flaked, 0.45)
    relief = [-(a + b) * 0.5 for a, b in zip(flaked_of(d1), flaked_of([0.5 * d for d in d2]))]
    relief_c = [-(a + b) * 0.5 for a, b in zip(flaked_of(d1c), flaked_of([0.5 * d for d in d2c]))]
    rgb = shade(rgb, relief, 0.5, reference_height=relief_c)

    # Whole lost laminae on a few slabs: a darker fresh face with a crisp step.
    lamina_c, lamina = layout_pair(ctx, lambda rng: fbm(rng, 3, 3, size=size))
    chosen = [1.0 if fates[k].stain > 0.72 else 0.0 for k in ids]
    chosen_c = [1.0 if common[k].stain > 0.72 else 0.0 for k in ids]
    peeled = [smoothstep(0.58, 0.6, f) * c for f, c in zip(lamina, chosen)]
    rgb = mix(rgb, p["fresh"], peeled, 0.5)
    rgb = shade(rgb, [-0.6 * v for v in peeled], 0.25,
                reference_height=[-0.6 * smoothstep(0.58, 0.6, f) * c for f, c in zip(lamina_c, chosen_c)])

    # Cleavage splits: about one slab in five has split right across along its
    # grain, one side settled a little lower than the other.
    def splits(stones):
        mask, side = [0.0] * (size * size), [0.0] * (size * size)
        for i, k in enumerate(ids):
            st = stones[k]
            if st.kink < 0.6 or st.lost or edge[i] <= 0:
                continue
            x0, y0, w, h, _ = rects[k]
            cx, cy = centres[k]
            a = (0.0 if along_x[k] else math.pi / 2) + 0.12 * (st.tone - 0.5)
            off = (st.cast * 0.3) * min(w, h)
            dx = (i % size + 0.5 - cx + size / 2) % size - size / 2
            dy = (i // size + 0.5 - cy + size / 2) % size - size / 2
            d = -dx * math.sin(a) + dy * math.cos(a) - off
            mask[i] = 1 - smoothstep(0.25 * NATIVE_PX, 0.85 * NATIVE_PX, abs(d))
            side[i] = -0.15 if d > 0 else 0.0
        return mask, side
    split, step = splits(fates)
    _, step_c = splits(common)
    rgb = shade(rgb, step, 0.6, reference_height=step_c)
    rgb = mix(rgb, p["crack"], split, 0.8)

    # Crusty lichen rosettes, pale grey and a few ochre, hugging the rims of
    # quiet, damp, unwalked stretches.
    body, rim = stamped(ctx, lambda rng: rosettes(ctx, rng, 90, 0.8, 2.2))
    quiet = [smoothstep(0.3, 0.7, d) * (1 - w) * (1 if 0 < e < 3 * NATIVE_PX else 0)
             for d, w, e in zip(decay.damp, decay.wear, edge)]
    tint = layout_field(ctx, lambda rng: lattice_noise(rng, 3, size=size))
    lichen_c = mix([[c] * (size * size) for c in p["lichen_pale"]], p["lichen_ochre"],
                   [smoothstep(0.55, 0.7, t) for t in tint], 1.0)
    rgb = mix(rgb, lichen_c, [b * q * 0.35 for b, q in zip(body, quiet)], 1.0)
    rgb = mix(rgb, lichen_c, [r * q for r, q in zip(rim, quiet)], 0.7)

    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 40, size=size))
    rgb = [[c * (0.95 + 0.1 * v) for c, v in zip(ch, fine)] for ch in rgb]
    crack_heading = [0.0 if a else math.pi / 2 for a in along_x]
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, UNIT,
                joint=(0.0, 1.8), creep=1.2, chipping=1.4, bevel=1.6, bevel_strength=0.32, crack_width=1.0,
                crack_along=crack_heading, moss=0.35, weeds=0.4, lichen=0.4, grit=0.55, dust=0.16, lip=0.24)
    return rgb, None
