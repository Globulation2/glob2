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
        palette={"dark": (10, 54, 42), "mid": (22, 86, 62), "light": (52, 120, 88)},
        style=Style(luma=62, std=8, grain_max=6), periodic=True, grid=8, blocks=4)
def render_moss(ctx):
    """Shared edge pillows with independent moss cushions farther inside."""
    p = RECIPES["moss"].palette
    f1, _, _, common_f1, _, _ = block_worley(ctx, 12, array=True)
    common_pillows_raw = 1 - common_f1
    common_pillows = normalize_like(common_pillows_raw, common_pillows_raw)
    pillows = normalize_like(1 - f1, common_pillows_raw)
    common_detail = fbm(ctx.shared_rng, 6, 3, size=ctx.size, array=True)
    own_detail = fbm(ctx.rng, 6, 3, size=ctx.size, array=True)
    detail = blend_fields(common_detail, own_detail, ctx.interior_mask)
    common_raw = 0.6 * common_pillows + 0.4 * common_detail
    raw = 0.6 * pillows + 0.4 * detail
    common_height = normalize_like(common_raw, common_raw)
    height = normalize_like(raw, common_raw)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, height, 0.2, reference_height=common_height)
    common_grain = lattice_noise(ctx.shared_rng, 48, size=ctx.size, array=True)
    own_grain = lattice_noise(ctx.rng, 48, size=ctx.size, array=True)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = rgb * (1 + 0.10 * (fine - 0.5) * 2)
    return rgb, None
