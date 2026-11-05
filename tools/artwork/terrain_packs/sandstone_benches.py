# SPDX-License-Identifier: GPL-3.0-or-later
"""The sandstone benches terrain material: sand and fallen sandstone under the raised sandstone benches decor."""
from terrain_core import RECIPES, Style, recipe

from ._strewn import strewn_ground


@recipe(name="sandstone_benches", group="obstacles", label="Sandstone benches", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={'dark': (176, 124, 82), 'mid': (204, 158, 110), 'light': (224, 186, 138), 'stone': (214, 168, 120), 'stone_core': (168, 110, 72)},
        style=Style(luma=150, std=14, grain_max=10, match=0.8), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_sandstone_benches(ctx):
    """Wind-blown sand at the foot of the benches, strewn with slabs fallen from their beds."""
    return strewn_ground(ctx, RECIPES["sandstone_benches"].palette, chips=9)
