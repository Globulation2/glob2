# SPDX-License-Identifier: GPL-3.0-or-later
"""The laterite terrain material."""
import numpy as np

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
    soil = layout_field(ctx, lambda rng: fbm(rng, 5, 3, size=size, array=True))
    rgb = ramp(soil, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size, array=True))
    rgb = rgb * (1 + 0.05 * (grit - 0.5) * 2)
    patches = layout_field(ctx, lambda rng: fbm(rng, 2, 3, size=size, array=True))
    fines = smoothstep(0.55, 0.75, patches)
    rgb = mix(rgb, p["fines"], fines, 0.7)
    rgb = barren_mark(ctx, rgb, p["crack"], strength=0.8, fade=0.05)
    # Nodules: two packed layers of cells (large, then medium), each cell a
    # dome that may touch its neighbours; thinned out over the fines.
    height, common, tone, iron = laterite_nodules(ctx, fines)
    rgb = cast(rgb, height, p["gap"], reach=6, strength=0.6)
    body = smoothstep(0.02, 0.15, height)
    # Each pebble takes its own tone from brown through orange.
    stone = ramp(tone, [(0.0, p["brown"]), (0.5, p["nodule"]), (1.0, p["orange"])])
    stone = mix(stone, p["iron"], iron, 1.0)
    rgb = mix(rgb, stone, body, 1.0)
    rgb = shade(rgb, height, 0.8, reference_height=common)
    lit = np.maximum(0.0, -gradient_light(height, reference_height=common)) * body
    rgb = mix(rgb, p["nodule_lit"], smoothstep(0.15, 0.7, lit) * (1 - iron), 0.55)
    # Glossy iron: a small tight highlight rather than a broad lit side.
    return mix(rgb, p["iron_lit"], smoothstep(0.3, 0.65, lit) * iron, 0.75), None


def laterite_nodules(ctx, fines):
    """Packed nodules from Worley cells: (height, common height, per-pixel
    tone, iron mask). Cell properties come from the shared stream, so
    boundary nodules match in every layout. One broad warp makes them
    rounded-angular; tone and iron take the same warp as the height, so
    every pebble keeps one colour. Iron pebbles sit on top of their
    neighbours rather than being notched by them."""
    size = len(fines)
    height, common = np.zeros(size), np.zeros(size)
    tone, iron, top = np.zeros(size), np.zeros(size), np.zeros(size)
    for cells, keep, low, high, black_share in ((5, 0.1875, 0.7, 0.95, 0.0), (14, 0.175, 0.55, 0.85, 0.09)):
        # Cells come from the kept nodules only, so none is cut or flattened
        # against a neighbour that was left out.
        kept = []
        f1, f2, ids, c1, c2, cids = block_worley(
            ctx, cells, keep=lambda n: kept.extend(ctx.shared_rng.random() < keep for _ in range(n)) or kept,
            array=True)
        count = cells * ctx.area_scale
        radius = np.array([0.5 * ctx.shared_rng.uniform(low, high) for _ in range(count)])
        shade_of = np.array([ctx.shared_rng.random() for _ in range(count)])
        black = np.array([ctx.shared_rng.random() < black_share for _ in range(count)])
        kept_cells = np.array(kept)

        def dome(a, b, i):
            d = a / radius[i]
            inside = kept_cells[i] & (d < 1)
            return np.where(inside, np.sqrt(np.where(inside, 1 - d * d, 0.0))
                            * (0.55 + 0.45 * smoothstep(0.0, 0.08, b - a)), 0.0)

        layer = dome(f1, f2, ids)
        reference = dome(c1, c2, cids)
        tones = shade_of[ids]
        blacks = np.where(black[ids], 1.0, 0.0)
        (layer, tones, blacks), (reference,) = jag(ctx, [layer, tones, blacks], [reference], 4, cells=8)
        h = layer * (1 - 0.85 * fines)
        rank = h + np.where((blacks > 0.5) & (h > 0.02), 1.0, 0.0)
        take = rank > top
        top = np.where(take, rank, top)
        height = np.where(take, h, height)
        tone = np.where(take, tones, tone)
        iron = np.where(take, np.where(blacks > 0.5, 1.0, 0.0), iron)
        common = np.maximum(common, reference)
    return height, common, tone, iron
