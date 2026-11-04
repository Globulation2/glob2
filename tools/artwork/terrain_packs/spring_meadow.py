# SPDX-License-Identifier: GPL-3.0-or-later
"""The spring meadow terrain material."""
from terrain_core import (
    Ctx,
    RECIPES,
    Style,
    blend_fields,
    desaturate,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    soft_blobs,
)


@recipe(name="spring_meadow", group="fertile", label="Spring meadow", profile="brush", border_class="solid",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (66, 120, 34), "mid": (94, 154, 48), "light": (130, 184, 70), "dot": (240, 232, 160)},
        style=Style(luma=102, std=7, grain_max=7), periodic=True, grid=8, blocks=4)
def render_spring_meadow(ctx):
    """Common edge turf and blooms, with independently seeded interiors."""
    p = RECIPES["spring_meadow"].palette
    shared_rng, own_rng = ctx.shared_rng, ctx.rng
    common_raw = fbm(shared_rng, 8, 3, size=ctx.size)
    own_raw = fbm(own_rng, 8, 3, size=ctx.size)
    common_height = normalize_like(common_raw, common_raw, 0.02, 0.98)
    own_height = normalize_like(own_raw, common_raw, 0.02, 0.98)
    height = blend_fields(common_height, own_height, ctx.interior_mask)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(own_rng, 48, size=ctx.size)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    rgb = desaturate(rgb, 0.06)
    shared_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared_ctx.rng = shared_rng
    common_dots, _, _ = soft_blobs(shared_ctx, 10, 4.0, 5.5)
    own_dots, _, _ = soft_blobs(ctx, 10, 4.0, 5.5)
    dots = blend_fields(common_dots, own_dots, ctx.interior_mask)
    rgb = mix(rgb, p["dot"], dots, 0.7)
    return rgb, None
