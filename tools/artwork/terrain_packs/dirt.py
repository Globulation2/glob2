# SPDX-License-Identifier: GPL-3.0-or-later
"""The dirt terrain material."""
import numpy as np

from terrain_core import (
    N,
    RECIPES,
    Style,
    barren_mark,
    blend_fields,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    stamp_disc,
)


@recipe(name="dirt", group="barren", label="Dirt", profile="soil", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (104, 82, 56), "mid": (136, 108, 74), "light": (160, 132, 96), "pebble": (86, 68, 48),
                 "crack": (66, 48, 32)},
        style=Style(luma=116, std=7, grain_max=9), periodic=True, grid=8, blocks=4)
def render_dirt(ctx):
    """The original speckled earth and stones, under the barren group's mark."""
    p = RECIPES["dirt"].palette
    common, common_grain, common_pebbles = dirt_fields(ctx.shared_rng, ctx.size)
    own, own_grain, own_pebbles = dirt_fields(ctx.rng, ctx.size)
    # A shared reference range keeps the common earth and tiny stones at
    # exactly the same tone, independent of each layout's interior statistics.
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common,
                            low=0.02, high=0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = rgb * (1 + 0.12 * (fine - 0.5) * 2)
    pebbles = blend_fields(common_pebbles, own_pebbles, ctx.interior_mask)
    rgb = mix(rgb, p["pebble"], pebbles, 0.8)
    return barren_mark(ctx, rgb, p["crack"], fade=0.0), None


def dirt_fields(rng, size):
    """Periodic earth, fine grain and pebble masks at the original density."""
    height = fbm(rng, 5, 4, size=size, array=True)
    height = domain_warp(height, fbm(rng, 3, 2, size=size, array=True), fbm(rng, 3, 2, size=size, array=True), 10)
    fine = lattice_noise(rng, 48, size=size, array=True)
    pebbles = np.zeros(size * size)
    for _ in range(40 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(pebbles, x, y, rng.uniform(1.6, 2.6), softness=0.6)
    return height, fine, pebbles
