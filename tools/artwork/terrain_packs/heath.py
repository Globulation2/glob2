# SPDX-License-Identifier: GPL-3.0-or-later
"""The heath terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    barren_mark,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
)
from ._heather import heather_cushions, soft_shadow


@recipe(name="heath", group="barren", label="Heath", profile="brush", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (108, 98, 86), "mid": (126, 116, 102), "light": (138, 128, 114),
                 "crack": (74, 62, 54), "shadow": (78, 68, 60),
                 "stem": (56, 47, 42), "body": (90, 75, 65), "tip": (132, 118, 96),
                 "dead": (92, 86, 78), "deadtip": (124, 118, 106)},
        style=Style(luma=100, std=15, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4)
def render_heath(ctx):
    """Dry heath: clumps of dormant and dead heather cushions, each built of
    lit sprig tufts, over pale, faded, cracked sandy peat."""
    p = RECIPES["heath"].palette
    rgb = heath_ground(ctx, p)
    mat, cover = heather_cushions(ctx, p)
    rgb = mix(rgb, p["shadow"], soft_shadow(cover), 0.5)
    return mix(rgb, mat, cover, 1.0), None


def heath_ground(ctx, p):
    """Pale, faded sandy peat under the barren group's cracks."""
    size = ctx.size
    base = layout_field(ctx, lambda rng: fbm(rng, 4, 3, size=size))
    rgb = ramp(base, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    sand = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (0.93 + 0.14 * v) for c, v in zip(ch, sand)] for ch in rgb]
    return barren_mark(ctx, rgb, p["crack"], fade=0.15)
