# SPDX-License-Identifier: GPL-3.0-or-later
"""The lush grass terrain material."""
from terrain_core import RECIPES, Style, blend_fields, fbm, mix, recipe, smoothstep
from ._grass import grass_look


@recipe(name="lush_grass", group="grass", label="Lush grass", profile="turf", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (14, 80, 40), "mid": (26, 104, 52), "light": (48, 128, 64), "clump": (10, 66, 34)},
        style=Style(luma=72, std=6, grain_max=6), periodic=True, grid=8, blocks=4)
def render_lush_grass(ctx):
    """Classic grass grain in deeper greens, with darker clumps for body."""
    p, size = RECIPES["lush_grass"].palette, ctx.size
    rgb = grass_look(ctx, p)
    clumps = blend_fields(fbm(ctx.shared_rng, 18, 2, size=size), fbm(ctx.rng, 18, 2, size=size),
                          ctx.interior_mask)
    return mix(rgb, p["clump"], [smoothstep(0.58, 0.74, v) for v in clumps], 0.45), None
