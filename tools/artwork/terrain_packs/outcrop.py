# SPDX-License-Identifier: GPL-3.0-or-later
"""The outcrop terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    apply_cell_jitter,
    band,
    blend_fields,
    block_worley,
    cell_jitter,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    ridge_mark,
    shade,
    smoothstep,
    stamp_disc,
)


@recipe(name="outcrop", group="ridges", label="Outcrop", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={"dark": (80, 80, 76), "mid": (110, 112, 108), "light": (140, 142, 136), "lichen": (120, 130, 70)},
        style=Style(luma=108, std=12, grain_max=10), periodic=True, grid=8, blocks=4,
        note="placeholder until the image-generated outcrop lands")
def render_outcrop(ctx):
    """Whole boundary slabs with independently cracked and weathered interiors."""
    p = RECIPES["outcrop"].palette
    f1, f2, ids, common_f1, common_f2, _ = block_worley(ctx, 5)
    common_wx = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    common_wy = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    wx = blend_fields(common_wx, fbm(ctx.rng, 4, 2, size=ctx.size), ctx.interior_mask)
    wy = blend_fields(common_wy, fbm(ctx.rng, 4, 2, size=ctx.size), ctx.interior_mask)
    common_edge = domain_warp([b - a for a, b in zip(common_f1, common_f2)], common_wx, common_wy, 6)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 6)
    common_grit = fbm(ctx.shared_rng, 10, 3, size=ctx.size)
    grit = blend_fields(common_grit, fbm(ctx.rng, 10, 3, size=ctx.size), ctx.interior_mask)
    tone = normalize_like(grit, common_grit, 0.05, 0.95)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, 5 * ctx.area_scale, 0.10))
    # Reference lighting keeps shared slab bevels identical when interior slabs move.
    rim = [smoothstep(0.0, 0.3, e) for e in edge]
    common_rim = [smoothstep(0.0, 0.3, e) for e in common_edge]
    rgb = shade(rgb, rim, 0.4, light=(-0.8, -0.8), reference_height=common_rim)
    gaps = band(edge, 0.03, 0.05)
    rgb = mix(rgb, p["dark"], gaps, 0.65)
    common_fine = lattice_noise(ctx.shared_rng, 48, size=ctx.size)
    fine = blend_fields(common_fine, lattice_noise(ctx.rng, 48, size=ctx.size), ctx.interior_mask)
    rgb = [[c * (1 + 0.10 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    common_lichen = outcrop_lichen(ctx.shared_rng, ctx.size, 10 * ctx.area_scale)
    own_lichen = outcrop_lichen(ctx.rng, ctx.size, 10 * ctx.area_scale)
    lichen = blend_fields(common_lichen, own_lichen, ctx.interior_mask)
    lichen = [l * (1 - g) for l, g in zip(lichen, gaps)]
    rgb = mix(rgb, p["lichen"], lichen, 0.75)
    return ridge_mark(ctx, rgb, lit=0.4, shadow=0.45), None


def outcrop_lichen(rng, size, count):
    """Small periodic lichen flecks, retaining their native density and scale."""
    mask = [0.0] * (size * size)
    for _ in range(count):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(mask, x, y, rng.uniform(2.6, 4.2), softness=0.6)
    return mask
