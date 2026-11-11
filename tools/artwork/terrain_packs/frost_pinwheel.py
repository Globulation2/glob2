# SPDX-License-Identifier: GPL-3.0-or-later
"""The frost pinwheel terrain material."""
import numpy as np

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


def pinwheel_rectangles(size, repeats, offset):
    """Four oblong pavers turning around a small square, `repeats` times
    across the block, shifted by `offset` so no joint runs along a tile cut.
    The long side is five eighths of the repeat."""
    c = size / repeats
    a = c * 0.625
    b = c - a
    rects, squares = [], []
    for j in range(repeats):
        for i in range(repeats):
            x, y = i * c + offset, j * c + offset
            rects += [(x, y, a, b), (x + a, y, b, a), (x + b, y + a, a, b), (x, y + b, b, a)]
            squares.append(len(rects))
            rects.append((x + b, y + b, a - b, a - b))
    return [(x % size, y % size, w, h, 5.25) for x, y, w, h in rects], set(squares)


@recipe(name="frost_pinwheel", group="paths", label="Frost pinwheel", profile="paved", border_class="built",
        seam=SEAM,
        palette={**FROST, "dark": (186, 186, 198), "mid": (210, 210, 220), "light": (232, 232, 240),
                 "warm": (222, 216, 214), "cool": (194, 198, 228), "ice": (124, 144, 204), "ice_light": (184, 196, 236),
                 "glint": (255, 255, 255)},
        style=Style(luma=188, std=20, grain_max=13, match=0.8), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_frost_pinwheel(ctx):
    """Frosted quartz pavers in a pinwheel around clear blue ice squares, with
    crystal glints and hoarfrost spreading out of the joints."""
    p, size = RECIPES["frost_pinwheel"].palette, ctx.size
    rects, squares = pinwheel_rectangles(size, 11, 7 * NATIVE_PX)
    centres, ids, edge, _, _ = rectangle_geometry(size, rects, gap=1.0 * NATIVE_PX)
    decay = decay_fields(ctx, reclaim=(0.6, 0.86))
    reach = 32 + 14 * NATIVE_PX
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.05, cracked=0.18, sink=0.5, heave=0.3)
    texture = layout_field(ctx, lambda rng: fbm(rng, 12, 3, size=size, array=True))
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.45, own=0.8, staining=0.0)
    sq = np.array([(1.0 if f.tone > 0.5 else 0.2) if k in squares else 0.0 for k, f in enumerate(fates)])[ids]
    ice = ramp(clamp01(0.6 * texture + 0.4 * smoothstep(0.0, 3.0 * NATIVE_PX, edge)),
               [(0.0, p["ice"]), (1.0, p["ice_light"])])
    rgb = mix(rgb, ice, sq)
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 96, size=size, array=True))
    rgb = mix(rgb, p["glint"], smoothstep(0.9, 0.95, speck) * (1 - sq), 0.45)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 5.6 * NATIVE_PX,
                joint=(0.0, 2.0), creep=3.5, chipping=1.0, bevel=2.0, bevel_strength=0.3,
                moss=1.0, weeds=0.6, lichen=0.6, grit=0.7, dust=0.18, lip=0.2, shadow=0.35, damp=0.5)
    rgb = weather(ctx, rgb, edge, decay, streaks=0.5, glaze=0.25)
    # The ice squares are inlays: they follow the pavers turning around them.
    record_stones(ctx, ids, edge, centres, inlays=squares)
    return rgb, None
