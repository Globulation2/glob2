# SPDX-License-Identifier: GPL-3.0-or-later
"""Frost shared by the ice-cave paths: rime ashlar, moonstone lozenge and frost pinwheel.

These paths reuse the laid-path wear of `_paving` (settling, chipping, cracks,
lost stones, filled joints, ground creeping back) with the ground swapped for
snow, then `weather` them: wind-blown streaks, powder feathering out of the
drifts, refrozen glaze, and sparse warm sanding grit, the cue that tells a
road apart from the slow ice terrain.
"""
import numpy as np

from terrain_core import NATIVE_PX, domain_warp, fbm, lattice_noise, layout_field, mix, smoothstep

# Frost in place of the shared ground, keyed to the game's own snow: the
# periwinkle of deep_snow (dark 176,185,216 / mean 196,201,228 / light
# 214,217,240) fills drifts and lost stones, so a reclaimed stretch reads as
# the surrounding snow drifting back over the road. Joints are lavender slate;
# nothing uses the cyan of the slow `ice` terrain (196,221,235).
SNOW_DARK, SNOW, SNOW_LIGHT = (176, 185, 216), (196, 201, 228), (214, 217, 240)
FROST = {
    "joint": (120, 126, 162), "grit": (192, 198, 224), "soil": (184, 191, 221),
    "moss_dark": SNOW_DARK, "moss": SNOW, "moss_light": SNOW_LIGHT,
    "leaf": (224, 226, 244), "lichen": (206, 210, 234), "shadow": (66, 72, 108),
    "crack": (118, 126, 158), "dust": (204, 208, 230), "damp": (168, 186, 220),
    "rubble": (206, 210, 232), "stain": (150, 158, 190),
}
SEAM = {"height": 3, "cast_q8": 24, "cast_width_q8": 512}


def weather(ctx, rgb, edge, decay, *, streaks=0.5, glaze=0.3, drift_edge=0.5, sanding=0.5):
    """Weather a frozen road after `dress`: wind-blown snow in long streaks
    across the surface, settling deepest in joints and hollows; refrozen
    meltwater glazing damp stretches smooth; and the broad reclaimed patches
    feathered out with powder so drifts have no hard rim."""
    size = ctx.size
    edge = np.asarray(edge, dtype=np.float64)
    # Streaks: long in x, narrow in y, warped so they curl like blown snow.
    def streak_field(rng):
        f = fbm(rng, 2, 3, cells_y=10, size=size, array=True)
        return domain_warp(f, fbm(rng, 3, 2, size=size, array=True), fbm(rng, 3, 2, size=size, array=True), 24)
    s = layout_field(ctx, streak_field)
    low = smoothstep(1.5 * NATIVE_PX, -0.5, edge)  # joints and rims
    blown = smoothstep(0.52, 0.78, s) * (0.35 + 0.65 * low)
    rgb = mix(rgb, SNOW_LIGHT, blown, streaks)
    # Powder feathering out from each drift.
    halo = smoothstep(0.45, 0.75, decay.growth) * (1 - decay.patch)
    rgb = mix(rgb, SNOW, halo, drift_edge)
    # Glaze: damp stretches refreeze to a smooth, slightly darker sheen.
    sheen = smoothstep(0.62, 0.88, decay.damp)
    rgb = mix(rgb, (176, 190, 222), sheen, glaze)
    # Sanding grit: sparse warm specks scattered for footing, densest where
    # the road is most trodden and swept clear of drifts. This is the cue
    # that separates a road from the slow `ice` terrain.
    # Specks one to two native px across: coarse lattice cells, high threshold.
    cells = max(8, size // (5 * NATIVE_PX))
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, cells, size=size, array=True))
    trod = decay.wear * (1 - decay.patch)
    grit = smoothstep(0.93 - 0.04 * trod, 0.96 - 0.04 * trod, speck) * (0.3 + 0.7 * trod)
    rgb = mix(rgb, (150, 138, 124), grit, sanding)
    return rgb
