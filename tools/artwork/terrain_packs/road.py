# SPDX-License-Identifier: GPL-3.0-or-later
"""The road terrain material."""
import bisect
import math

from terrain_core import (
    RECIPES,
    Style,
    clamp01,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shade,
    smoothstep,
    worn_edge,
)


@recipe(name='road', group="legacy", label='Road', profile="paved", border_class="built",
        seam={'height': 3, 'cast_q8': 48, 'cast_width_q8': 512}, palette={'dark': (95, 84, 71), 'mid': (135, 122, 103), 'light': (165, 149, 128)}, style=Style(luma=121.2, std=23.2, grain_max=22.9),
        periodic=True, grid=8, blocks=4, preview=(133, 120, 101), minimap=(176, 138, 98))
def render_road(ctx):
    """Laid stone setts in straight courses, with straight mortar joints.

    Course and joint geometry is periodic and common to every layout, so no
    layout can cut a boundary stone; only stone tone and texture vary inside.
    Each joint takes the paths group's crisp, lighter worn lip.
    """
    p, size = RECIPES["road"].palette, ctx.size
    course = 32  # HD pixels per course: eight native pixels, dividing every block.
    rows = size // course
    joints = []
    for _ in range(rows):
        # Joint positions for one course, wrapping once around the block.
        start = ctx.shared_rng.random() * size
        row, x = [start], start
        while True:
            length = ctx.shared_rng.uniform(30.0, 58.0)
            if start + size - (x + length) < 30.0:
                break
            x += length
            row.append(x)
        joints.append(row)
    stones = sum(len(row) for row in joints)
    common_tones = [ctx.shared_rng.random() for _ in range(stones)]
    own_tones = [ctx.rng.random() for _ in range(stones)]
    height, mortar, tone = [0.0] * (size * size), [0.0] * (size * size), [0.0] * (size * size)
    first = 0
    for k, row in enumerate(joints):
        starts = row + [row[0] + size]
        for y in range(k * course, (k + 1) * course):
            dy = min(y - k * course, (k + 1) * course - 1 - y) + 0.5
            for x in range(size):
                ux = x + 0.5 if x + 0.5 >= row[0] else x + 0.5 + size
                stone = bisect.bisect_right(starts, ux) - 1
                dx = min(ux - starts[stone], starts[stone + 1] - ux)
                i = y * size + x
                # Rounded corners: worn setts, not sawn bricks.
                edge = min(dx, dy) if min(dx, dy) > 5 else 5 - math.hypot(max(0.0, 5 - dx), max(0.0, 5 - dy))
                mortar[i] = 1 - smoothstep(1.5, 3.0, edge)
                height[i] = smoothstep(1.5, 9.0, edge)
                w = ctx.interior_mask[i]
                tone[i] = common_tones[first + stone] + (own_tones[first + stone] - common_tones[first + stone]) * w
        first += len(row)
    texture = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    shade_tone = [clamp01(0.45 * v + 0.55 * s) for v, s in zip(texture, tone)]
    rgb = ramp(shade_tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, height, 0.22)
    rgb = mix(rgb, tuple(c * 0.62 for c in p["dark"]), mortar, 0.9)
    rgb = worn_edge(rgb, mortar)
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.09 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    return rgb, None
