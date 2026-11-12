# SPDX-License-Identifier: GPL-3.0-or-later
"""The steppe grass terrain material."""
from terrain_core import N, RECIPES, Style, domain_warp, fbm, lattice_noise, mix, normalize_like, recipe, smoothstep
from ._grass import fine_grain, layout, turf


@recipe(name="steppe_grass", group="grass", label="Steppe grass", profile="turf", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"shadow": (40, 66, 56), "dark": (64, 98, 78), "mid": (98, 136, 104), "light": (146, 174, 140),
                 "warm": (150, 160, 104), "cool": (60, 100, 110), "sheen": (196, 212, 198), "plume": (214, 210, 186)},
        style=Style(luma=112, std=11, grain_max=9), periodic=True, grid=8, blocks=4)
def render_steppe_grass(ctx):
    """Cold steppe: fine sage blue-grey fescue, a silver sheen in broad bands
    where the wind lays it over, and pale feather-grass plumes on the crowns."""
    p, size = RECIPES["steppe_grass"].palette, ctx.size
    rgb, height = turf(ctx, p, broad=6, tuft_cells=128, tuft_y=20, tuft_w=0.5, relief=0.12,
                       warm=0.25, cool=0.45, comb=0.7)

    def sheen(rng):
        s = fbm(rng, 3, 3, cells_y=10, size=size, array=True)
        return domain_warp(s, fbm(rng, 2, 2, size=size, array=True), fbm(rng, 2, 2, size=size, array=True),
                           1.0 * size / N)

    s = normalize_like(layout(ctx, sheen), sheen(ctx.shared_rng))
    rgb = mix(rgb, p["sheen"], smoothstep(0.62, 0.9, s) * (0.3 + 0.7 * height), 0.4)
    # Plumes: thin pale streaks, about a quarter of a glob long.
    plumes = layout(ctx, lambda rng: lattice_noise(rng, 96, cells_y=28, size=size, array=True))
    rgb = mix(rgb, p["plume"], smoothstep(0.86, 0.97, plumes) * smoothstep(0.4, 0.8, height), 0.6)
    return fine_grain(ctx, rgb, 0.07), None
