# SPDX-License-Identifier: GPL-3.0-or-later
"""The volcanic ash terrain material."""
import math

from terrain_core import (
    N,
    RECIPES,
    Style,
    TAU,
    barren_mark,
    blend_fields,
    domain_warp,
    fbm,
    lattice_noise,
    mix,
    normalize,
    normalize_like,
    ramp,
    recipe,
    shifted,
    stamp_disc,
)


@recipe(name="volcanic_ash", group="barren", label="Volcanic ash", profile="soil", border_class="solid",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (58, 54, 50), "mid": (72, 67, 62), "light": (88, 82, 76), "glint": (140, 134, 126),
                 "stone": (26, 24, 24), "stone_lit": (92, 88, 84), "pore": (12, 11, 11), "rust": (176, 88, 32),
                 "crack": (22, 20, 19)},
        style=Style(luma=66, std=9.1, grain_max=13), periodic=True, grid=8, blocks=4)
def render_volcanic_ash(ctx):
    """Dark charcoal fine-grained ash with scattered porous black stones and
    occasional rusty mineral specks, under the barren group's mark: dense
    one-pixel grit over broad light and dark patches, near-black pitted
    stones with a pinprick highlight, sparse pale flecks in clusters."""
    p = RECIPES["volcanic_ash"].palette
    size = ctx.size
    common, common_broad, common_fine, common_stones, common_pores, common_glints, common_rust = ash_fields(
        ctx.shared_rng, size)
    own, own_broad, own_fine, own_stones, own_pores, own_glints, own_rust = ash_fields(ctx.rng, size)
    mask = ctx.interior_mask
    soil = normalize_like(blend_fields(common, own, mask), common, low=0.02, high=0.98)
    rgb = ramp(soil, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    broad = blend_fields(common_broad, own_broad, mask)
    rgb = [[c * (0.88 + 0.24 * v) for c, v in zip(channel, broad)] for channel in rgb]
    fine = blend_fields(common_fine, own_fine, mask)
    rgb = [[c * (1 + 0.22 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    rgb = mix(rgb, p["glint"], blend_fields(common_glints, own_glints, mask), 0.75)
    stones = blend_fields(common_stones, own_stones, mask)
    rgb = mix(rgb, p["stone"], stones, 0.92)
    # A faint lit top-left edge lifts each stone off the soil.
    rim = [max(0.0, s - t) for s, t in zip(stones, shifted(stones, -3, -3))]
    rgb = mix(rgb, p["stone_lit"], rim, 0.7)
    pores = blend_fields(common_pores, own_pores, mask)
    rgb = mix(rgb, p["pore"], [v * s for v, s in zip(pores, stones)], 0.8)
    rgb = mix(rgb, p["rust"], blend_fields(common_rust, own_rust, mask), 0.85)
    return barren_mark(ctx, rgb, p["crack"], cells=6, fade=0.18), None


def ash_fields(rng, size):
    """Periodic ash soil, broad patches, one-pixel grit, irregular stones (each
    with a pinprick highlight and pits), clustered pale flecks and sparse
    clusters of rust specks."""
    scale = (size // N) ** 2
    soil = fbm(rng, 6, 3, size=size)
    soil = domain_warp(soil, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    broad = normalize(fbm(rng, 2, 2, size=size))
    fine = lattice_noise(rng, 48, size=size)
    stones, pores = [0.0] * (size * size), [0.0] * (size * size)
    glints, rust = [0.0] * (size * size), [0.0] * (size * size)
    for _ in range(10 * scale):
        x, y = rng.random() * size, rng.random() * size
        r = rng.uniform(4.5, 8.0)
        # Irregular: a few overlapping lobes rather than one round disc.
        for _ in range(rng.randint(2, 4)):
            a = rng.random() * TAU
            stamp_disc(stones, x + 0.45 * r * math.cos(a), y + 0.45 * r * math.sin(a), r * rng.uniform(0.6, 0.9),
                       softness=0.35)
        for _ in range(rng.randint(1, 3)):
            a, d = rng.random() * TAU, rng.uniform(0.0, 0.4) * r
            stamp_disc(pores, x + d * math.cos(a), y + d * math.sin(a), rng.uniform(1.0, 1.6), softness=0.4)
        stamp_disc(glints, x - 0.35 * r, y - 0.35 * r, 2.2, softness=0.4)
    for _ in range(8 * scale):
        x, y = rng.random() * size, rng.random() * size
        for _ in range(rng.randint(1, 3)):
            stamp_disc(glints, x + rng.uniform(-14, 14), y + rng.uniform(-14, 14), rng.uniform(2.2, 3.0), softness=0.4)
    for _ in range(scale + scale // 2):
        x, y = rng.random() * size, rng.random() * size
        for _ in range(rng.randint(1, 2)):
            stamp_disc(rust, x + rng.uniform(-8, 8), y + rng.uniform(-8, 8), rng.uniform(3.0, 4.2), softness=0.35)
    return soil, broad, fine, stones, pores, glints, rust
