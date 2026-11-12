# SPDX-License-Identifier: GPL-3.0-or-later
"""The dark water terrain material.

Dark water rolls on the same periodic wave field as water and deep water (same
seed, wind, grid and clock), so crests carry on across the deep/dark blend. Its
own character: a heavy, inky swell with little chop and faint cold glints, and
an ink-dark shadow on the back face of every swell crest that travels with it,
so the troughs read deeper than deep water's."""
import numpy as np
from scipy.ndimage import uniform_filter

from terrain_core import RECIPES, Style, mix, recipe, smoothstep
from ._waves import WAVE_GRID, WAVE_PHASES, WAVE_SIZE, render_waves, wave_layers


@recipe(name="dark_water", group="deep_water", label="Dark water", profile="still", border_class="pool",
        seam={"height": 7, "cast_q8": 80, "cast_width_q8": 704},
        palette={"deep": (25, 18, 72), "base": (38, 28, 98), "light": (47, 37, 114), "glint": (80, 88, 160),
                 "shadow": (16, 11, 48)},
        style=Style(luma=38, std=5, grain_max=6, match=0.0), phases=WAVE_PHASES, animation_ticks=6,
        periodic=True, grid=WAVE_GRID, preview=(32, 26, 76), minimap=(16, 12, 52))
def render_dark_water(ctx):
    p = RECIPES["dark_water"].palette
    rgb = render_waves(ctx, p, 0.5, 0.09, 0.3, 0.12)
    swell, _, front = wave_layers(ctx)
    # The lee of each crest, deepest where the swell falls into its trough.
    lee = smoothstep(0.0, 1.0, -front) * smoothstep(0.25, -0.35, swell)
    return mix(rgb, p["shadow"], soften(lee, ctx.size), 0.7), None


def soften(values, size):
    """A wrapping box blur: the swell front steps with the warp's whole-pixel
    offsets, which would otherwise show as fine ridges in the shadow."""
    field = np.asarray(values).reshape(size, size)
    return uniform_filter(field, size=9 * size // WAVE_SIZE + 1, mode="wrap").ravel()
