# SPDX-License-Identifier: GPL-3.0-or-later
"""The wind swept snow terrain material."""
import numpy as np

from terrain_core import (
    N,
    RECIPES,
    Style,
    as_list,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
)


def drift_ridges(phase, crest=0.72):
    """Asymmetric drift profile in [0, 1] from a ridge phase: a long windward
    rise easing out of the trough and steepening to a sharp crest, then a
    short lee face that falls steeply and flattens into the next trough."""
    f = phase % 1.0
    rise = f / crest
    lee = (1 - f) / (1 - crest)
    return np.where(f < crest, rise ** 1.5, lee * lee)


@recipe(name="wind_swept_snow", group="ice", label="Wind-swept snow", profile="soil", border_class="solid",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512, "fringe": [196, 210, 232], "fringe_q8": 64,
              "fringe_width_q8": 384},
        palette={"shadow": (168, 176, 200), "lee": (186, 192, 214), "mid": (208, 212, 232),
                 "lit": (232, 234, 250), "crest": (250, 250, 255), "sparkle": (255, 255, 255)},
        style=Style(luma=204, std=11, grain_max=6, match=0.6), periodic=True, grid=8, blocks=4)
def render_wind_swept_snow(ctx):
    """Snow carved by one prevailing wind into drifts: long, gently curving
    crests lit along a crisp rim, each falling to a soft lavender lee shadow
    that fades into broad quiet troughs, with faint ripples on the scoured
    flats and scattered sparkle; deep snow's palette at a little more relief.

    The ridge phase is a plane wave with whole wave numbers on the block, so
    every layout shares it; only the smooth warps and drift heights vary
    inside, which keeps crests continuous across block boundaries.
    """
    p, size = RECIPES["wind_swept_snow"].palette, ctx.size
    yy, xx = np.indices((size, size), dtype=np.float64)
    # Whole ridge counts per 8-tile block: drifts about a tile apart across
    # the diagonal, faint ripples a third of that, a little off parallel.
    scale = size / (8 * N)
    drift_k, ripple_k = (5, 5), (16, 10)
    base = scale * (drift_k[0] * xx + drift_k[1] * yy) / size
    fine = scale * (ripple_k[0] * xx + ripple_k[1] * yy) / size
    # Broad bends keep crests long and roughly parallel; a weaker, finer warp
    # offsets neighbouring stretches so crests split and merge.
    bend = np.array(layout_field(ctx, lambda rng: fbm(rng, 2, 1, size=size))).reshape(size, size)
    fray = np.array(layout_field(ctx, lambda rng: fbm(rng, 4, 2, size=size))).reshape(size, size)
    phase = base + 0.6 * (bend - 0.5) + 0.12 * (fray - 0.5)
    ripple_phase = fine + 0.6 * (bend - 0.5) + 0.1 * (fray - 0.5)
    # Drift height: tall drifts where snow piled up, low scoured flats elsewhere.
    pile = np.array(layout_field(ctx, lambda rng: fbm(rng, 3, 2, size=size))).reshape(size, size)
    amp = np.clip((pile - 0.3) / 0.45, 0, 1)
    amp = 0.3 + 0.7 * amp * amp * (3 - 2 * amp)
    crest = 0.8
    height = 34.0 * amp * drift_ridges(phase, crest) + 0.5 * (1.2 - amp) * drift_ridges(ripple_phase, 0.65)
    # Low sun from the upper left, as a fixed light so shared boundaries
    # match; tone is measured from flat snow, which sits at the mid colour.
    hx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) / 2
    hy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) / 2
    light = np.array([-0.62, -0.62, 0.48])
    light /= np.linalg.norm(light)
    lambert = (-hx * light[0] - hy * light[1] + light[2]) / np.sqrt(hx * hx + hy * hy + 1)
    tone = np.clip(0.55 + 1.2 * (lambert - light[2]), 0, 1)
    rgb = ramp(as_list(tone), [(0.0, p["shadow"]), (0.3, p["lee"]), (0.55, p["mid"]), (1.0, p["lit"])])
    # A crisp rim catches the sun along each crest.
    f = phase % 1.0
    rim = np.clip(1 - np.maximum(crest - f, 0) / 0.02 - np.maximum(f - crest, 0) / 0.006, 0, 1) * amp
    rgb = mix(rgb, p["crest"], as_list(rim), 0.6)
    # Wind-polished grain and sparkle on the lit faces.
    grain_field = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.02 * (v - 0.5) * 2) for c, v in zip(channel, grain_field)] for channel in rgb]
    speck = np.array(layout_field(ctx, lambda rng: lattice_noise(rng, 96, size=size)))
    rgb = mix(rgb, p["sparkle"], as_list(np.clip((speck - 0.9) / 0.06, 0, 1) * tone.ravel()), 0.6)
    return rgb, None
