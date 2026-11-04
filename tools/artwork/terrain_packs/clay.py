# SPDX-License-Identifier: GPL-3.0-or-later
"""The clay terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    band,
    barren_mark,
    blend_fields,
    block_worley,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
)


@recipe(name="clay", group="barren", label="Clay", profile="soil", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (150, 76, 46), "mid": (184, 102, 64), "light": (204, 128, 88), "crack": (112, 56, 34)},
        style=Style(luma=124, std=8, grain_max=8), periodic=True, grid=8, blocks=4)
def render_clay(ctx):
    """Red baked clay in bold plates, split again by the barren group's cracks."""
    p = RECIPES["clay"].palette
    size, shared, own = ctx.size, ctx.shared_rng, ctx.rng
    f1, f2, _, common1, common2, _ = block_worley(ctx, 6)
    common_noise = fbm(shared, 4, 3, size=size)
    noise = blend_fields(common_noise, fbm(own, 4, 3, size=size), ctx.interior_mask)
    common_x, common_y = fbm(shared, 4, 2, size=size), fbm(shared, 4, 2, size=size)
    wx = blend_fields(common_x, fbm(own, 4, 2, size=size), ctx.interior_mask)
    wy = blend_fields(common_y, fbm(own, 4, 2, size=size), ctx.interior_mask)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 5)
    common_plates_raw = domain_warp([1 - v for v in common1], common_x, common_y, 5)
    plates_raw = domain_warp([1 - v for v in f1], wx, wy, 5)
    common_plates = normalize_like(common_plates_raw, common_plates_raw)
    plates = normalize_like(plates_raw, common_plates_raw)
    common_tone = [0.6 * a + 0.4 * b for a, b in zip(common_noise, common_plates)]
    tone = normalize_like([0.6 * a + 0.4 * b for a, b in zip(noise, plates)], common_tone)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grit = lattice_noise(shared, 48, size=size)
    grit = blend_fields(common_grit, lattice_noise(own, 48, size=size), ctx.interior_mask)
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    cracks = band(edge, 0.04, 0.07)
    rgb = mix(rgb, p["crack"], cracks, 0.7)
    return barren_mark(ctx, rgb, p["crack"], cells=9, fade=0.1), None
