# SPDX-License-Identifier: GPL-3.0-or-later
"""The petrified stumps terrain material: stony forest litter under the petrified stumps decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="petrified_stumps", group="ridges", label="Petrified stumps", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={'dark': (90, 84, 43), 'mid': (114, 106, 61), 'light': (138, 130, 83), 'stone': (174, 158, 101), 'stone_core': (122, 114, 73)},
        style=Style(luma=112, std=13, grain_max=10), periodic=True, grid=8, blocks=4,
        note="ground under the decor sprites (tools/artwork/terrain_decor.py)")
def render_petrified_stumps(ctx):
    """Stony forest litter around the stumps, scattered with flakes of petrified wood. The group's ledges run across it."""
    return strewn_ground(ctx, RECIPES["petrified_stumps"].palette, chips=10, ledges=True)
