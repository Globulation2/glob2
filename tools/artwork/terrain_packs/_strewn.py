# SPDX-License-Identifier: GPL-3.0-or-later
"""Family helper: ground strewn with small loose stones, under raised decor.

The Barriers masses (sandstone benches, obsidian ridge, granite tors,
travertine terraces) stand on it with their contact shadows; the Outcrops looks
(penitentes, termite mounds, hoodoos, petrified stumps) add the group's
ridge_mark ledges.
"""
import numpy as np

from terrain_core import (
    Ctx,
    blend_fields,
    fbm,
    lattice_noise,
    mix,
    ramp,
    ridge_mark,
    shade,
    soft_blobs,
)


def strewn_ground(ctx, p, chips=10, chip_radius=(2.5, 4.5), grain=0.06, ledges=False):
    """A soft ramped base with fine grain and hand-sized loose stones.

    Common and own fields blend through the interior mask, so the shared block
    perimeter stays identical while interiors vary."""
    shared_rng, own_rng = ctx.shared_rng, ctx.rng
    common_raw = fbm(shared_rng, 4, 3, size=ctx.size, array=True)
    own_raw = fbm(own_rng, 4, 3, size=ctx.size, array=True)
    ordered = np.sort(common_raw)
    low, high = ordered[int(0.02 * (len(ordered) - 1))], ordered[int(0.98 * (len(ordered) - 1))]
    span = high - low or 1.0
    common_height = np.clip((common_raw - low) / span, 0.0, 1.0)
    own_height = np.clip((own_raw - low) / span, 0.0, 1.0)
    interior = ctx.interior
    height = blend_fields(common_height, own_height, interior)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared_rng, 48, size=ctx.size, array=True)
    own_grain = lattice_noise(own_rng, 48, size=ctx.size, array=True)
    fine = blend_fields(common_grain, own_grain, interior)
    rgb = rgb * (1 + grain * (fine - 0.5) * 2)
    rgb = shade(rgb, height, 0.14, light=(-0.6, -0.8), reference_height=common_height)
    shared_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared_ctx.rng = shared_rng
    common_halo, common_core, _ = soft_blobs(shared_ctx, chips, *chip_radius, core=1.5, array=True)
    own_halo, own_core, _ = soft_blobs(ctx, chips, *chip_radius, core=1.5, array=True)
    halo = blend_fields(common_halo, own_halo, interior)
    core = blend_fields(common_core, own_core, interior)
    rgb = mix(rgb, p["stone"], halo, 0.75)
    rgb = mix(rgb, p["stone_core"], core, 0.6)
    if ledges:
        rgb = ridge_mark(ctx, rgb)
    return rgb, None
