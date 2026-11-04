# SPDX-License-Identifier: GPL-3.0-or-later
"""The hardpan terrain material."""
from terrain_core import (
    N,
    RECIPES,
    Style,
    apply_cell_jitter,
    blend_fields,
    block_worley,
    cell_jitter,
    domain_warp,
    fbm,
    jag,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shade,
    smoothstep,
    stamp_disc,
    wet_pits,
)


@recipe(name="hardpan", group="rough", label="Hardpan", profile="soil", border_class="solid", seam={"height": 1},
        palette={"dark": (186, 176, 164), "mid": (214, 204, 190), "light": (232, 224, 212),
                 "crack": (104, 94, 88), "dimple": (180, 170, 158), "damp": (150, 138, 124),
                 "glint": (196, 188, 176)},
        style=Style(luma=176, std=20, grain_max=14, match=0.8), periodic=True, grid=8, blocks=4)
def render_hardpan(ctx):
    """Hardpan: the rough group's heavy lumps as small, tightly packed, low
    pillows of chalky baked crust, split by thin cracks, dimpled at their
    centres, with a few cells sunk into shallow damp hollows."""
    p, size = RECIPES["hardpan"].palette, ctx.size
    # About six pillows per tile, far finer than mudflat's plates. As in
    # `lumps`, but each cell's rise is capped by its own widest F2-F1, so the
    # narrow cells of a random point clump still reach a lit top instead of
    # sinking under the crack threshold. Pits are shallow, not cliffs.
    f1, f2, ids, c1, c2, cids = block_worley(ctx, 6)
    count = 6 * ctx.area_scale
    sunk = [ctx.shared_rng.random() < 0.03 for _ in range(count)]

    def pillows(a_field, b_field, id_field):
        reach = [0.0] * count
        for a, b, i in zip(a_field, b_field, id_field):
            reach[i] = max(reach[i], b - a)
        rise = [max(0.05, min(0.35, 0.8 * r)) for r in reach]
        return [smoothstep(0.0, rise[i], b - a) * (-0.3 if sunk[i] else 1.0)
                for a, b, i in zip(a_field, b_field, id_field)]

    height, common = pillows(f1, f2, ids), pillows(c1, c2, cids)
    # Round the outlines into soft blobs; the reference takes the common warp.
    common_x, common_y = fbm(ctx.shared_rng, 4, 2, size=size), fbm(ctx.shared_rng, 4, 2, size=size)
    wx = blend_fields(common_x, fbm(ctx.rng, 4, 2, size=size), ctx.interior_mask)
    wy = blend_fields(common_y, fbm(ctx.rng, 4, 2, size=size), ctx.interior_mask)
    height, common = domain_warp(height, wx, wy, 10), domain_warp(common, common_x, common_y, 10)
    tone = layout_field(ctx, lambda rng: fbm(rng, 10, 3, size=size))
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, count, 0.04))

    def dimples(rng):
        holes = [0.0] * (size * size)
        for _ in range(15 * (size // N) ** 2):
            x, y = rng.random() * size, rng.random() * size
            stamp_disc(holes, x, y, rng.uniform(4.0, 7.0), softness=0.6)
        return holes

    # Dimples dent only the pillow centres, so the bevel lights their far rim.
    holes = layout_field(ctx, dimples)
    dent = [v * smoothstep(0.6, 0.9, h) for v, h in zip(holes, height)]
    rgb = mix(rgb, p["dimple"], dent, 0.35)
    height = [h - 0.2 * d for h, d in zip(height, dent)]
    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.04 * (v - 0.5) * 2) for c, v in zip(ch, fine)] for ch in rgb]
    # Finer, crenellated polygon edges rather than straight Voronoi lines.
    (*rgb, height), (common,) = jag(ctx, [*rgb, height], [common], 4, cells=16)
    rgb = shade(rgb, height, 0.2, reference_height=common)
    # Cracks outline every cell, sunken ones included.
    crack = [1 - smoothstep(0.02, 0.10, abs(h)) for h in height]
    rgb = mix(rgb, p["crack"], crack, 0.8)
    pit = [smoothstep(0.05, 0.2, -h) for h in height]
    return wet_pits(rgb, height, common, pit, p["damp"], p["glint"], depth=0.6), None
