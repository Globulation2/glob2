# SPDX-License-Identifier: GPL-3.0-or-later
"""The grass terrain material.

Legacy surfaces keep their original palette/style statistics; grass is the
exception, redrawn as sunlit turf in natural greens (minimap and preview keep
the classic colours)."""
from terrain_core import (
    N,
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
    smoothstep,
)


@recipe(name='grass', group="legacy", label='Grass', profile="turf", border_class="solid",
        seam={'height': 2, 'cast_q8': 56, 'cast_width_q8': 512},
        palette={'shadow': (30, 74, 38), 'dark': (44, 98, 40), 'mid': (70, 124, 46), 'light': (112, 150, 62),
                 'warm': (138, 150, 66), 'cool': (36, 92, 64)},
        style=Style(luma=88, std=8, grain_max=8),
        periodic=True, grid=8, blocks=4, preview=(30, 113, 30), minimap=(0, 90, 0))
def render_grass(ctx):
    """Fine tufts over soft growth: blue-green hollows, yellow-green crowns and
    broad sun/moisture drift, with gentle top-left relief."""
    p, size = RECIPES["grass"].palette, ctx.size

    def growth(rng):
        g = fbm(rng, 6, 4, size=size, array=True)
        g = domain_warp(g, fbm(rng, 3, 2, size=size, array=True), fbm(rng, 3, 2, size=size, array=True),
                        1.5 * size / N)
        tufts = fbm(rng, 64, 2, cells_y=24, size=size, array=True)
        return 0.6 * g + 0.4 * tufts

    common_raw = growth(ctx.shared_rng)
    common = normalize_like(common_raw, common_raw, 0.02, 0.98)
    height = normalize_like(blend_fields(common_raw, growth(ctx.rng), ctx.interior_mask), common_raw, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p['shadow']), (0.35, p['dark']), (0.7, p['mid']), (1.0, p['light'])])
    rgb = shade(rgb, height, 0.16, reference_height=common)
    # Broad warm/cool drift, a cell or two across, like sun and soil moisture.
    common_drift = fbm(ctx.shared_rng, 1.5, 3, size=size, array=True)
    drift = normalize_like(blend_fields(common_drift, fbm(ctx.rng, 1.5, 3, size=size, array=True),
                                        ctx.interior_mask), common_drift)
    rgb = mix(rgb, p['warm'], smoothstep(0.55, 0.9, drift), 0.55 * 0.35)
    rgb = mix(rgb, p['cool'], smoothstep(0.45, 0.1, drift), 0.55 * 0.3)
    fine = blend_fields(lattice_noise(ctx.shared_rng, 64, size=size, array=True),
                        lattice_noise(ctx.rng, 64, size=size, array=True), ctx.interior_mask)
    return rgb * (1 + 0.07 * (fine - 0.5) * 2), None
