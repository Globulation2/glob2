# SPDX-License-Identifier: GPL-3.0-or-later
"""The sand terrain material."""
from terrain_core import RECIPES, Style, mix, recipe
from ._sand import sand_look


@recipe(name='sand', group="legacy", label='Sand', profile="sand", border_class="solid",
        seam={'height': 1}, palette={'dark': (141, 130, 33), 'mid': (183, 169, 47), 'light': (225, 209, 67)}, style=Style(luma=160.1, std=27.0, grain_max=36.3),
        periodic=True, grid=8, blocks=4, preview=(182, 168, 48), minimap=(170, 170, 0))
def render_sand(ctx):
    """Warm granular sand with common edge dunes and independent interiors."""
    p = RECIPES["sand"].palette
    rgb, flecks = sand_look(ctx, p)
    # The tiny brown mineral flecks remain part of the scalar surface, with
    # no plants or large deposits introduced into the legacy sand identity.
    return mix(rgb, p["dark"], flecks, 0.18), None
