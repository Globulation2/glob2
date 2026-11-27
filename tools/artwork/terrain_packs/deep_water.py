# SPDX-License-Identifier: GPL-3.0-or-later
"""The deep water terrain material."""
from terrain_core import RECIPES, Style, recipe
from ._waves import WAVE_GRID, WAVE_PHASES, render_waves


@recipe(name="deep_water", group="deep_water", label="Deep water", profile="still", border_class="pool",
        seam={"height": 6, "cast_q8": 72, "cast_width_q8": 640},
        palette={"deep": (40, 28, 132), "base": (52, 38, 152), "light": (62, 48, 170), "glint": (110, 96, 214)},
        style=Style(luma=56, std=6, grain_max=7, match=0.0), phases=WAVE_PHASES, animation_ticks=6,
        periodic=True, grid=WAVE_GRID, preview=(44, 34, 120), minimap=(24, 18, 84))
def render_deep_water(ctx):
    return render_waves(ctx, RECIPES["deep_water"].palette, 0.4, 0.12, 0.3, 0.15), None
