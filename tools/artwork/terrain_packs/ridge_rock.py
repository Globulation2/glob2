# SPDX-License-Identifier: GPL-3.0-or-later
"""The ridge rock terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    blend_fields,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    ridge_mark,
    shade,
    smoothstep,
)


@recipe(name="ridge_rock", group="ridges", label="Ridge", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={"dark": (72, 72, 76), "mid": (104, 104, 106), "light": (138, 138, 136), "crack": (46, 46, 50)},
        style=Style(luma=102, std=12, grain_max=10), periodic=True, grid=8, blocks=4)
def render_ridge_rock(ctx):
    p = RECIPES["ridge_rock"].palette
    common, common_grain, common_ridges, common_widths = ridge_rock_fields(ctx.shared_rng, ctx.size)
    own, own_grain, own_ridges, own_widths = ridge_rock_fields(ctx.rng, ctx.size)
    weight = ctx.interior_mask
    # Shared scalar strata and crack geometry join before palette mapping;
    # the common stretch and lighting scale keep the perimeter unchanged.
    h = normalize_like(blend_fields(common, own, weight), common, 0.03, 0.97)
    common_h = normalize_like(common, common, 0.03, 0.97)
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, weight)
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    ridged = blend_fields(common_ridges, own_ridges, weight)
    widths = blend_fields(common_widths, own_widths, weight)
    cracks = [1 - smoothstep(0.0, 0.05 + 0.10 * w, r) for r, w in zip(ridged, widths)]
    rgb = shade(rgb, h, 0.2, light=(-1.0, -0.3), reference_height=common_h)
    rgb = mix(rgb, p["crack"], cracks, 0.3)
    return ridge_mark(ctx, rgb), None


def ridge_rock_fields(rng, size):
    """Periodic strata, grain and sparse variable-width cracks at native scale."""
    strata = fbm(rng, 2, 3, cells_y=8, gain=0.4, size=size)
    strata = domain_warp(strata, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 12, 3)
    base = fbm(rng, 5, 3, size=size)
    height = [0.75 * s + 0.25 * b for s, b in zip(strata, base)]
    fine = lattice_noise(rng, 48, size=size)
    ridged = [abs(2 * v - 1) for v in domain_warp(
        fbm(rng, 2, 2, cells_y=5, size=size),
        fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8, 3)]
    widths = fbm(rng, 3, 2, size=size)
    return height, fine, ridged, widths
