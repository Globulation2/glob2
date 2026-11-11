# SPDX-License-Identifier: GPL-3.0-or-later
"""The hedge terrain material."""
import numpy as np

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


@recipe(name="hedge", group="obstacles", label="Hedge", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"dark": (22, 50, 24), "mid": (36, 76, 36), "light": (64, 108, 52), "gap": (10, 26, 14)},
        style=Style(luma=56, std=10, grain_max=9), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_hedge(ctx):
    """Leaf litter and dark soil under the raised hedge decor."""
    p = RECIPES["hedge"].palette
    common, common_grit, common_leaves = hedge_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_leaves = hedge_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["gap"]), (0.5, p["dark"]), (1.0, p["mid"])])
    grit = blend_fields(common_grit, own_grit, ctx.interior)
    rgb = rgb * (1 + 0.14 * (grit - 0.5) * 2)
    leaves = blend_fields(common_leaves, own_leaves, ctx.interior)
    rgb = mix(rgb, p["light"], leaves, 0.25)
    return rgb, None


def hedge_fields(rng, size):
    """Periodic litter height, grain and leaf flecks beneath the raised hedge."""
    height = fbm(rng, 8, 3, size=size, array=True)
    height = domain_warp(height, fbm(rng, 3, 2, size=size, array=True),
                         fbm(rng, 3, 2, size=size, array=True), 8)
    grit = lattice_noise(rng, 48, size=size, array=True)
    leaves = np.zeros(size * size)
    for _ in range(14 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = rng.uniform(2.5, 4.5)
        rng.random()  # Retain soft_blobs' per-disc choice draw.
        stamp_disc(leaves, x, y, radius, softness=0.75)
    return height, grit, leaves
