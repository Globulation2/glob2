# SPDX-License-Identifier: GPL-3.0-or-later
"""The road (trail) terrain material."""
from ._paving import decay_fields, dress, rectangle_geometry, stone_colour, stone_fates, uneven_rows
from terrain_core import (
    NATIVE_PX,
    RECIPES,
    Style,
    fbm,
    layout_field,
    ramp,
    recipe,
)


def sett_rectangles(size, rng):
    """Setts in running courses of uneven height, each with its own corner
    radius, wrapping the block."""
    stones = []
    y = 0
    for height in uneven_rows(size, rng, heights=(5, 6, 7, 8, 9, 10, 11)):
        start = rng.random() * size
        x, row = start, []
        while True:
            length = rng.uniform(30.0, 58.0)
            if start + size - (x + length) < 30.0:
                row.append((x, start + size - x))
                break
            row.append((x, length))
            x += length
        for x0, length in row:
            stones.append((x0, y, length, height, rng.uniform(3.0, 10.0)))
        y += height
    return stones


@recipe(name='road', group="legacy", label='Road', profile="paved", border_class="built",
        seam={'height': 3, 'cast_q8': 48, 'cast_width_q8': 512},
        palette={'dark': (74, 72, 64), 'mid': (122, 120, 110), 'light': (172, 168, 154),
                 'warm': (152, 124, 86), 'cool': (100, 112, 124), 'stain': (138, 96, 58), 'joint': (62, 50, 36),
                 'grit': (112, 98, 76), 'soil': (70, 56, 40), 'dust': (132, 120, 100)},
        style=Style(luma=116, std=22, grain_max=11),
        periodic=True, grid=8, blocks=4, preview=(133, 120, 101), minimap=(176, 138, 98), stone_edges=True)
def render_road(ctx):
    """An old trail of stone setts in uneven running courses: settled, chipped
    and sunken, a few cracked or lost to the dirt, grit and moss in the joints
    and the ground creeping back at its edges.

    Course and sett geometry is periodic and common to every layout, so no
    layout can cut a boundary stone; tones and wear vary inside.
    """
    p, size = RECIPES["road"].palette, ctx.size
    centres, ids, edge, _, _ = rectangle_geometry(size, sett_rectangles(size, ctx.shared_rng))
    decay = decay_fields(ctx, reclaim=(0.72, 0.9))
    reach = 32 + 58 + NATIVE_PX
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.06, cracked=0.12, sink=0.5, heave=0.3)
    texture = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    rgb = stone_colour(lambda tone: ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.45, own=0.75, staining=0.45)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 20.0,
                creep=1.0, chipping=1.6, bevel=2.0, bevel_strength=0.28, moss=0.5, weeds=0.5, lichen=0.5,
                grit=0.4, dust=0.22)
    return rgb, None
