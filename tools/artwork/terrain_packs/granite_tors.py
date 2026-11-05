# SPDX-License-Identifier: GPL-3.0-or-later
"""The granite tors terrain material: moss and granite grus under the raised granite tors decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="granite_tors", group="obstacles", label="Granite tors", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={'dark': (92, 98, 70), 'mid': (112, 116, 88), 'light': (134, 136, 108), 'stone': (150, 144, 132), 'stone_core': (110, 104, 96)},
        style=Style(luma=112, std=12, grain_max=10, match=0.8), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_granite_tors(ctx):
    """Mossy turf and coarse granite grit between the tors, with weathered cobbles."""
    return strewn_ground(ctx, RECIPES["granite_tors"].palette, chips=10)
