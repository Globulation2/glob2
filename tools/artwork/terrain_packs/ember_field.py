# SPDX-License-Identifier: GPL-3.0-or-later
"""The ember field terrain material."""
import math

from terrain_core import (
    RECIPES,
    Style,
    TAU,
    band,
    blend_fields,
    block_object_field,
    block_worley,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize_like,
    ramp,
    recipe,
    stamp_disc,
)


@recipe(name="ember_field", group="lava", label="Ember field", profile="molten", border_class="flow",
        seam={"height": 4, "fringe": [160, 80, 40], "fringe_q8": 96, "fringe_width_q8": 512},
        palette={"dark": (50, 28, 24), "mid": (60, 34, 28), "light": (80, 44, 32),
                 "ember_dim": (120, 50, 30), "ember": (230, 120, 40), "ember_core": (250, 200, 80)},
        style=Style(luma=48, std=10, grain_max=9, match=0.7), phases=4, animation_ticks=8,
        blend_steps=4, periodic=True, grid=8, blocks=4,
        preview=(110, 52, 36), minimap=(120, 48, 30),
        note="placeholder until the image-generated ember field lands")
def render_ember_field(ctx):
    """Common boundary fractures and whole embers, varied interior crust."""
    p = RECIPES["ember_field"].palette
    common, common_grain = ember_field_soil(ctx.shared_rng, ctx.size)
    own, own_grain = ember_field_soil(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    # Cracks carrying a dim steady glow, sharing lava's DNA.
    f1, f2, _, _, _, _ = block_worley(ctx, 16)
    common_x, common_y = fbm(ctx.shared_rng, 4, 2, size=ctx.size), fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    own_x, own_y = fbm(ctx.rng, 4, 2, size=ctx.size), fbm(ctx.rng, 4, 2, size=ctx.size)
    edge = domain_warp([b - a for a, b in zip(f1, f2)],
                       blend_fields(common_x, own_x, ctx.interior_mask),
                       blend_fields(common_y, own_y, ctx.interior_mask), 6)
    cracks = band(edge, 0.03, 0.04)
    rgb = mix(rgb, p["ember_dim"], cracks, 0.35)
    # Ten embers per native tile matches the former eight-to-twelve average.
    # Perimeter embers are whole shared objects rather than faded RGB images.
    shared, own = block_object_field(ctx, 10, (3.5, 5.0), spacing=0.85)
    glow = [0.0] * (ctx.size * ctx.size)
    core = [0.0] * (ctx.size * ctx.size)
    for cx, cy, r, offset in shared + own:
        intensity = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(TAU * (ctx.t % 1.0 + offset)))
        stamp_disc(glow, cx, cy, r * 2.6, intensity * 0.7, 0.9)
        stamp_disc(core, cx, cy, r, intensity, 0.6)
    rgb = mix(rgb, p["ember_dim"], glow, 1.0)
    ember_color = [
        [a + (b - a) * c for c in core] for a, b in zip(p["ember"], p["ember_core"])
    ]
    rgb = mix(rgb, ember_color, core, 1.0)
    return rgb, None


def ember_field_soil(rng, size):
    """Periodic crust height and fine grain, independent of animation time."""
    height = fbm(rng, 5, 4, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    return height, lattice_noise(rng, 48, size=size)
