# SPDX-License-Identifier: GPL-3.0-or-later
"""The loam terrain material."""
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


@recipe(name="loam", group="fertile", label="Loam", profile="soil", border_class="solid",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (44, 26, 14), "mid": (80, 50, 26), "light": (112, 76, 42), "fleck": (92, 124, 44)},
        style=Style(luma=60, std=9, grain_max=8), periodic=True, grid=8, blocks=4)
def render_loam(ctx):
    """Common periodic soil and flecks at the perimeter, varied interior beds."""
    p = RECIPES["loam"].palette
    common, common_grit, common_flecks = loam_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_flecks = loam_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = rgb * (1 + 0.12 * (grit - 0.5) * 2)
    # Green-brown flecks remain soft, sparse and about two native pixels wide.
    flecks = blend_fields(common_flecks, own_flecks, ctx.interior_mask)
    return mix(rgb, p["fleck"], flecks, 0.8), None


def loam_fields(rng, size):
    """Periodic loam height, fine grain and flecks at the existing native scale."""
    height = fbm(rng, 5, 4, size=size, array=True)
    height = domain_warp(height, fbm(rng, 3, 2, size=size, array=True), fbm(rng, 3, 2, size=size, array=True), 8)
    grit = lattice_noise(rng, 48, size=size, array=True)
    flecks = np.zeros(size * size)
    for _ in range(22 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = 4.0 + rng.random() * 1.5
        rng.random()  # Retain the per-disc choice draw used by soft_blobs.
        stamp_disc(flecks, x, y, radius, softness=0.75)
    return height, grit, flecks
