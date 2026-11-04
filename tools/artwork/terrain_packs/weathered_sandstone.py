# SPDX-License-Identifier: GPL-3.0-or-later
"""The weathered sandstone terrain material."""
from terrain_core import (
    N,
    RECIPES,
    Style,
    apply_cell_jitter,
    band,
    barren_mark,
    blend_fields,
    block_worley,
    cast,
    cell_jitter,
    domain_warp,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    recipe,
    shade,
    smoothstep,
    stamp_disc,
)


@recipe(name="weathered_sandstone", group="barren", label="Weathered sandstone", profile="rock",
        border_class="solid", seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"tones": [(182, 152, 86), (170, 140, 76), (188, 162, 98), (164, 132, 72), (176, 150, 92)],
                 "rust": (150, 98, 54), "pale": (206, 192, 142), "joint": (110, 88, 56), "crack": (100, 80, 52)},
        style=Style(luma=134, std=12, grain_max=14, match=0.75), periodic=True, grid=8, blocks=4)
def render_weathered_sandstone(ctx):
    """Weathered sandstone: one solid bed of ochre stone worn into rounded,
    pillowed blocks with fine joints, faint bedding, iron stain and pale
    nodules, under the barren group's cracks."""
    p = RECIPES["weathered_sandstone"].palette
    size, shared, own = ctx.size, ctx.shared_rng, ctx.rng
    cells = 2
    f1, f2, ids, common1, common2, common_ids = block_worley(ctx, cells)
    common_x, common_y = fbm(shared, 4, 2, size=size), fbm(shared, 4, 2, size=size)
    wx = blend_fields(common_x, fbm(own, 4, 2, size=size), ctx.interior_mask)
    wy = blend_fields(common_y, fbm(own, 4, 2, size=size), ctx.interior_mask)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 10)
    common_edge = domain_warp([b - a for a, b in zip(common1, common2)], common_x, common_y, 10)
    common_wear = fbm(shared, 4, 2, size=size)
    wear = blend_fields(common_wear, fbm(own, 4, 2, size=size), ctx.interior_mask)

    def relief(e, w):
        # A broad, low dome per slab, gently undulating on top.
        return 0.9 * smoothstep(0.0, 0.7, e) + 0.1 * w

    height = [relief(e, w) for e, w in zip(edge, wear)]
    common = [relief(e, w) for e, w in zip(common_edge, common_wear)]
    count = cells * ctx.area_scale
    tones = [p["tones"][int(shared.random() * len(p["tones"]))] for _ in range(count)]
    rgb = [[tones[i][k] for i in ids] for k in range(3)]
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(shared, count, 0.05))
    bedding = layout_field(ctx, lambda rng: fbm(rng, 3, 2, cells_y=20, size=size))
    rgb = [[c * (0.93 + 0.14 * v) for c, v in zip(ch, bedding)] for ch in rgb]
    stain = layout_field(ctx, lambda rng: fbm(rng, 3, 3, size=size))
    rgb = mix(rgb, p["rust"], [smoothstep(0.52, 0.8, v) for v in stain], 0.5)
    bloom = layout_field(ctx, lambda rng: fbm(rng, 9, 3, size=size))
    rgb = mix(rgb, p["pale"], [smoothstep(0.5, 0.75, b) * smoothstep(0.55, 0.85, h) for b, h in zip(bloom, height)], 0.45)
    nodules = layout_field(ctx, lambda rng: sandstone_nodules(rng, size))
    rgb = mix(rgb, p["pale"], nodules, 0.6)
    height = [h + 0.04 * n for h, n in zip(height, nodules)]
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))
    rgb = [[c * (0.95 + 0.10 * v) for c, v in zip(ch, speck)] for ch in rgb]
    rgb = shade(rgb, height, 0.15, reference_height=common)
    rgb = cast(rgb, height, p["joint"], reach=2, strength=0.15)
    rgb = mix(rgb, p["joint"], band(edge, 0.012, 0.025), 0.85)
    return barren_mark(ctx, rgb, p["crack"], cells=4, strength=0.55, fade=0.22), None


def sandstone_nodules(rng, size):
    """Small pale concretions weathered out of the stone, 2-3 native px."""
    mask = [0.0] * (size * size)
    for _ in range(10 * (size // N) ** 2):
        stamp_disc(mask, rng.random() * size, rng.random() * size, rng.uniform(4.5, 6.5), softness=0.5)
    return mask
