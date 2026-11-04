# SPDX-License-Identifier: GPL-3.0-or-later
"""The chasm terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    blend_fields,
    block_worley,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    shade,
    smoothstep,
)


@recipe(name="chasm", group="void", label="Chasm", profile="crisp", border_class="cut",
        seam={"height": 8, "cast_q8": 96, "cast_width_q8": 512},
        palette={"dark": (26, 20, 24), "mid": (46, 38, 42), "light": (74, 64, 66), "floor": (6, 4, 8)},
        style=Style(luma=40, std=9, grain_max=8), periodic=True, grid=8, blocks=4,
        preview=(48, 38, 44), minimap=(30, 22, 28))
def render_chasm(ctx):
    p = RECIPES["chasm"].palette
    # Boundary channels retain the same actual Voronoi cells in every layout.
    # Interior cells move without cross-fading unrelated rock-wall images.
    f1, _, ids, _, _, _ = block_worley(ctx, 6, metric="angular", stretch=2.6)
    count = 6 * ctx.area_scale
    deep = [1.0 if k < 2 * ctx.area_scale else 0.0
            for k in ctx.shared_rng.sample(range(count), count)]
    if ctx.variant == 0:
        ctx.rng.setstate(ctx.shared_rng.getstate())
    common, common_grain = chasm_fields(ctx.shared_rng, ctx.size)
    own, own_grain = chasm_fields(ctx.rng, ctx.size)
    h = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.03, 0.97)
    common_h = normalize_like(common, common, 0.03, 0.97)
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, h, 0.7, light=(-1.0, 0.0), reference_height=common_h)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    floor = [(1 - smoothstep(0.18, 0.34, d)) * deep[i] for d, i in zip(f1, ids)]
    rgb = mix(rgb, p["floor"], floor, 0.9)
    return rgb, None


def chasm_fields(rng, size):
    """Periodic layered rock walls and fine grain at the original scale."""
    strata = domain_warp(fbm(rng, 2, 3, cells_y=6, size=size),
                         fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8, 3)
    base = fbm(rng, 4, 3, size=size)
    height = [0.6 * s + 0.4 * b for s, b in zip(strata, base)]
    fine = lattice_noise(rng, 48, size=size)
    return height, fine
