# SPDX-License-Identifier: GPL-3.0-or-later
"""The travertine terraces terrain material: calcite crust under the raised travertine terraces decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="travertine_terraces", group="obstacles", label="Travertine terraces", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={'dark': (181, 172, 154), 'mid': (201, 194, 178), 'light': (219, 214, 202), 'stone': (229, 226, 220), 'stone_core': (193, 164, 128)},
        style=Style(luma=196, std=10, grain_max=10, match=0.8), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_travertine_terraces(ctx):
    """A pale calcite crust at the foot of the terraces, with flakes of fallen flowstone."""
    return strewn_ground(ctx, RECIPES["travertine_terraces"].palette, chips=8)
