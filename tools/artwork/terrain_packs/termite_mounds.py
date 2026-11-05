# SPDX-License-Identifier: GPL-3.0-or-later
"""The termite mounds terrain material: dry red earth under the termite mounds decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="termite_mounds", group="ridges", label="Termite mounds", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={'dark': (126, 86, 56), 'mid': (154, 112, 74), 'light': (180, 140, 98), 'stone': (168, 106, 68), 'stone_core': (120, 74, 48)},
        style=Style(luma=128, std=13, grain_max=10), periodic=True, grid=8, blocks=4,
        note="ground under the decor sprites (tools/artwork/terrain_decor.py)")
def render_termite_mounds(ctx):
    """Dry red earth around the mounds, scattered with crumbs of their clay. The group's ledges run across it."""
    return strewn_ground(ctx, RECIPES["termite_mounds"].palette, chips=10, ledges=True)
