# SPDX-License-Identifier: GPL-3.0-or-later
"""The granite tors terrain material: moss and granite grus under the raised granite tors decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="granite_tors", group="obstacles", label="Granite tors", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={'dark': (87, 102, 63), 'mid': (107, 120, 81), 'light': (129, 140, 101), 'stone': (145, 148, 125), 'stone_core': (105, 108, 89)},
        style=Style(luma=112, std=12, grain_max=10, match=0.8), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_granite_tors(ctx):
    """Mossy turf and coarse granite grit between the tors, with weathered cobbles."""
    return strewn_ground(ctx, RECIPES["granite_tors"].palette, chips=10)
