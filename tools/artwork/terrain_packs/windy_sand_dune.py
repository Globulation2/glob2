# SPDX-License-Identifier: GPL-3.0-or-later
"""The windy sand dune terrain material."""
import math

from terrain_core import RECIPES, Style, blend_fields, fbm, mix, recipe, smoothstep
from ._sand import sand_look


# Wind ripples run across the wind that drives the water swell (WAVE_SWELL's
# main term, toward the lower right), so dune crests and wave crests share one
# angle. Whole wave numbers on the block keep the ripples periodic; a second
# family one cycle denser drifts out of phase with the first, and switching
# between them breaks crests into forks and ends.
RIPPLE_WIND = (2, 1)


RIPPLE_CYCLES = 6


RIPPLE_CREST = 0.68  # fraction of each ripple that is the long windward rise


@recipe(name="windy_sand_dune", group="sand", label="Windy sand dune", profile="sand", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (184, 148, 98), "mid": (212, 180, 130), "light": (236, 212, 166), "lee": (168, 132, 88)},
        style=Style(luma=184, std=20, grain_max=26, match=0.9), periodic=True, grid=8, blocks=4)
def render_windy_sand_dune(ctx):
    """Classic sand grain swept into wind ripples: broad lit windward slopes up
    to a bright crest, then a short shaded lee face toward the lower right."""
    p, size = RECIPES["windy_sand_dune"].palette, ctx.size
    rgb, flecks = sand_look(ctx, p)
    light = blend_fields(ripple_light(ctx.shared_rng, size), ripple_light(ctx.rng, size), ctx.interior_mask)
    rgb = [[c * (1 + 0.18 * v) for c, v in zip(channel, light)] for channel in rgb]
    rgb = mix(rgb, p["lee"], [max(0.0, -v) for v in light], 0.3)
    return mix(rgb, p["dark"], flecks, 0.18), None


def ripple_tone(t):
    """Light across one ripple from trough (0) over the crest to the next trough."""
    if t < RIPPLE_CREST:
        u = t / RIPPLE_CREST
        return 0.1 + 0.5 * u + 0.3 * smoothstep(0.8, 1.0, u)
    # Lee: the crest highlight falls into shadow, which lifts toward the trough.
    u = (t - RIPPLE_CREST) / (1 - RIPPLE_CREST)
    crest, shadow = 1 - smoothstep(0.0, 0.15, u), 1 - smoothstep(0.0, 1.0, u)
    return 0.1 + 0.8 * crest - 0.8 * shadow * (1 - crest)


def ripple_light(rng, size):
    """Signed light of meandering, forking wind ripples, from the ripple phase."""
    kx, ky = (RIPPLE_CYCLES * k for k in RIPPLE_WIND)
    # Cells per 128 px canvas: the bend spans a tile, the wobble a ripple or two.
    bend = fbm(rng, 1, 2, size=size)
    wobble = fbm(rng, 6, 1, size=size)
    family = fbm(rng, 2, 2, size=size)
    relief = fbm(rng, 4, 2, size=size)
    light = []
    for i, (b, w, f, r) in enumerate(zip(bend, wobble, family, relief)):
        x, y = i % size, i // size
        offset = 1.0 * (b - 0.5) + 0.05 * (w - 0.5)
        first = (kx * x + ky * y) / size + offset
        second = ((kx + 1) * x + ky * y) / size + offset + 0.5
        m = smoothstep(0.47, 0.53, f)
        tone = ripple_tone(first - math.floor(first)) * (1 - m) + ripple_tone(second - math.floor(second)) * m
        light.append(tone * (0.25 + 0.75 * smoothstep(0.3, 0.62, r)))
    return light
