# SPDX-License-Identifier: GPL-3.0-or-later
"""The molten lava pool terrain material: a brighter, hotter lava."""
import math

import numpy as np

from terrain_core import (
    RECIPES,
    Style,
    TAU,
    blend_fields,
    block_worley,
    domain_warp,
    fbm,
    gradient_light,
    layout_field,
    mix,
    ramp,
    recipe,
    shade,
)


@recipe(name="molten_lava_pool", group="lava", label="Molten lava pool", profile="molten", border_class="flow",
        seam={"height": 4, "fringe": [255, 120, 10], "fringe_q8": 160, "fringe_width_q8": 768},
        palette={"crust_deep": (84, 12, 3), "crust": (152, 30, 7), "crust_hot": (204, 54, 10),
                 "seep": (246, 104, 8), "crack": (232, 76, 8), "flow": (255, 184, 0),
                 "flow_core": (255, 230, 40), "flow_white": (255, 246, 150), "ridge": (226, 84, 22)},
        style=Style(luma=96, std=32, grain_max=20, match=0.5), phases=4, animation_ticks=16,
        blend_steps=4, periodic=True, grid=8, blocks=4,
        preview=(183, 63, 12), minimap=(214, 74, 14))
def render_molten_lava_pool(ctx):
    """Red-hot crust plates floating on a molten pool: a few wide yellow trunk
    channels tapering into branches, an orange halo bleeding into each plate's
    rim, connected orange secondary cracks and lumpy embossed crust."""
    p, size = RECIPES["molten_lava_pool"].palette, ctx.size
    # Plates about a tile across, split again by a secondary crack net.
    f1, f2, ids, common1, common2, common_ids = block_worley(ctx, 1, array=True)
    g1, g2, _, common_g1, common_g2, _ = block_worley(ctx, 5, array=True)

    def warped(own1, own2, shared1, shared2, cells, amount):
        # Warp the shared and own cell edges separately, so the warp never
        # carries a layout's interior cells into the common block edge.
        sx = fbm(ctx.shared_rng, cells, 3, size=size, array=True)
        sy = fbm(ctx.shared_rng, cells, 3, size=size, array=True)
        ox = fbm(ctx.rng, cells, 3, size=size, array=True)
        oy = fbm(ctx.rng, cells, 3, size=size, array=True)
        shared = domain_warp(shared2 - shared1, sx, sy, amount)
        own = domain_warp(own2 - own1, ox, oy, amount)
        return blend_fields(shared, own, ctx.interior_mask)

    edge = warped(f1, f2, common1, common2, 3, 18)
    fine = warped(g1, g2, common_g1, common_g2, 6, 5 * size / 512)
    # Hierarchy: a broad field picks trunks, branches and thin seams; frothy
    # high-frequency grit breaks every bank.
    grit = layout_field(ctx, lambda rng: fbm(rng, 40, 2, size=size, array=True))
    trunk = layout_field(ctx, lambda rng: fbm(rng, 2, 2, size=size, array=True))
    rank = np.clip((trunk - 0.4) / 0.3, 0, 1) ** 2
    channel_w = 0.016 + 0.07 * rank
    e = edge - (grit - 0.5) * (0.03 + 0.04 * rank)
    channel = 1 - np.clip((e - channel_w) / 0.015, 0, 1)
    core = 1 - np.clip((e - channel_w * 0.25) / (channel_w * 0.5 + 1e-3), 0, 1)
    halo = np.exp(-np.maximum(e - channel_w, 0) / (0.07 + 0.08 * rank))
    # Secondary cracks: connected, thin and orange, a few gated away.
    gate = np.clip((layout_field(ctx, lambda rng: fbm(rng, 4, 2, size=size, array=True)) - 0.3) / 0.15, 0, 1)
    f = fine - (grit - 0.5) * 0.02
    crack = (1 - np.clip(f / 0.025, 0, 1)) * gate
    crack_halo = np.exp(-f / 0.07) * gate
    # Crust: saturated red at mid value, lumpy relief with warm lit ridges.
    def lumpy(rng):
        return domain_warp(fbm(rng, 5, 4, size=size, array=True), fbm(rng, 3, 2, size=size, array=True),
                           fbm(rng, 3, 2, size=size, array=True), 12)

    common_lumps = lumpy(ctx.shared_rng)
    lumps = blend_fields(common_lumps, lumpy(ctx.rng), ctx.interior_mask)
    heat = np.clip(0.55 * halo + 0.3 * crack_halo + 0.45 * (lumps - 0.5) + 0.3, 0, 1)
    rgb = ramp(heat, [(0.0, p["crust_deep"]), (0.4, p["crust"]), (0.78, p["crust_hot"]), (1.0, p["seep"])])
    # Light on the shared scale, so every layout's block edge stays identical.
    lit = np.maximum(0, -gradient_light(lumps, reference_height=common_lumps))
    rgb = mix(rgb, p["ridge"], np.clip(lit * 6, 0, 1), 0.35)
    rgb = shade(rgb, lumps, 0.18, reference_height=common_lumps)
    rgb = mix(rgb, p["crack"], crack, 0.8)
    # Animation: each plate's seams pulse on their own clock, with a faint
    # periodic shimmer travelling along the channels. With four phases each
    # step is a quarter turn, so the swing stays narrow enough not to flash.
    time = (ctx.phase % ctx.phases) / ctx.phases
    offsets = np.array([ctx.shared_rng.random() for _ in range(ctx.area_scale)])
    pulse = blend_fields(0.5 + 0.5 * np.sin(TAU * (time + offsets[common_ids])),
                         0.5 + 0.5 * np.sin(TAU * (time + offsets[ids])), ctx.interior_mask)
    sa = layout_field(ctx, lambda rng: fbm(rng, 12, 2, size=size, array=True))
    sb = layout_field(ctx, lambda rng: fbm(rng, 12, 2, size=size, array=True))
    shimmer = math.cos(TAU * time) * (sa - 0.5) + math.sin(TAU * time) * (sb - 0.5)
    hot = np.clip(0.15 + 0.55 * pulse + 0.7 * shimmer + 0.25 * rank, 0, 1)
    # The halo breathes with its channel, so plate rims flare and cool too.
    rgb = mix(rgb, p["seep"], np.clip(halo * 1.4 - 0.4, 0, 1) * (0.3 + 0.55 * pulse), 1.0)
    flow = ramp(hot, [(0.0, p["seep"]), (0.5, p["flow"]), (1.0, p["flow_core"])])
    rgb = mix(rgb, flow, channel, 1.0)
    rgb = mix(rgb, p["flow_white"], core * hot * (0.3 + 0.7 * rank), 0.7)
    return rgb, None
