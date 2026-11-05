# SPDX-License-Identifier: GPL-3.0-or-later
"""The icefall terrain material: trampled snow and fallen ice under the raised icefall decor."""
from terrain_core import (
    Ctx,
    RECIPES,
    Style,
    clamp01,
    fbm,
    lattice_noise,
    mix,
    ramp,
    recipe,
    shade,
    soft_blobs,
)


@recipe(name="icefall", group="obstacles", label="Icefall", profile="canopy", border_class="cover",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"dark": (138, 160, 200), "mid": (184, 198, 226), "light": (214, 224, 242),
                 "ice": (120, 176, 214), "ice_core": (72, 128, 182)},
        style=Style(luma=192, std=12, grain_max=8, match=0.8), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_icefall(ctx):
    """Snow at the foot of the ice walls, a shade colder and duller than the
    wind-packed surface above, strewn with blocks of blue ice fallen from them."""
    p = RECIPES["icefall"].palette
    shared_rng, own_rng = ctx.shared_rng, ctx.rng
    common_raw = fbm(shared_rng, 4, 3, size=ctx.size)
    own_raw = fbm(own_rng, 4, 3, size=ctx.size)
    ordered = sorted(common_raw)
    low, high = ordered[int(0.02 * (len(ordered) - 1))], ordered[int(0.98 * (len(ordered) - 1))]
    span = high - low or 1.0
    common_height = [clamp01((v - low) / span) for v in common_raw]
    own_height = [clamp01((v - low) / span) for v in own_raw]
    interior = ctx.interior_mask
    height = [a + (b - a) * w for a, b, w in zip(common_height, own_height, interior)]
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(own_rng, 48, size=ctx.size)
    fine = [a + (b - a) * w for a, b, w in zip(common_grain, own_grain, interior)]
    rgb = [[c * (1 + 0.06 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    rgb = shade(rgb, height, 0.14, light=(-0.6, -0.8), reference_height=common_height)
    # Fallen ice: small blue blocks with darker hearts, hand-sized beside a glob.
    shared_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared_ctx.rng = shared_rng
    common_halo, common_core, _ = soft_blobs(shared_ctx, 10, 2.5, 4.5, core=1.5)
    own_halo, own_core, _ = soft_blobs(ctx, 10, 2.5, 4.5, core=1.5)
    halo = [a + (b - a) * w for a, b, w in zip(common_halo, own_halo, interior)]
    core = [a + (b - a) * w for a, b, w in zip(common_core, own_core, interior)]
    rgb = mix(rgb, p["ice"], halo, 0.75)
    rgb = mix(rgb, p["ice_core"], core, 0.6)
    return rgb, None
