# SPDX-License-Identifier: GPL-3.0-or-later
"""The lush grass terrain material."""
from terrain_core import RECIPES, Style, blend_fields, cast, fbm, mix, ramp, recipe, shade, smoothstep, soft_blobs
from ._grass import fine_grain, shared_ctx, turf


@recipe(name="lush_grass", group="grass", label="Lush grass", profile="turf", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"shadow": (14, 56, 34), "dark": (24, 82, 38), "mid": (42, 112, 44), "light": (84, 146, 56),
                 "warm": (92, 140, 44), "cool": (18, 76, 56),
                 "leaf": (38, 116, 58), "leaf_lit": (86, 160, 80), "leaf_shade": (14, 62, 40)},
        style=Style(luma=76, std=9, grain_max=9), periodic=True, grid=8, blocks=4)
def render_lush_grass(ctx):
    """Clover sward: dense deep-green turf with low rounded leaf cushions,
    lit from the top left and casting short shadows."""
    p, size = RECIPES["lush_grass"].palette, ctx.size
    rgb, _ = turf(ctx, p, broad=7, tuft_cells=80, tuft_y=24, tuft_w=0.45, relief=0.16)
    common, _, _ = soft_blobs(shared_ctx(ctx), 4, 14.0, 22.0, array=True)
    own, _, _ = soft_blobs(ctx, 4, 14.0, 22.0, array=True)
    leaves = blend_fields(common, own, ctx.interior_mask)
    # Lobes break each cushion into leaves rather than a smooth disc.
    common_lobes = fbm(ctx.shared_rng, 70, 2, size=size, array=True)
    lobes = blend_fields(common_lobes, fbm(ctx.rng, 70, 2, size=size, array=True), ctx.interior_mask)

    def cushions(blobs, lobe):
        return blobs * (0.55 + 0.45 * smoothstep(0.3, 0.7, lobe))

    cushion = cushions(leaves, lobes)
    leaf = ramp(cushion, [(0.0, p["leaf_shade"]), (0.5, p["leaf"]), (1.0, p["leaf_lit"])])
    # Light against the common cushions so every layout's boundary matches.
    leaf = shade(leaf, cushion, 0.3, reference_height=cushions(common, common_lobes))
    rgb = mix(rgb, leaf, smoothstep(0.05, 0.3, cushion), 0.9)
    rgb = cast(rgb, cushion, p["leaf_shade"], reach=4, strength=0.45)
    return fine_grain(ctx, rgb, 0.07), None
