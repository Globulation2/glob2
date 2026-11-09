# SPDX-License-Identifier: GPL-3.0-or-later
"""The penitentes terrain material: wind-packed snow under the penitentes decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="penitentes", group="ridges", label="Penitentes", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={'dark': (165, 188, 205), 'mid': (197, 218, 227), 'light': (223, 240, 241), 'stone': (173, 214, 225), 'stone_core': (129, 176, 199)},
        style=Style(luma=200, std=10, grain_max=10), periodic=True, grid=8, blocks=4,
        note="ground under the decor sprites (tools/artwork/terrain_decor.py)")
def render_penitentes(ctx):
    """Wind-packed snow between the blades, with fallen shards of ice. The group's ledges run across it."""
    return strewn_ground(ctx, RECIPES["penitentes"].palette, chips=8, ledges=True)
