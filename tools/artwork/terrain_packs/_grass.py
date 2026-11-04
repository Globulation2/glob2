# SPDX-License-Identifier: GPL-3.0-or-later
"""The grass look shared by lush and dry grass: the same ground as the classic
grass, built from the same common-edge and per-layout fields, so it keeps the
soft grain and only shifts the colour and a few small accents."""
from terrain_core import blend_fields, domain_warp, fbm, lattice_noise, normalize_like, ramp


def grass_look(ctx, p):
    """Classic grass turf through another dark/mid/light palette."""
    common, common_grain = grass_fields(ctx.shared_rng, ctx.size)
    own, own_grain = grass_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    return [[c * (1 + 0.05 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]


def grass_fields(rng, size):
    """Small tufts over broad growth patches, using only measured color targets."""
    growth = fbm(rng, 8, 3, size=size)
    growth = domain_warp(growth, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 6)
    blades = fbm(rng, 48, 2, cells_y=24, size=size)
    height = [0.82 * a + 0.18 * b for a, b in zip(growth, blades)]
    return height, lattice_noise(rng, 48, size=size)
