# SPDX-License-Identifier: GPL-3.0-or-later
"""The glacier terrain material: moraine on snow under the raised glacier decor."""
import numpy as np

from terrain_core import (
    RECIPES,
    Style,
    as_list,
    cast,
    jag,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shade,
)

from .scree import SCREE_TONES, TALUS_LAYERS, scree_fields, wrap_blur

# The talus chips and cobbles, thinned out so snow shows between them.
MORAINE_LAYERS = [dict(TALUS_LAYERS[0], keep=0.45), dict(TALUS_LAYERS[1], keep=0.3)]


@recipe(name="glacier", group="obstacles", label="Glacier", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"void": (70, 88, 116), "snow": (204, 214, 232), "drift": (226, 232, 244),
                 "dark": (72, 76, 84), "mid": (106, 110, 118), "light": (140, 144, 150)},
        style=Style(luma=166, std=35, grain_max=15, match=1.0), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_glacier(ctx):
    """Moraine at the foot of the ice walls: the chips and cobbles of the talus
    under cliffs, in cold grey stone, with packed snow between them."""
    p, size = RECIPES["glacier"].palette, ctx.size
    top, common_top, layer, fragment = scree_fields(ctx, MORAINE_LAYERS)
    rock = layer >= 0
    tones = np.array([ctx.shared_rng.random() for _ in range(SCREE_TONES)])
    key = (fragment * 7 + layer * 1031 + 3) % SCREE_TONES
    tone = np.where(rock, 0.1 + 0.7 * tones[key], 0.0)
    rgb = ramp(as_list(tone), [(0.0, p["snow"]), (0.01, p["dark"]), (0.45, p["mid"]), (0.8, p["light"])])
    # Wind-packed drifts across the snow between the stones.
    drift = np.array(layout_field(ctx, lambda rng: lattice_noise(rng, 6, size=size))).reshape(size, size)
    rgb = mix(rgb, p["drift"], as_list(np.where(rock, 0.0, np.clip((drift - 0.45) * 2.5, 0, 1))), 0.8)
    grit = np.array(layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))).reshape(size, size)
    fine = as_list(np.where(rock, 0.06, 0.04) * (grit - 0.5) * 2)
    rgb = [[c * (1 + f) for c, f in zip(channel, fine)] for channel in rgb]
    (*rgb, height), (common,) = jag(ctx, [*rgb, as_list(top)], [as_list(common_top)], 3, cells=6)
    rgb = shade(rgb, height, 0.85, reference_height=common)
    h = np.array(height).reshape(size, size)
    # Blue snow shade around the stones, lighter than talus's dark gaps.
    occlusion = np.clip((wrap_blur(h, 6) - h) / 6.0, 0, 1)
    rgb = mix(rgb, p["void"], as_list(occlusion), 0.55)
    return cast(rgb, height, p["void"], reach=4, strength=0.35), None
