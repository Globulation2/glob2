# SPDX-License-Identifier: GPL-3.0-or-later
"""The sand look shared by every sand material: the same ground as the classic
sand, built from the same common-edge and per-layout fields, so it keeps the
speckled grain and only shifts the colour and a few small accents."""
from terrain_core import (
    N,
    blend_fields,
    block_worley,
    domain_warp,
    fbm,
    lattice_noise,
    normalize_like,
    ramp,
    shade,
    smoothstep,
    stamp_disc,
)


def sand_look(ctx, p):
    """Sand dunes and lit pebble grains through a palette, and its flecks."""
    common, common_flecks = sand_fields(ctx.shared_rng, ctx.size)
    own, own_flecks = sand_fields(ctx.rng, ctx.size)
    # Lit grains one or two native pixels across, half the size of the
    # original tiles' grains, rather than independent single-pixel noise.
    count = SAND_GRAINS
    first, second, ids, common_first, common_second, common_ids = block_worley(ctx, count)
    tones = [ctx.shared_rng.random() for _ in range(count * ctx.area_scale)]
    def grains(f1, f2, cells):
        return [smoothstep(0.0, 0.35, b - a) * (0.55 + 0.45 * tones[i]) for a, b, i in zip(f1, f2, cells)]
    own_grains, common_grains = grains(first, second, ids), grains(common_first, common_second, common_ids)
    surface = [SAND_DUNES * s + (1 - SAND_DUNES) * g for s, g in zip(blend_fields(common, own, ctx.interior_mask), own_grains)]
    reference = [SAND_DUNES * s + (1 - SAND_DUNES) * g for s, g in zip(common, common_grains)]
    height = normalize_like(surface, reference, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, own_grains, SAND_BEVEL, reference_height=common_grains)
    return rgb, blend_fields(common_flecks, own_flecks, ctx.interior_mask)


SAND_GRAINS, SAND_DUNES, SAND_BEVEL = 640, 0.25, 0.15


def sand_fields(rng, size):
    """Dunes, native-pixel grains and sparse sub-pixel mineral flecks."""
    dunes = fbm(rng, 3, 3, size=size)
    dunes = domain_warp(dunes, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    grains = lattice_noise(rng, 32, size=size)
    speckle = lattice_noise(rng, 64, size=size)
    height = [0.24 * a + 0.66 * b + 0.10 * c for a, b, c in zip(dunes, grains, speckle)]
    flecks = [0.0] * (size * size)
    for _ in range(5 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(flecks, x, y, rng.uniform(1.4, 2.4), softness=0.7)
    return height, flecks
