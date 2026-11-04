# SPDX-License-Identifier: GPL-3.0-or-later
"""The boardwalk terrain material."""
from terrain_core import (
    N,
    RECIPES,
    Style,
    clamp01,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
    stamp_disc,
    worn_edge,
)


@recipe(name="boardwalk", group="paths", label="Boardwalk", profile="crisp", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (92, 84, 74), "mid": (134, 124, 108), "light": (164, 154, 136), "gap": (40, 36, 32),
                 "knot": (70, 62, 54), "nail": (52, 50, 50)},
        style=Style(luma=122, std=14, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4,
        note="planks run one way: accepted compromise")
def render_boardwalk(ctx):
    """Long weathered planks, silvered grey-brown, with grain, knots and nails.

    Planks are about 8 native px wide and 2.5 to 4.5 cells long. Plank ends,
    knots and per-plank brightness come from the shared stream, so every
    layout has the same joints and no layout fades between planks that do
    not line up; only the wood grain inside varies."""
    p, size = RECIPES["boardwalk"].palette, ctx.size
    shared, own = ctx.shared_rng, ctx.rng
    rows = 4 * (size // N)
    width = size / rows
    # Plank ends: each row gets joints 2.5-4.5 cells apart, wrapping the block.
    ends, shifts, bright = [], [], []
    for _ in range(rows):
        x, row = shared.random() * size, []
        start = x
        while True:
            row.append(x % size)
            x += shared.uniform(2.5, 4.5) * N
            if x - start > size - 2.0 * N:
                break
        ends.append(sorted(row))
        shifts.append([int(shared.random() * size) for _ in row])
        bright.append([1 + (shared.random() - 0.5) * 0.22 for _ in row])
    grain_common = fbm(shared, 2, 3, cells_y=40, size=size)
    grain_own = fbm(own, 2, 3, cells_y=40, size=size)
    streak_c = lattice_noise(shared, 4, 64, size=size)
    streak_o = lattice_noise(own, 4, 64, size=size)
    interior = ctx.interior_mask
    tone, gaps, knots, nails = ([0.0] * (size * size) for _ in range(4))
    # Most planks carry a knot somewhere along their length.
    knot_list = []
    for k in range(rows):
        for e in ends[k]:
            if shared.random() < 0.6:
                knot_list.append((e + shared.uniform(0.6, 2.0) * N, (k + shared.uniform(0.3, 0.7)) * width,
                                  shared.uniform(5, 9)))
    for kx, ky, kr in knot_list:
        stamp_disc(knots, kx % size, ky % size, kr, softness=0.6)
    # Each pixel samples its plank's own stretch of grain, so neighbouring
    # planks never share a grain line; joints and nail pairs sit at the ends.
    for i in range(size * size):
        x, y = i % size, i // size
        k = min(rows - 1, int(y / width))
        row = ends[k]
        seg = max((j for j, e in enumerate(row) if e <= x), default=len(row) - 1)
        sample = y * size + (x + shifts[k][seg]) % size
        w = interior[i]
        g = grain_common[sample] + (grain_own[sample] - grain_common[sample]) * w
        st = streak_c[sample] + (streak_o[sample] - streak_c[sample]) * w
        tone[i] = clamp01((0.45 * g + 0.55 * st) * bright[k][seg])
        edge = min(y - k * width, (k + 1) * width - y)
        gap = 1 - smoothstep(1.0, 3.5, edge + 0.5)
        jd = min(abs(((x + 0.5 - e) + size / 2) % size - size / 2) for e in row)
        gaps[i] = max(gap, 1 - smoothstep(1.0, 2.5, jd))
        nail_y = abs(edge - width * 0.28)
        if 5.0 < jd < 8.0 and nail_y < 1.6:
            nails[i] = 1.0
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 6, 128, size=size))
    rgb = [[c * (1 + 0.16 * (v - 0.5) * 2) for c, v in zip(ch, fine)] for ch in rgb]
    rgb = mix(rgb, p["knot"], knots, 0.55)
    rgb = mix(rgb, p["nail"], nails, 0.5)
    rgb = mix(rgb, p["gap"], gaps, 0.9)
    return worn_edge(rgb, gaps, strength=0.2), None
