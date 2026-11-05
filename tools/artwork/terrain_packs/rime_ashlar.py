# SPDX-License-Identifier: GPL-3.0-or-later
"""The rime ashlar terrain material."""

from ._frost_paths import FROST, SEAM, weather
from ._paving import decay_fields, dress, rectangle_geometry, stone_colour, stone_fates, uneven_rows
from terrain_core import (
    NATIVE_PX,
    RECIPES,
    Style,
    fbm,
    group_cracks,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
)


def ashlar_rectangles(size, rng):
    """Big carved blocks in uneven courses: longer and taller than road setts."""
    stones, y = [], 0
    for height in uneven_rows(size, rng, heights=(9, 10, 12, 13, 15)):
        start = rng.random() * size
        x = start
        while True:
            length = rng.uniform(56.0, 112.0)
            if start + size - (x + length) < 56.0:
                stones.append((x, y, start + size - x, height, rng.uniform(2.0, 6.0)))
                break
            stones.append((x, y, length, height, rng.uniform(2.0, 6.0)))
            x += length
        y += height
    return stones


@recipe(name="rime_ashlar", group="paths", label="Rime ashlar", profile="paved", border_class="built",
        seam=SEAM,
        palette={**FROST, "deep": (156, 168, 210), "mid": (198, 206, 234), "light": (234, 236, 248),
                 "warm": (216, 214, 228), "cool": (160, 174, 220)},
        style=Style(luma=186, std=22, grain_max=12, match=0.8), periodic=True, grid=8, blocks=4)
def render_rime_ashlar(ctx):
    """Blocks cut from the glacier: each its own depth of blue, with internal
    fracture planes, bright chipped rims, snow-packed joints and drifts."""
    p, size = RECIPES["rime_ashlar"].palette, ctx.size
    centres, ids, edge, _, _ = rectangle_geometry(size, ashlar_rectangles(size, ctx.shared_rng), gap=1.25 * NATIVE_PX)
    decay = decay_fields(ctx, reclaim=(0.6, 0.86))
    reach = 32 + 112 + NATIVE_PX
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.05, cracked=0.24, sink=0.55, heave=0.3)
    # Clear ice is darker and bluer inside, milkier toward the rim.
    texture = layout_field(ctx, lambda rng: fbm(rng, 6, 3, size=size))
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["deep"]), (0.55, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.4, own=0.7, staining=0.35)
    milk = [smoothstep(6.0 * NATIVE_PX, 0.0, e) for e in edge]
    rgb = mix(rgb, p["light"], milk, 0.35)
    # Fracture planes inside the ice: pale, thin, crossing several blocks.
    planes = group_cracks(ctx, cells=4, width=0.035, gaps=0.45)
    rgb = mix(rgb, (248, 252, 255), planes, 0.7)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 24.0,
                joint=(0.0, 2.2), creep=3.5, chipping=1.0, bevel=2.2, bevel_strength=0.32,
                moss=1.0, weeds=0.5, lichen=0.4, grit=0.7, dust=0.2, lip=0.2, shadow=0.35, damp=0.6)
    rgb = weather(ctx, rgb, edge, decay, streaks=0.5, glaze=0.25)
    return rgb, None
