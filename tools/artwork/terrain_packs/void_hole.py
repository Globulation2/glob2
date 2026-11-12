# SPDX-License-Identifier: GPL-3.0-or-later
"""The void hole terrain material.

Void: nothing crosses, nothing flies. The depth cue is a cast onto every
neighbour (rank above all other materials), since a hole this dark cannot
show a lip of its own."""
from terrain_core import RECIPES, Style, blend_fields, fbm, normalize_like, ramp, recipe


@recipe(name="void_hole", group="void", label="Hole", profile="crisp", border_class="cut",
        seam={"height": 8, "cast_q8": 96, "cast_width_q8": 512},
        palette={"dark": (6, 6, 10), "mid": (10, 10, 14), "light": (14, 14, 20)},
        style=Style(luma=10, std=2, grain_max=3), periodic=True, grid=8, blocks=4,
        preview=(28, 24, 36), minimap=(16, 14, 22))
def render_void_hole(ctx):
    p = RECIPES["void_hole"].palette
    common = fbm(ctx.shared_rng, 3, 2, size=ctx.size, array=True)
    own = fbm(ctx.rng, 3, 2, size=ctx.size, array=True)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    # Keep the hole quiet and nearly black, with only subtle interior depth.
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    return rgb, None
