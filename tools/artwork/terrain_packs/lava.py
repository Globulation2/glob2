# SPDX-License-Identifier: GPL-3.0-or-later
"""The lava terrain material."""
import math

from terrain_core import (
    RECIPES,
    Style,
    TAU,
    band,
    blend_fields,
    block_worley,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    shade,
)


@recipe(name="lava", group="lava", label="Lava", profile="molten", border_class="flow",
        seam={"height": 4, "fringe": [214, 110, 40], "fringe_q8": 128, "fringe_width_q8": 640},
        palette={"crust_dark": (36, 18, 16), "crust": (50, 24, 20), "crust_warm": (96, 42, 26),
                 "glow": (170, 70, 24), "glow_bright": (220, 150, 60)},
        style=Style(luma=58, std=17, grain_max=13, match=0.8), phases=4, animation_ticks=8,
        blend_steps=4, periodic=True, grid=8, blocks=4,
        preview=(168, 72, 30), minimap=(176, 68, 28),
        note="placeholder until the image-generated lava lands")
def render_lava(ctx):
    """Whole common boundary fractures with independently seeded interior crust."""
    p = RECIPES["lava"].palette
    f1, f2, ids, common1, common2, _ = block_worley(ctx, 4)
    common_wx = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    common_wy = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    own_wx = fbm(ctx.rng, 4, 2, size=ctx.size)
    own_wy = fbm(ctx.rng, 4, 2, size=ctx.size)
    common_edge = domain_warp([b - a for a, b in zip(common1, common2)], common_wx, common_wy, 10)
    own_edge = domain_warp([b - a for a, b in zip(f1, f2)], own_wx, own_wy, 10)
    edge = blend_fields(common_edge, own_edge, ctx.interior_mask)
    common_crust = fbm(ctx.shared_rng, 6, 3, size=ctx.size)
    own_crust = fbm(ctx.rng, 6, 3, size=ctx.size)
    crust_noise = normalize_like(blend_fields(common_crust, own_crust, ctx.interior_mask), common_crust)
    rgb = ramp(crust_noise, [(0.0, p["crust_dark"]), (1.0, p["crust"])])
    rgb = shade(rgb, [1 - v for v in f1], 0.2, reference_height=[1 - v for v in common1])
    common_grit = lattice_noise(ctx.shared_rng, 48, size=ctx.size)
    own_grit = lattice_noise(ctx.rng, 48, size=ctx.size)
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = [[c * (1 + 0.10 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    channel = band(edge, 0.03, 0.045)
    warm = band(edge, 0.08, 0.10)
    rgb = mix(rgb, p["crust_warm"], warm, 0.35)
    # IDs on the common boundary refer to the same crust plates in every layout.
    offsets = [ctx.shared_rng.random() for _ in range(4 * ctx.area_scale)]
    time = (ctx.phase % ctx.phases) / ctx.phases
    pulse = [0.5 + 0.5 * math.sin(TAU * (time + offsets[i])) for i in ids]
    glow = [[a + (b - a) * q for q in pulse] for a, b in zip(p["glow"], p["glow_bright"])]
    return mix(rgb, glow, channel, 0.9), None
