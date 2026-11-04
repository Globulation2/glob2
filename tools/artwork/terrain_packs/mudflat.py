# SPDX-License-Identifier: GPL-3.0-or-later
"""The mudflat terrain material."""
from terrain_core import (
    RECIPES,
    Style,
    apply_cell_jitter,
    blend_fields,
    block_worley,
    cell_jitter,
    domain_warp,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shade,
    smoothstep,
    wet_pits,
)


@recipe(name="mudflat", group="rough", label="Mudflat", profile="soil", border_class="solid", seam={"height": 1},
        palette={"dark": (128, 114, 96), "mid": (164, 148, 126), "light": (186, 172, 150),
                 "gap": (52, 44, 36), "damp": (96, 84, 70), "salt": (222, 216, 204), "glint": (200, 196, 186)},
        style=Style(luma=146, std=20, grain_max=14, match=0.9), periodic=True, grid=8, blocks=4)
def render_mudflat(ctx):
    """Desiccated mudflat: big curled plates split by deep dark cracks, salt
    bloom on top and damp mud in a few sunken plates."""
    p, size = RECIPES["mudflat"].palette, ctx.size
    # Plates about two tiles across; one in twelve has sunk into a damp hollow.
    f1, f2, ids, c1, c2, cids = block_worley(ctx, 3)
    count = 3 * ctx.area_scale
    sunk = [ctx.shared_rng.random() < 0.08 for _ in range(count)]

    def plate(a, b, i):
        e = b - a
        if sunk[i]:
            return -0.6
        # Flat top that curls up toward its rim, then drops into the crack.
        return smoothstep(0.03, 0.09, e) * (0.65 + 0.35 * (1 - smoothstep(0.09, 0.35, e)))

    height = [plate(a, b, i) for a, b, i in zip(f1, f2, ids)]
    common = [plate(a, b, i) for a, b, i in zip(c1, c2, cids)]
    # Bend the straight Voronoi cracks; the reference takes the common warp so
    # every layout lights against the same field.
    cx, cy = fbm(ctx.shared_rng, 6, 2, size=size), fbm(ctx.shared_rng, 6, 2, size=size)
    sx = blend_fields(cx, fbm(ctx.rng, 6, 2, size=size), ctx.interior_mask)
    sy = blend_fields(cy, fbm(ctx.rng, 6, 2, size=size), ctx.interior_mask)
    height, common = domain_warp(height, sx, sy, 10), domain_warp(common, cx, cy, 10)
    # Dry crust with salt bloom, each plate faintly its own tone.
    tone = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, count, 0.06))
    salt = layout_field(ctx, lambda rng: fbm(rng, 10, 2, size=size))
    rgb = mix(rgb, p["salt"], [smoothstep(0.62, 0.8, v) for v in salt], 0.45)
    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.1 * (v - 0.5) * 2) for c, v in zip(ch, fine)] for ch in rgb]
    rgb = shade(rgb, height, 0.45, reference_height=common)
    # Deep dark cracks between the plates; damp mud in the sunken ones.
    crack = [1 - smoothstep(0.05, 0.35, h) for h in height]
    rgb = mix(rgb, p["gap"], crack, 0.95)
    pit = [smoothstep(0.1, 0.4, -h) for h in height]
    return wet_pits(rgb, height, common, pit, p["damp"], p["glint"], depth=0.6), None
