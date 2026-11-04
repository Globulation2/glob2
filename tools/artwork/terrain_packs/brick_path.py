# SPDX-License-Identifier: GPL-3.0-or-later
"""The brick path terrain material."""
import math

from terrain_core import (
    NATIVE_PX,
    RECIPES,
    Style,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    recipe,
    shade,
    smoothstep,
    worn_edge,
)


# Brick firings, after the reference: red, plum, grey-brown, dark, rust.
BRICK_FIRINGS = [
    ((156, 84, 68), 3), ((116, 82, 76), 3), ((118, 102, 88), 3),
    ((94, 68, 64), 2), ((166, 96, 70), 1),
]


def pick_firing(rng):
    """One weighted brick firing colour."""
    total = sum(w for _, w in BRICK_FIRINGS)
    r = rng.random() * total
    for colour, w in BRICK_FIRINGS:
        r -= w
        if r < 0:
            return colour
    return BRICK_FIRINGS[-1][0]


def herringbone(size, width, length):
    """Axis-aligned herringbone over a periodic block, in render pixels.

    Bricks are `width` x `length` (joint included). Horizontal bricks sit on the
    lattice (width, width), (length, -length); each vertical brick hangs below
    its horizontal partner's left end. The pattern repeats over the block when
    size / 2 is a multiple of both width and length. Axis-aligned rather than
    at 45 degrees: joints stay on whole native pixels, and a path reads the
    same running across or down the screen.

    Returns the brick list and, per pixel, the brick index, the distance from
    the brick face's edge, the joint mask and the position along the brick
    (0..1) for lengthwise firing gradients.
    """
    assert (size // 2) % width == 0 and (size // 2) % length == 0
    origins = set()
    span = size // width + size // length + 4
    for i in range(-span, span):
        for j in range(-span, span):
            origins.add(((i * width + j * length) % size, (i * width - j * length) % size))
    bricks = []
    # Shift by half a brick width so joints never run along a cell edge.
    shift = width // 2
    for ox, oy in sorted(origins):
        ox, oy = (ox + shift) % size, (oy + shift) % size
        bricks.append((ox, oy, length, width, 0))
        bricks.append((ox, (oy + width) % size, width, length, 1))
    ids = [-1] * (size * size)
    edge = [0.0] * (size * size)
    joint = [0.0] * (size * size)
    along = [0.0] * (size * size)
    gap = NATIVE_PX  # one native pixel of joint on each brick's right and bottom side
    radius = 0.75 * NATIVE_PX
    for k, (x0, y0, bw, bh, vertical) in enumerate(bricks):
        fw, fh = bw - gap, bh - gap
        for v in range(bh):
            row = ((y0 + v) % size) * size
            for u in range(bw):
                i = row + (x0 + u) % size
                ids[i] = k
                if u >= fw or v >= fh:
                    joint[i], edge[i] = 1.0, 0.0
                else:
                    dx = min(u + 0.5, fw - u - 0.5)
                    dy = min(v + 0.5, fh - v - 0.5)
                    e = min(dx, dy)
                    if dx < radius and dy < radius:
                        e = radius - math.hypot(radius - dx, radius - dy)
                    edge[i] = e
                    joint[i] = 1.0 if e < 0 else 0.0
                along[i] = (v / bh) if vertical else (u / bw)
    assert min(ids) >= 0
    return bricks, ids, edge, joint, along


@recipe(name="brick_path", group="paths", label="Brick path", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"joint": (38, 28, 28)},
        style=Style(luma=112, std=20, grain_max=16), periodic=True, grid=8, blocks=4)
def render_brick_path(ctx):
    """Brick path: long clay pavers laid in an axis-aligned herringbone, so the
    path reads the same whichever way it runs."""
    p, size = RECIPES["brick_path"].palette, ctx.size
    # Long 4:1 pavers, 4 x 16 native px with their joint: every joint lands on
    # whole native pixels, so the 32 px tiles stay crisp.
    bricks, ids, edge, joint, along = herringbone(size, 4 * NATIVE_PX, 16 * NATIVE_PX)
    # Joint geometry is a fixed lattice common to every layout; brick firings
    # come from the shared stream at the block edge and each layout's own
    # stream inside, like road's stone tones.
    def firings(rng):
        out = []
        for _ in bricks:
            c = pick_firing(rng)
            lift = 1 + (rng.random() - 0.5) * 0.16
            slope = (rng.random() - 0.5) * 0.18
            out.append((tuple(v * lift for v in c), slope))
        return out
    common, own = firings(ctx.shared_rng), firings(ctx.rng)
    interior = ctx.interior_mask
    mottle = layout_field(ctx, lambda rng: fbm(rng, 24, 3, size=size))
    rgb = ([0.0] * (size * size), [0.0] * (size * size), [0.0] * (size * size))
    # Each brick one firing, a little brighter or darker and graded end to end,
    # with a fine mottle across all of them.
    for i in range(size * size):
        k, w = ids[i], interior[i]
        (ca, sa), (cb, sb) = common[k], own[k]
        slope = sa + (sb - sa) * w
        f = (1 + slope * (along[i] - 0.5) * 2) * (1 + 0.14 * (mottle[i] - 0.5) * 2)
        for ch in range(3):
            rgb[ch][i] = (ca[ch] + (cb[ch] - ca[ch]) * w) * f
    # A slight bevel, dark recessed joints and the Paths group's worn lip.
    height = [smoothstep(0.0, 1.5 * NATIVE_PX, e) for e in edge]
    rgb = shade(rgb, height, 0.25)
    rgb = mix(rgb, p["joint"], joint, 0.95)
    rgb = worn_edge(rgb, joint, light=(NATIVE_PX, NATIVE_PX), strength=0.18)
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.07 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    return rgb, None
