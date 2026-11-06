# SPDX-License-Identifier: GPL-3.0-or-later
"""Family helper: ground strewn with small loose stones, under raised decor.

The Barriers masses (sandstone benches, obsidian ridge, granite tors,
travertine terraces) stand on it with their contact shadows; the Outcrops looks
(penitentes, termite mounds, hoodoos, petrified stumps) add the group's
ridge_mark ledges.
"""
from terrain_core import (
    Ctx,
    clamp01,
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
    common_raw = fbm(shared_rng, 4, 3, size=ctx.size)
    own_raw = fbm(own_rng, 4, 3, size=ctx.size)
    ordered = sorted(common_raw)
    low, high = ordered[int(0.02 * (len(ordered) - 1))], ordered[int(0.98 * (len(ordered) - 1))]
    span = high - low or 1.0
    common_height = [clamp01((v - low) / span) for v in common_raw]
    own_height = [clamp01((v - low) / span) for v in own_raw]
    interior = ctx.interior_mask
    height = [a + (b - a) * w for a, b, w in zip(common_height, own_height, interior)]
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(own_rng, 48, size=ctx.size)
    fine = [a + (b - a) * w for a, b, w in zip(common_grain, own_grain, interior)]
    rgb = [[c * (1 + grain * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    rgb = shade(rgb, height, 0.14, light=(-0.6, -0.8), reference_height=common_height)
    shared_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared_ctx.rng = shared_rng
    common_halo, common_core, _ = soft_blobs(shared_ctx, chips, *chip_radius, core=1.5)
    own_halo, own_core, _ = soft_blobs(ctx, chips, *chip_radius, core=1.5)
    halo = [a + (b - a) * w for a, b, w in zip(common_halo, own_halo, interior)]
    core = [a + (b - a) * w for a, b, w in zip(common_core, own_core, interior)]
    rgb = mix(rgb, p["stone"], halo, 0.75)
    rgb = mix(rgb, p["stone_core"], core, 0.6)
    if ledges:
        rgb = ridge_mark(ctx, rgb)
    return rgb, None
