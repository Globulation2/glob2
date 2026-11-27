# SPDX-License-Identifier: GPL-3.0-or-later
"""The water terrain material."""
from terrain_core import RECIPES, Style, recipe
from ._waves import WAVE_GRID, WAVE_PHASES, render_waves


@recipe(name="water", group="water", label="Water", profile="still", border_class="pool",
        seam={"height": 5, "cast_q8": 72, "cast_width_q8": 640},
        palette={"deep": (50, 36, 168), "base": (68, 52, 198), "light": (86, 72, 216), "glint": (156, 150, 244)},
        style=Style(luma=74, std=8, grain_max=8, match=0.0), phases=WAVE_PHASES, animation_ticks=6,
        periodic=True, grid=WAVE_GRID, preview=(70, 50, 191), minimap=(0, 40, 120))
def render_water(ctx):
    return render_waves(ctx, RECIPES["water"].palette, 0.35, 0.2, 0.55, 0.2), None
