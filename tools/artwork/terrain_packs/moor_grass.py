# SPDX-License-Identifier: GPL-3.0-or-later
"""The moor grass terrain material."""
from terrain_core import N, RECIPES, Style, domain_warp, fbm, lattice_noise, mix, normalize_like, recipe, smoothstep
from ._grass import fine_grain, layout, turf


@recipe(name="moor_grass", group="grass", label="Moor grass", profile="turf", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"shadow": (30, 54, 50), "dark": (53, 87, 56), "mid": (90, 126, 58), "light": (134, 160, 82),
                 "warm": (130, 150, 66), "cool": (50, 86, 96), "stem": (98, 76, 152), "seed": (150, 140, 192)},
        style=Style(luma=100, std=12, grain_max=9), periodic=True, grid=8, blocks=4)
def render_moor_grass(ctx):
    """Upland moor: green turf with blue skylight in its hollows and shadows,
    and drifts of blue-violet stems with pale seed heads on the tallest."""
    p, size = RECIPES["moor_grass"].palette, ctx.size
    rgb, height = turf(ctx, p, broad=6, tuft_cells=100, tuft_y=20, tuft_w=0.5, relief=0.15,
                       warm=0.25, cool=0.42, comb=0.6)
    drift = normalize_like(layout(ctx, lambda rng: fbm(rng, 4, 3, size=size, array=True)),
                           fbm(ctx.shared_rng, 4, 3, size=size, array=True))
    where = 0.35 + 0.65 * smoothstep(0.25, 0.6, drift)

    def stem_field(rng):
        f = lattice_noise(rng, 150, cells_y=14, size=size, array=True)
        return domain_warp(f, fbm(rng, 2, 2, size=size, array=True), fbm(rng, 2, 2, size=size, array=True),
                           0.6 * size / N)

    # Stems are drawn at nearly full strength in thin streaks, so the violet
    # stays violet instead of averaging with the green into brown.
    stems = layout(ctx, stem_field)
    rgb = mix(rgb, p["stem"], smoothstep(0.66, 0.78, stems) * where * (0.5 + 0.5 * height), 0.85)
    rgb = mix(rgb, p["seed"], smoothstep(0.86, 0.95, stems) * where * smoothstep(0.5, 0.9, height), 0.6)
    return fine_grain(ctx, rgb, 0.07), None
