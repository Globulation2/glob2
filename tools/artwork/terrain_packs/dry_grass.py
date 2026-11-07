# SPDX-License-Identifier: GPL-3.0-or-later
"""The dry grass terrain material."""
from terrain_core import N, RECIPES, Style, domain_warp, fbm, mix, normalize_like, recipe, smoothstep
from ._grass import fine_grain, layout, turf


@recipe(name="dry_grass", group="grass", label="Dry grass", profile="turf", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"shadow": (104, 80, 40), "dark": (140, 110, 52), "mid": (178, 146, 74), "light": (212, 184, 110),
                 "warm": (206, 150, 72), "cool": (146, 138, 82), "crest": (238, 218, 156), "trough": (110, 84, 44)},
        style=Style(luma=124, std=12, grain_max=9), periodic=True, grid=8, blocks=4)
def render_dry_grass(ctx):
    """Prairie waves: ripe amber grass with long combed blades, bowing in the
    wind in broad curved bands of bright crest and dark trough."""
    p, size = RECIPES["dry_grass"].palette, ctx.size
    rgb, height = turf(ctx, p, broad=6, tuft_cells=110, tuft_y=16, tuft_w=0.5, relief=0.14, warm=0.4, cool=0.3,
                       comb=1.0)

    def waves(rng):
        w = fbm(rng, 2, 2, cells_y=8, size=size)
        return domain_warp(w, fbm(rng, 2, 2, size=size), fbm(rng, 2, 2, size=size), 1.2 * size / N)

    wave = normalize_like(layout(ctx, waves), waves(ctx.shared_rng))
    rgb = mix(rgb, p["crest"], [smoothstep(0.6, 0.85, v) * (0.4 + 0.6 * h) for v, h in zip(wave, height)], 0.5)
    rgb = mix(rgb, p["trough"], [smoothstep(0.4, 0.15, v) for v in wave], 0.35)
    return fine_grain(ctx, rgb, 0.08), None
