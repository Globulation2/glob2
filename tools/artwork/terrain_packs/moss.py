# SPDX-License-Identifier: GPL-3.0-or-later
"""The moss terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    blend_fields,
    block_worley,
    fbm,
    lattice_noise,
    normalize_like,
    ramp,
    recipe,
    shade,
)


@recipe(name="moss", group="fertile", label="Moss", profile="soil", border_class="solid",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (26, 60, 38), "mid": (38, 84, 50), "light": (62, 110, 68)},
        style=Style(luma=68, std=7, grain_max=6), periodic=True, grid=8, blocks=4)
def render_moss(ctx):
    """Shared edge pillows with independent moss cushions farther inside."""
    p = RECIPES["moss"].palette
    f1, _, _, common_f1, _, _ = block_worley(ctx, 12)
    common_pillows_raw = [1 - v for v in common_f1]
    common_pillows = normalize_like(common_pillows_raw, common_pillows_raw)
    pillows = normalize_like([1 - v for v in f1], common_pillows_raw)
    common_detail = fbm(ctx.shared_rng, 6, 3, size=ctx.size)
    own_detail = fbm(ctx.rng, 6, 3, size=ctx.size)
    detail = blend_fields(common_detail, own_detail, ctx.interior_mask)
    common_raw = [0.6 * a + 0.4 * b for a, b in zip(common_pillows, common_detail)]
    raw = [0.6 * a + 0.4 * b for a, b in zip(pillows, detail)]
    common_height = normalize_like(common_raw, common_raw)
    height = normalize_like(raw, common_raw)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, height, 0.2, reference_height=common_height)
    common_grain = lattice_noise(ctx.shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(ctx.rng, 48, size=ctx.size)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.10 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    return rgb, None
