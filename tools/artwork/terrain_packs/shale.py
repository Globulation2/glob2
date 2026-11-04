# SPDX-License-Identifier: GPL-3.0-or-later
"""The shale terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    apply_cell_jitter,
    barren_mark,
    cast,
    cell_jitter,
    fbm,
    jag,
    lattice_noise,
    layout_field,
    plates,
    recipe,
    shade,
)


@recipe(name="shale", group="barren", label="Shale", profile="rock", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"fines": (118, 110, 98), "gap": (78, 72, 64), "crack": (64, 58, 52),
                 "chips": [(162, 156, 146), (148, 144, 138), (170, 160, 144), (138, 134, 128),
                           (156, 146, 132), (176, 170, 160)]},
        style=Style(luma=136, std=16, grain_max=16, match=0.8), periodic=True, grid=8, blocks=4)
def render_shale(ctx):
    """Shale: big flat plates of grey stone, each lying at its own level so
    one overlaps the next, with jagged broken edges, under the barren cracks."""
    p = RECIPES["shale"].palette
    size = ctx.size
    height, common, ids = plates(ctx, 3, stretch=1.7, step=0.35, rise=0.05)
    count = 3 * ctx.area_scale
    tones = [p["chips"][int(ctx.shared_rng.random() * len(p["chips"]))] for _ in range(count)]
    rgb = [[tones[i][k] for i in ids] for k in range(3)]
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, count, 0.08))
    layers = layout_field(ctx, lambda rng: fbm(rng, 3, 2, cells_y=24, size=size))
    rgb = [[c * (0.9 + 0.2 * v) for c, v in zip(channel, layers)] for channel in rgb]
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))
    rgb = [[c * (0.94 + 0.12 * v) for c, v in zip(channel, speck)] for channel in rgb]
    (*rgb, height), (common,) = jag(ctx, [*rgb, height], [common], 6, cells=12)
    rgb = shade(rgb, height, 0.35, reference_height=common)
    rgb = cast(rgb, height, p["gap"], reach=5, strength=0.55)
    return barren_mark(ctx, rgb, p["crack"], cells=4, strength=0.7, fade=0.15), None
