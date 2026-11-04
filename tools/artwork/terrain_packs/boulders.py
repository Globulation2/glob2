# SPDX-License-Identifier: GPL-3.0-or-later
"""The boulders terrain material."""
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
    stamp_disc,
)


@recipe(name="boulders", group="obstacles", label="Boulders", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"ground_dark": (112, 100, 84), "ground": (128, 114, 96), "ground_light": (144, 130, 110),
                 "rock_dark": (84, 88, 92), "rock_light": (138, 142, 142), "shadow": (40, 36, 34)},
        style=Style(luma=108, std=14, grain_max=12, match=0.5),
        periodic=True, grid=8, blocks=4,
        note="placeholder until the image-generated boulders land")
def render_boulders(ctx):
    """Dusty ground under the raised boulder decor: quiet warm earth with fine
    grit and a few faint stones, so the decor sprites carry the boulders."""
    p = RECIPES["boulders"].palette
    common, common_grit, common_pebbles = boulders_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_pebbles = boulders_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["ground_dark"]), (0.5, p["ground"]), (1.0, p["ground_light"])])
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = [[c * (1 + 0.1 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    pebbles = blend_fields(common_pebbles, own_pebbles, ctx.interior_mask)
    rgb = mix(rgb, p["ground_dark"], pebbles, 0.35)
    return rgb, None


def boulders_fields(rng, size):
    """Periodic dusty earth and faint pebbles under the unchanged raised decor."""
    height = fbm(rng, 6, 3, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 6)
    grit = lattice_noise(rng, 48, size=size)
    pebbles = [0.0] * (size * size)
    for _ in range(10 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = rng.uniform(2.5, 4.0)
        rng.random()  # Retain soft_blobs' per-disc choice draw.
        stamp_disc(pebbles, x, y, radius, softness=0.75)
    return height, grit, pebbles
