# SPDX-License-Identifier: GPL-3.0-or-later
"""The pale sand terrain material."""
from terrain_core import RECIPES, Style, mix, recipe
from ._sand import sand_look


@recipe(name="pale_sand", group="sand", label="Pale sand", profile="sand", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (190, 180, 146), "mid": (214, 204, 170), "light": (234, 226, 196), "shell": (250, 246, 232)},
        style=Style(luma=198, std=14, grain_max=24, match=0.5), periodic=True, grid=8, blocks=4)
def render_pale_sand(ctx):
    """Classic sand grain bleached pale, its flecks turned to shell fragments."""
    p = RECIPES["pale_sand"].palette
    rgb, flecks = sand_look(ctx, p)
    return mix(rgb, p["shell"], flecks, 0.7), None
