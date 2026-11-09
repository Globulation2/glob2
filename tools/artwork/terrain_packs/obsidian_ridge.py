# SPDX-License-Identifier: GPL-3.0-or-later
"""The obsidian ridge terrain material: ash and glassy chips under the raised obsidian ridge decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="obsidian_ridge", group="obstacles", label="Obsidian ridge", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={'dark': (45, 46, 58), 'mid': (63, 64, 76), 'light': (83, 84, 98), 'stone': (37, 40, 58), 'stone_core': (115, 124, 154)},
        style=Style(luma=66, std=12, grain_max=10, match=0.8), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_obsidian_ridge(ctx):
    """Grey volcanic ash, strewn with black glass chips that catch a sky-blue glint."""
    return strewn_ground(ctx, RECIPES["obsidian_ridge"].palette, chips=12)
