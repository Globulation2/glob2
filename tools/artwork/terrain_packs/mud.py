# SPDX-License-Identifier: GPL-3.0-or-later
"""The mud terrain material."""
from terrain_core import (
    N,
    RECIPES,
    Style,
    clamp01,
    domain_warp,
    fbm,
    lattice_noise,
    lumps,
    mix,
    ramp,
    recipe,
    shade,
    stamp_disc,
    wet_pits,
)


@recipe(name="mud", group="rough", label="Mud", profile="soil", border_class="solid",
        seam={"height": 1},
        palette={"dark": (60, 49, 38), "mid": (86, 71, 54), "light": (114, 97, 76), "sheen": (148, 133, 114),
                 "water": (46, 38, 32), "glint": (168, 160, 150)},
        style=Style(luma=76, std=11, grain_max=8), periodic=True, grid=8, blocks=4)
def render_mud(ctx):
    """Churned heavy lumps about a fifth of a tile across and water-filled
    pits with a wet sheen, at the sizes a glob would see underfoot."""
    p = RECIPES["mud"].palette
    common, common_grit, common_pools = mud_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_pools = mud_fields(ctx.rng, ctx.size)
    # One reference range for all layouts keeps their common perimeter exact.
    # Blend the height and wetness fields before colouring rather than fading
    # between finished images; pools and mud contours can cross cell edges.
    ordered = sorted(common)
    low = ordered[int(0.02 * (len(ordered) - 1))]
    high = ordered[int(0.98 * (len(ordered) - 1))]
    span = max(high - low, 1e-9)
    height = [clamp01((a + (b - a) * w - low) / span)
              for a, b, w in zip(common, own, ctx.interior_mask)]
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    grit = [a + (b - a) * w for a, b, w in zip(common_grit, own_grit, ctx.interior_mask)]
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    pools = [a + (b - a) * w for a, b, w in zip(common_pools, own_pools, ctx.interior_mask)]
    rgb = mix(rgb, p["sheen"], pools, 0.25)
    height, common_height, pit, _ = lumps(ctx, 9, pits=0.25, warp=12)
    rgb = shade(rgb, height, 0.5, reference_height=common_height)
    return wet_pits(rgb, height, common_height, pit, p["water"], p["glint"]), None


def mud_fields(rng, size):
    """Periodic mud height, grain and wet pools, a little finer than the
    original feature scale."""
    height = fbm(rng, 8, 4, size=size)
    height = domain_warp(height, fbm(rng, 5, 2, size=size), fbm(rng, 5, 2, size=size), 8)
    grit = lattice_noise(rng, 48, size=size)
    pools = [0.0] * (size * size)
    # Pools may wrap around the block, so shared boundary pools remain whole.
    for _ in range(rng.randint(3, 5) * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(pools, x, y, rng.uniform(8, 13), softness=0.85)
    return height, grit, pools
