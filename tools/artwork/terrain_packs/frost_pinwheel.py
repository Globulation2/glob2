# SPDX-License-Identifier: GPL-3.0-or-later
"""The frost pinwheel terrain material."""

from ._frost_paths import FROST, SEAM, weather
from ._paving import decay_fields, dress, rectangle_geometry, stone_colour, stone_fates
from terrain_core import (
    record_stones,
    NATIVE_PX,
    RECIPES,
    Style,
    clamp01,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
)


def pinwheel_rectangles(size, cell, offset):
    """Four oblong pavers turning around a small square, `cell` native px a
    repeat, shifted by `offset` so no joint runs along a tile cut."""
    c = cell * NATIVE_PX
    a = (cell + cell // 4) // 2 * NATIVE_PX
    b = c - a
    rects, squares = [], []
    for gy in range(0, size, c):
        for gx in range(0, size, c):
            x, y = gx + offset, gy + offset
            rects += [(x, y, a, b), (x + a, y, b, a), (x + b, y + a, a, b), (x, y + b, b, a)]
            squares.append(len(rects))
            rects.append((x + b, y + b, a - b, a - b))
    return [(x % size, y % size, w, h, 2.5 * NATIVE_PX / 4 * 3) for x, y, w, h in rects], set(squares)


@recipe(name="frost_pinwheel", group="paths", label="Frost pinwheel", profile="paved", border_class="built",
        seam=SEAM,
        palette={**FROST, "dark": (186, 186, 198), "mid": (210, 210, 220), "light": (232, 232, 240),
                 "warm": (222, 216, 214), "cool": (194, 198, 228), "ice": (124, 144, 204), "ice_light": (184, 196, 236),
                 "glint": (255, 255, 255)},
        style=Style(luma=188, std=20, grain_max=12, match=0.8), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_frost_pinwheel(ctx):
    """Frosted quartz pavers in a pinwheel around clear blue ice squares, with
    crystal glints and hoarfrost spreading out of the joints."""
    p, size = RECIPES["frost_pinwheel"].palette, ctx.size
    rects, squares = pinwheel_rectangles(size, 32, 10 * NATIVE_PX)
    centres, ids, edge, _, _ = rectangle_geometry(size, rects, gap=1.0 * NATIVE_PX)
    decay = decay_fields(ctx, reclaim=(0.6, 0.86))
    reach = 32 + 20 * NATIVE_PX
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.05, cracked=0.18, sink=0.5, heave=0.3)
    texture = layout_field(ctx, lambda rng: fbm(rng, 12, 3, size=size))
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.45, own=0.8, staining=0.0)
    sq = [(1.0 if fates[k].tone > 0.5 else 0.2) if k in squares else 0.0 for k in ids]
    ice = ramp([clamp01(0.6 * t + 0.4 * smoothstep(0.0, 3.0 * NATIVE_PX, e)) for t, e in zip(texture, edge)],
               [(0.0, p["ice"]), (1.0, p["ice_light"])])
    rgb = mix(rgb, ice, sq)
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 96, size=size))
    rgb = mix(rgb, p["glint"], [smoothstep(0.9, 0.95, s) * (1 - q) for s, q in zip(speck, sq)], 0.45)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 8 * NATIVE_PX,
                joint=(0.0, 2.0), creep=3.5, chipping=1.0, bevel=2.0, bevel_strength=0.3,
                moss=1.0, weeds=0.6, lichen=0.6, grit=0.7, dust=0.18, lip=0.2, shadow=0.35, damp=0.5)
    rgb = weather(ctx, rgb, edge, decay, streaks=0.5, glaze=0.25)
    # The ice squares are inlays: they follow the pavers turning around them.
    record_stones(ctx, ids, edge, centres, inlays=squares)
    return rgb, None
