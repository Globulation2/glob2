# SPDX-License-Identifier: GPL-3.0-or-later
"""The cliffs terrain material: talus under the raised cliff decor."""
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

from .scree import SCREE_LAYERS, SCREE_TONES, scree_fields, wrap_blur


@recipe(name="cliffs", group="obstacles", label="Cliffs", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"void": (28, 20, 16), "ground": (72, 56, 44), "dark": (92, 72, 56),
                 "mid": (124, 98, 74), "light": (156, 128, 96)},
        style=Style(luma=84, std=18, grain_max=14, match=0.8), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_cliffs(ctx):
    """Talus at the foot of the cliff walls: the chips and cobbles of scree,
    without its dominant blocks, in the ochre stone of the cliff faces."""
    p, size = RECIPES["cliffs"].palette, ctx.size
    top, common_top, layer, fragment = scree_fields(ctx, SCREE_LAYERS[:2])
    rock = layer >= 0
    tones = np.array([ctx.shared_rng.random() for _ in range(SCREE_TONES)])
    key = (fragment * 7 + layer * 1031 + 3) % SCREE_TONES
    tone = np.where(rock, 0.3 + 0.55 * tones[key], 0.0)
    rgb = ramp(as_list(tone), [(0.0, p["ground"]), (0.3, p["dark"]), (0.58, p["mid"]), (0.85, p["light"])])
    grit = np.array(layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))).reshape(size, size)
    fine = as_list(np.where(rock, 0.06, 0.22) * (grit - 0.5) * 2)
    rgb = [[c * (1 + f) for c, f in zip(channel, fine)] for channel in rgb]
    (*rgb, height), (common,) = jag(ctx, [*rgb, as_list(top)], [as_list(common_top)], 3, cells=6)
    rgb = shade(rgb, height, 0.85, reference_height=common)
    h = np.array(height).reshape(size, size)
    occlusion = np.clip((wrap_blur(h, 6) - h) / 6.0, 0, 1)
    rgb = mix(rgb, p["void"], as_list(occlusion), 0.9)
    return cast(rgb, height, p["void"], reach=4, strength=0.5), None
