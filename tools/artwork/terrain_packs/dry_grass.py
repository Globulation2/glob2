# SPDX-License-Identifier: GPL-3.0-or-later
"""The dry grass terrain material."""
from terrain_core import RECIPES, Style, blend_fields, fbm, mix, recipe, smoothstep
from ._grass import grass_look


@recipe(name="dry_grass", group="grass", label="Dry grass", profile="turf", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (92, 100, 36), "mid": (120, 124, 52), "light": (150, 146, 74), "straw": (176, 164, 96)},
        style=Style(luma=114, std=7, grain_max=7), periodic=True, grid=8, blocks=4)
def render_dry_grass(ctx):
    """Classic grass grain in olive, with short pale straw streaks."""
    p, size = RECIPES["dry_grass"].palette, ctx.size
    rgb = grass_look(ctx, p)
    # Streaks: noise stretched along x, kept to its brightest ridges.
    straw = blend_fields(fbm(ctx.shared_rng, 40, 2, cells_y=10, size=size),
                         fbm(ctx.rng, 40, 2, cells_y=10, size=size), ctx.interior_mask)
    return mix(rgb, p["straw"], [smoothstep(0.66, 0.8, v) for v in straw], 0.5), None
