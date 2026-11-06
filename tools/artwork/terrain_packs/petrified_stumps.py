# SPDX-License-Identifier: GPL-3.0-or-later
"""The petrified stumps terrain material: stony forest litter under the petrified stumps decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="petrified_stumps", group="ridges", label="Petrified stumps", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={'dark': (92, 82, 66), 'mid': (116, 104, 84), 'light': (140, 128, 106), 'stone': (176, 156, 124), 'stone_core': (124, 112, 96)},
        style=Style(luma=112, std=13, grain_max=10), periodic=True, grid=8, blocks=4,
        note="ground under the decor sprites (tools/artwork/terrain_decor.py)")
def render_petrified_stumps(ctx):
    """Stony forest litter around the stumps, scattered with flakes of petrified wood. The group's ledges run across it."""
    return strewn_ground(ctx, RECIPES["petrified_stumps"].palette, chips=10, ledges=True)
