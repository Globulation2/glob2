# SPDX-License-Identifier: GPL-3.0-or-later
"""The hoodoos terrain material: pink sandstone grit under the hoodoos decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="hoodoos", group="ridges", label="Hoodoos", profile="rock", border_class="solid",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={'dark': (176, 120, 88), 'mid': (204, 152, 114), 'light': (226, 182, 144), 'stone': (216, 150, 108), 'stone_core': (130, 104, 90)},
        style=Style(luma=160, std=13, grain_max=10), periodic=True, grid=8, blocks=4,
        note="ground under the decor sprites (tools/artwork/terrain_decor.py)")
def render_hoodoos(ctx):
    """Pink sandstone grit washed from the pillars, with fallen chips of their dark caps. The group's ledges run across it."""
    return strewn_ground(ctx, RECIPES["hoodoos"].palette, chips=10, ledges=True)
