# SPDX-License-Identifier: GPL-3.0-or-later
"""The dry grass terrain material."""
from terrain_core import N, RECIPES, Style, domain_warp, fbm, mix, normalize_like, recipe, smoothstep
from ._grass import fine_grain, layout, turf


@recipe(name="dry_grass", group="grass", label="Dry grass", profile="turf", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"shadow": (90, 80, 40), "dark": (126, 112, 54), "mid": (164, 146, 76), "light": (198, 180, 108),
                 "warm": (192, 150, 72), "cool": (112, 128, 62), "crest": (222, 208, 150), "trough": (98, 86, 44)},
        style=Style(luma=116, std=11, grain_max=9), periodic=True, grid=8, blocks=4)
def render_dry_grass(ctx):
    """Prairie waves: late-summer golden straw with green still showing at
    the roots, long combed blades bowing in the wind in broad curved bands of
    pale crest and dark trough."""
    p, size = RECIPES["dry_grass"].palette, ctx.size
    rgb, height = turf(ctx, p, broad=6, tuft_cells=110, tuft_y=16, tuft_w=0.5, relief=0.14, warm=0.35, cool=0.4,
                       comb=1.0)

    def waves(rng):
        w = fbm(rng, 2, 2, cells_y=8, size=size, array=True)
        return domain_warp(w, fbm(rng, 2, 2, size=size, array=True), fbm(rng, 2, 2, size=size, array=True),
                           1.2 * size / N)

    wave = normalize_like(layout(ctx, waves), waves(ctx.shared_rng))
    rgb = mix(rgb, p["crest"], smoothstep(0.6, 0.85, wave) * (0.4 + 0.6 * height), 0.35)
    rgb = mix(rgb, p["trough"], smoothstep(0.4, 0.15, wave), 0.3)
    return fine_grain(ctx, rgb, 0.08), None
