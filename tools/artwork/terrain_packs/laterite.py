# SPDX-License-Identifier: GPL-3.0-or-later
"""The laterite terrain material."""
import math

from terrain_core import (
    RECIPES,
    Style,
    barren_mark,
    block_worley,
    cast,
    fbm,
    gradient_light,
    jag,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shade,
    smoothstep,
)


@recipe(name="laterite", group="barren", label="Laterite", profile="soil", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (112, 46, 26), "mid": (136, 60, 32), "light": (154, 76, 42), "fines": (198, 120, 62),
                 "nodule": (168, 78, 40), "brown": (130, 66, 42), "orange": (190, 94, 46), "nodule_lit": (212, 124, 72), "iron": (54, 34, 28),
                 "iron_lit": (168, 138, 118), "gap": (66, 26, 14), "crack": (84, 34, 20)},
        style=Style(luma=96, std=12, grain_max=12, match=0.8), periodic=True, grid=8, blocks=4)
def render_laterite(ctx):
    """Laterite: a packed bed of rounded, hardened iron-red nodules, a few of
    them glossy brown-black iron, over a cracked red crust with lower
    patches of bare ochre fines."""
    p = RECIPES["laterite"].palette
    size = ctx.size
    # Crust and fines first, so the barren mark lies between the nodules.
    soil = layout_field(ctx, lambda rng: fbm(rng, 5, 3, size=size))
    rgb = ramp(soil, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.05 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    patches = layout_field(ctx, lambda rng: fbm(rng, 2, 3, size=size))
    fines = [smoothstep(0.55, 0.75, v) for v in patches]
    rgb = mix(rgb, p["fines"], fines, 0.7)
    rgb = barren_mark(ctx, rgb, p["crack"], strength=0.8, fade=0.05)
    # Nodules: two packed layers of cells (large, then medium), each cell a
    # dome that may touch its neighbours; thinned out over the fines.
    height, common, tone, iron = laterite_nodules(ctx, fines)
    rgb = cast(rgb, height, p["gap"], reach=6, strength=0.6)
    body = [smoothstep(0.02, 0.15, h) for h in height]
    # Each pebble takes its own tone from brown through orange.
    stone = ramp(tone, [(0.0, p["brown"]), (0.5, p["nodule"]), (1.0, p["orange"])])
    stone = mix(stone, p["iron"], iron, 1.0)
    rgb = mix(rgb, stone, body, 1.0)
    rgb = shade(rgb, height, 0.8, reference_height=common)
    lit = [max(0.0, -g) * b for g, b in zip(gradient_light(height, reference_height=common), body)]
    rgb = mix(rgb, p["nodule_lit"], [smoothstep(0.15, 0.7, v) * (1 - i) for v, i in zip(lit, iron)], 0.55)
    # Glossy iron: a small tight highlight rather than a broad lit side.
    return mix(rgb, p["iron_lit"], [smoothstep(0.3, 0.65, v) * i for v, i in zip(lit, iron)], 0.75), None


def laterite_nodules(ctx, fines):
    """Packed nodules from Worley cells: (height, common height, per-pixel
    tone, iron mask). Cell properties come from the shared stream, so
    boundary nodules match in every layout. One broad warp makes them
    rounded-angular; tone and iron take the same warp as the height, so
    every pebble keeps one colour. Iron pebbles sit on top of their
    neighbours rather than being notched by them."""
    size = len(fines)
    height, common = [0.0] * size, [0.0] * size
    tone, iron, top = [0.0] * size, [0.0] * size, [0.0] * size
    for cells, keep, low, high, black_share in ((5, 0.1875, 0.7, 0.95, 0.0), (14, 0.175, 0.55, 0.85, 0.09)):
        # Cells come from the kept nodules only, so none is cut or flattened
        # against a neighbour that was left out.
        kept = []
        f1, f2, ids, c1, c2, cids = block_worley(
            ctx, cells, keep=lambda n: kept.extend(ctx.shared_rng.random() < keep for _ in range(n)) or kept)
        count = cells * ctx.area_scale
        radius = [0.5 * ctx.shared_rng.uniform(low, high) for _ in range(count)]
        shade_of = [ctx.shared_rng.random() for _ in range(count)]
        black = [ctx.shared_rng.random() < black_share for _ in range(count)]
    
        def dome(a, b, i):
            if not kept[i]:
                return 0.0
            d = a / radius[i]
            return math.sqrt(1 - d * d) * (0.55 + 0.45 * smoothstep(0.0, 0.08, b - a)) if d < 1 else 0.0

        layer = [dome(a, b, i) for a, b, i in zip(f1, f2, ids)]
        reference = [dome(a, b, i) for a, b, i in zip(c1, c2, cids)]
        tones = [shade_of[i] for i in ids]
        blacks = [1.0 if black[i] else 0.0 for i in ids]
        (layer, tones, blacks), (reference,) = jag(ctx, [layer, tones, blacks], [reference], 4, cells=8)
        for k in range(size):
            h = layer[k] * (1 - 0.85 * fines[k])
            rank = h + (1.0 if blacks[k] > 0.5 and h > 0.02 else 0.0)
            if rank > top[k]:
                top[k], height[k], tone[k] = rank, h, tones[k]
                iron[k] = 1.0 if blacks[k] > 0.5 else 0.0
            common[k] = max(common[k], reference[k])
    return height, common, tone, iron
