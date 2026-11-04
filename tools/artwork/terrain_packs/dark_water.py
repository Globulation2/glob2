# SPDX-License-Identifier: GPL-3.0-or-later
"""The dark water terrain material."""
from terrain_core import (
    N,
    RECIPES,
    Style,
    blend_fields,
    fbm,
    flat,
    mix,
    normalize_like,
    recipe,
    stamp_disc,
)


@recipe(name="dark_water", group="deep_water", label="Dark water", profile="still", border_class="pool",
        seam={"height": 7, "cast_q8": 80, "cast_width_q8": 704},
        palette={"tint": (40, 30, 104), "murk": (32, 24, 84), "speck": (20, 15, 52)},
        style=Style(luma=38, std=4, grain_max=5, match=0.5), periodic=True, grid=8, blocks=4,
        preview=(32, 26, 76), minimap=(16, 12, 52))
def render_dark_water(ctx):
    p = RECIPES["dark_water"].palette
    common, common_specks = dark_water_fields(ctx.shared_rng, ctx.size)
    own, own_specks = dark_water_fields(ctx.rng, ctx.size)
    murk = normalize_like(blend_fields(common, own, ctx.interior_mask), common)
    rgb = mix(flat(p["tint"], size=ctx.size), p["murk"], murk, 0.6)
    specks = blend_fields(common_specks, own_specks, ctx.interior_mask)
    rgb = mix(rgb, p["speck"], specks, 0.6)
    return rgb, None


def dark_water_fields(rng, size):
    """Static periodic murk and sparse soft specks at the native feature scale."""
    murk = fbm(rng, 3, 2, size=size)
    specks = [0.0] * (size * size)
    for _ in range(8 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = rng.uniform(3.0, 4.5)
        rng.random()  # The original scatter's per-speck choice draw.
        stamp_disc(specks, x, y, radius, softness=0.75)
    return murk, specks
