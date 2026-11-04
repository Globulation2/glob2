# SPDX-License-Identifier: GPL-3.0-or-later
"""The sandstone terrain material."""
import math

from terrain_core import (
    N,
    RECIPES,
    Style,
    fbm,
    group_cracks,
    lattice_noise,
    layout_field,
    mix,
    recipe,
    shade,
    smoothstep,
    worn_edge,
)


@recipe(name="sandstone", group="paths", label="Sandstone tiles", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"ochre": (176, 150, 92), "grey": (132, 130, 116), "joint": (72, 64, 54), "crack": (88, 78, 62),
                 "speck": (96, 86, 70)},
        style=Style(luma=136, std=18, grain_max=16, match=0.7), periodic=True, grid=8, blocks=4)
def render_sandstone(ctx):
    """Square sandstone flags, two by two per cell, ochre and grey in a checker,
    each with rounded corners, mottle, speckle and a few fine cracks, set in
    dark joints with the Paths group's worn lip. The flag grid is the same in
    every layout."""
    p, size = RECIPES["sandstone"].palette, ctx.size
    flag = N // 2
    per_row = size // flag
    jit = [ctx.shared_rng.uniform(0.9, 1.1) for _ in range(per_row * per_row)]
    joints, ids = [0.0] * (size * size), [0] * (size * size)
    for i in range(size * size):
        x, y = i % size, i // size
        fx, fy = x // flag, y // flag
        ids[i] = fy * per_row + fx
        lx, ly = x - fx * flag + 0.5, y - fy * flag + 0.5
        dx, dy = min(lx, flag - lx), min(ly, flag - ly)
        # Rounded corners: distance to an inset rounded square.
        r = 10.0
        cx, cy = max(0.0, r - dx), max(0.0, r - dy)
        edge = r - math.hypot(cx, cy) if dx < r and dy < r else min(dx, dy)
        joints[i] = 1 - smoothstep(2.0, 5.0, edge)
    base = [p["ochre"] if ((k // per_row) + (k % per_row)) % 2 == 0 else p["grey"] for k in range(per_row * per_row)]
    rgb = [[base[k][c] * jit[k] for k in ids] for c in range(3)]
    mottle = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    rgb = [[c * (0.88 + 0.24 * v) for c, v in zip(ch, mottle)] for ch in rgb]
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))
    rgb = mix(rgb, p["speck"], [smoothstep(0.78, 0.92, v) for v in speck], 0.5)
    cracks = group_cracks(ctx, cells=5, width=0.04, gaps=0.45)
    rgb = mix(rgb, p["crack"], cracks, 0.6)
    rgb = shade(rgb, [1 - j for j in joints], 0.25)
    rgb = mix(rgb, p["joint"], joints, 0.9)
    return worn_edge(rgb, joints, strength=0.22), None
