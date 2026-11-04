# SPDX-License-Identifier: GPL-3.0-or-later
"""The deep snow terrain material."""
from terrain_core import (
    Ctx,
    RECIPES,
    Style,
    clamp01,
    fbm,
    lattice_noise,
    mix,
    ramp,
    recipe,
    shade,
    soft_blobs,
)


@recipe(name="deep_snow", group="ice", label="Deep snow", profile="soil", border_class="solid",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512, "fringe": [196, 210, 232], "fringe_q8": 64,
              "fringe_width_q8": 384},
        palette={"dark": (156, 170, 210), "mid": (204, 208, 234), "light": (236, 236, 250), "sparkle": (252, 252, 255)},
        style=Style(luma=203, std=9, grain_max=6), periodic=True, grid=8, blocks=4)
def render_deep_snow(ctx):
    """Interchangeable snow blocks: common edge drifts, varied interior mounds.

    Height, grain and sparkle masks share one periodic field at the perimeter.
    Only the interior's scalar fields vary; their common quantile range and
    lighting scale keep a layout's snow from retinting its shared boundaries.
    """
    p = RECIPES["deep_snow"].palette
    shared_rng, own_rng = ctx.shared_rng, ctx.rng
    common_raw = fbm(shared_rng, 3, 3, size=ctx.size)
    own_raw = fbm(own_rng, 3, 3, size=ctx.size)
    ordered = sorted(common_raw)
    low, high = ordered[int(0.02 * (len(ordered) - 1))], ordered[int(0.98 * (len(ordered) - 1))]
    span = high - low or 1.0
    common_height = [clamp01((v - low) / span) for v in common_raw]
    own_height = [clamp01((v - low) / span) for v in own_raw]
    interior = ctx.interior_mask
    height = [a + (b - a) * w for a, b, w in zip(common_height, own_height, interior)]
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(own_rng, 48, size=ctx.size)
    fine = [a + (b - a) * w for a, b, w in zip(common_grain, own_grain, interior)]
    rgb = [[c * (1 + 0.05 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    rgb = shade(rgb, height, 0.12, light=(-0.6, -0.8), reference_height=common_height)
    shared_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared_ctx.rng = shared_rng
    common_sparkle, _, _ = soft_blobs(shared_ctx, 6, 3.0, 4.0)
    own_sparkle, _, _ = soft_blobs(ctx, 6, 3.0, 4.0)
    sparkle = [a + (b - a) * w for a, b, w in zip(common_sparkle, own_sparkle, interior)]
    rgb = mix(rgb, p["sparkle"], sparkle, 0.6)
    return rgb, None
