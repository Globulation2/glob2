# SPDX-License-Identifier: GPL-3.0-or-later
"""The ice terrain material."""
import numpy as np

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
    shade,
)


@recipe(name='ice', group="legacy", label='Ice', profile="frozen", border_class="solid",
        seam={'height': 4, 'cast_q8': 64, 'cast_width_q8': 640, 'fringe': [228, 240, 248], 'fringe_q8': 96, 'fringe_width_q8': 384}, palette={'dark': (186, 214, 231), 'mid': (195, 221, 235), 'light': (208, 229, 241)}, style=Style(luma=215.4, std=6.2, grain_max=5.6),
        periodic=True, grid=8, blocks=4, preview=(190, 225, 240), minimap=(190, 225, 240))
def render_ice(ctx):
    """Quiet blue frozen ground, with common whorls and interior frost grain."""
    p = RECIPES["ice"].palette
    common, common_frost, common_grain = ice_fields(ctx.shared_rng, ctx.size)
    own, own_frost, own_grain = ice_fields(ctx.rng, ctx.size)
    common_height = normalize_like(common, common, 0.02, 0.98)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, height, 0.035, light=(-0.6, -0.8), reference_height=common_height)
    frost = normalize_like(blend_fields(common_frost, own_frost, ctx.interior_mask), common_frost)
    rgb = mix(rgb, p["light"], frost, 0.10)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = rgb * (1 + 0.025 * (fine - 0.5) * 2)
    return rgb, None


def ice_fields(rng, size):
    """Periodic glassy whorls, soft crystalline frost and fine cold grain."""
    height = fbm(rng, 3, 3, cells_y=6, size=size, array=True)
    height = domain_warp(height, fbm(rng, 2, 2, size=size, array=True),
                         fbm(rng, 2, 2, size=size, array=True), 10, 6)
    frost = np.abs(2 * fbm(rng, 12, 2, size=size, array=True) - 1)
    fine = lattice_noise(rng, 48, size=size, array=True)
    return height, frost, fine
