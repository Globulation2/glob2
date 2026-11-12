# SPDX-License-Identifier: GPL-3.0-or-later
"""The turf shared by lush and dry grass: the sunlit turf of grass (fine tufts
over soft growth, a four-stop ramp, top-left relief and broad warm/cool drift),
with its density, blade shape and drift exposed so each look can make the turf
its own before adding its accents."""
from terrain_core import (
    N,
    Ctx,
    blend_fields,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    rgb_field,
    shade,
    smoothstep,
)


def layout(ctx, make):
    """`make(rng)` from the shared stream at the block edge, varied inside."""
    return blend_fields(make(ctx.shared_rng), make(ctx.rng), ctx.interior_mask)


def shared_ctx(ctx):
    """A context drawing from the shared stream, for stamps on the block edge."""
    shared = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared.rng = ctx.shared_rng
    return shared


def turf(ctx, p, *, broad=6, tuft_cells=64, tuft_y=24, tuft_w=0.4, relief=0.16, warm=0.35, cool=0.3, comb=0.0):
    """Growth and tufts through shadow/dark/mid/light, lit from the top left,
    drifting toward `warm` and `cool`. `comb` bends the blades along a slowly
    turning direction. Returns (rgb, height)."""
    size = ctx.size

    def growth(rng):
        g = fbm(rng, broad, 4, size=size, array=True)
        g = domain_warp(g, fbm(rng, 3, 2, size=size, array=True), fbm(rng, 3, 2, size=size, array=True),
                        1.5 * size / N)
        tufts = fbm(rng, tuft_cells, 2, cells_y=tuft_y, size=size, array=True)
        if comb:
            tufts = domain_warp(tufts, fbm(rng, 2, 2, size=size, array=True), fbm(rng, 2, 2, size=size, array=True),
                                comb * size / N)
        return (1 - tuft_w) * g + tuft_w * tufts

    common_raw = growth(ctx.shared_rng)
    common = normalize_like(common_raw, common_raw, 0.02, 0.98)
    height = normalize_like(blend_fields(common_raw, growth(ctx.rng), ctx.interior_mask), common_raw, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["shadow"]), (0.35, p["dark"]), (0.7, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, height, relief, reference_height=common)
    # Broad warm/cool drift, a cell or two across, like sun and soil moisture.
    common_drift = fbm(ctx.shared_rng, 1.5, 3, size=size, array=True)
    drift = normalize_like(blend_fields(common_drift, fbm(ctx.rng, 1.5, 3, size=size, array=True),
                                        ctx.interior_mask), common_drift)
    rgb = mix(rgb, p["warm"], smoothstep(0.55, 0.9, drift), 0.55 * warm)
    rgb = mix(rgb, p["cool"], smoothstep(0.45, 0.1, drift), 0.55 * cool)
    return rgb, height


def fine_grain(ctx, rgb, amount):
    """About one native pixel of grain over the finished colour."""
    fine = layout(ctx, lambda rng: lattice_noise(rng, 64, size=ctx.size, array=True))
    return rgb_field(rgb) * (1 + amount * (fine - 0.5) * 2)
