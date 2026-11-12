# SPDX-License-Identifier: GPL-3.0-or-later
"""The thicket terrain material."""
import math

import numpy as np

from terrain_core import (
    N,
    RECIPES,
    Style,
    TAU,
    blend_fields,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    stamp_stroke,
)


@recipe(name="thicket", group="obstacles", label="Thicket", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"dark": (40, 54, 26), "mid": (62, 80, 38), "light": (92, 110, 58),
                 "twig": (96, 78, 52), "twig_dark": (52, 42, 30)},
        style=Style(luma=65, std=11, grain_max=10, match=0.7),
        periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_thicket(ctx):
    """Shared undergrowth and twig fields beneath independently raised decor."""
    p = RECIPES["thicket"].palette
    common, common_grain, common_twigs = thicket_fields(ctx.shared_rng, ctx.size)
    own, own_grain, own_twigs = thicket_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior)
    rgb = rgb * (1 + 0.12 * (fine - 0.5) * 2)
    twigs = blend_fields(common_twigs, own_twigs, ctx.interior)
    return mix(rgb, p["twig_dark"], twigs, 0.35), None


def thicket_fields(rng, size):
    """Periodic soil, grain and faint twig masks at the original native scale."""
    height = fbm(rng, 7, 3, size=size, array=True)
    height = domain_warp(height, fbm(rng, 3, 2, size=size, array=True),
                         fbm(rng, 3, 2, size=size, array=True), 8)
    fine = lattice_noise(rng, 48, size=size, array=True)
    twigs = np.zeros(size * size)
    for _ in range(rng.randint(3, 5) * (size // N) ** 2):
        length = 14 + rng.random() * 12
        x, y = rng.random() * size, rng.random() * size
        angle = rng.random() * TAU
        stamp_stroke(twigs, x - math.cos(angle) * length / 2, y - math.sin(angle) * length / 2,
                     angle, length, 2.5)
    return height, fine, twigs
