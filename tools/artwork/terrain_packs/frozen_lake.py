# SPDX-License-Identifier: GPL-3.0-or-later
"""The frozen lake terrain material."""
import numpy as np

from terrain_core import (
    N,
    RECIPES,
    Style,
    blend_fields,
    block_worley,
    domain_warp,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
    stamp_disc,
)


def warped_edges(ctx, cells, warp, cells_warp=4):
    """Long, wandering fracture lines: Worley edges warped, common at the block edge."""
    size = ctx.size
    f1, f2, _, c1, c2, _ = block_worley(ctx, cells, array=True)
    cx = fbm(ctx.shared_rng, cells_warp, 2, size=size, array=True)
    cy = fbm(ctx.shared_rng, cells_warp, 2, size=size, array=True)
    wx = blend_fields(cx, fbm(ctx.rng, cells_warp, 2, size=size, array=True), ctx.interior_mask)
    wy = blend_fields(cy, fbm(ctx.rng, cells_warp, 2, size=size, array=True), ctx.interior_mask)
    return domain_warp(f2 - f1, wx, wy, warp)


def bubbles(ctx):
    """Clusters of small air bubbles trapped in the ice."""
    size = ctx.size

    def make(rng):
        mask = np.zeros(size * size)
        for _ in range(3 * (size // N) ** 2):
            x, y = rng.random() * size, rng.random() * size
            for _ in range(rng.randint(5, 12)):
                stamp_disc(mask, x + rng.gauss(0, 7), y + rng.gauss(0, 7), rng.uniform(1.0, 2.6), softness=0.4)
        return mask
    return layout_field(ctx, make)


@recipe(name="frozen_lake", group="ice", label="Frozen lake", profile="frozen", border_class="solid",
        seam={'height': 4, 'cast_q8': 64, 'cast_width_q8': 640, 'fringe': [228, 240, 248], 'fringe_q8': 96,
              'fringe_width_q8': 384},
        palette={"deep": (88, 126, 146), "mid": (130, 168, 184), "pale": (178, 206, 216), "glint": (226, 238, 242),
                 "crack": (206, 226, 232), "white": (242, 248, 250), "frost": (226, 236, 240)},
        style=Style(luma=160, std=18, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4)
def render_frozen_lake(ctx):
    """Frosted lake ice: teal depth under a thin white bloom, a few long white
    pressure cracks, trapped bubbles and patches of frost. It plays exactly
    like ice and stays static, pale and grey-white beside any water."""
    p, size = RECIPES["frozen_lake"].palette, ctx.size
    # Depth: broad clear (dark) and cloudy (pale) zones of thick lake ice.
    depth = layout_field(ctx, lambda rng: fbm(rng, 3, 3, size=size, array=True))
    rgb = ramp(depth, [(0.0, p["deep"]), (0.55, p["mid"]), (1.0, p["pale"])])
    # Cold streaks frozen into the ice.
    streak = layout_field(ctx, lambda rng: fbm(rng, 4, 3, cells_y=14, size=size, array=True))
    rgb = mix(rgb, p["pale"], smoothstep(0.55, 0.85, streak), 0.25)
    # Soft specular sheen: a broad diagonal glint.
    sheen = layout_field(ctx, lambda rng: fbm(rng, 2, 2, size=size, array=True))
    rgb = mix(rgb, p["glint"], smoothstep(0.6, 0.9, sheen), 0.35)
    # Pressure cracks: few, long and bright; a fine fracture net under them.
    major = warped_edges(ctx, 1, 18, 3)
    fine = warped_edges(ctx, 6, 6, 8)
    gate = layout_field(ctx, lambda rng: fbm(rng, 5, 2, size=size, array=True))
    net = (1 - smoothstep(0.012, 0.03, fine)) * smoothstep(0.42, 0.55, gate)
    rgb = mix(rgb, p["crack"], net, 0.27)
    # Only some ridges survive: long pressure cracks, not a full net.
    keep = layout_field(ctx, lambda rng: fbm(rng, 2, 2, size=size, array=True))
    ridge = smoothstep(0.45, 0.6, keep)
    line = (1 - smoothstep(0.01, 0.035, major)) * ridge
    halo = (1 - smoothstep(0.02, 0.09, major)) * ridge
    rgb = mix(rgb, p["crack"], halo, 0.225)
    rgb = mix(rgb, p["white"], line, 0.765)
    rgb = mix(rgb, p["white"], bubbles(ctx), 0.7)
    # Frost and wind-blown snow dusting in patches.
    frost = layout_field(ctx, lambda rng: fbm(rng, 6, 3, size=size, array=True))
    grain = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size, array=True))
    dust = smoothstep(0.62, 0.8, frost) * (0.6 + 0.4 * grain)
    rgb = mix(rgb, p["frost"], dust, 0.7)
    return rgb, None
