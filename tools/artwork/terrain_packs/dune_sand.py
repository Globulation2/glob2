# SPDX-License-Identifier: GPL-3.0-or-later
"""The dune sand terrain material."""
from terrain_core import RECIPES, Style, mix, recipe
from ._sand import sand_look


@recipe(name="dune_sand", group="sand", label="Dune sand", profile="sand", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (176, 120, 60), "mid": (206, 152, 84), "light": (230, 186, 118)},
        style=Style(luma=158, std=20, grain_max=26, match=0.9), periodic=True, grid=8, blocks=4)
def render_dune_sand(ctx):
    """Classic sand grain in warm orange, with its mineral flecks."""
    p = RECIPES["dune_sand"].palette
    rgb, flecks = sand_look(ctx, p)
    return mix(rgb, p["dark"], flecks, 0.18), None
