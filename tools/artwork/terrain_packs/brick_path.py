# SPDX-License-Identifier: GPL-3.0-or-later
"""The brick path terrain material."""
import math

from ._paving import decay_fields, dress, stone_fates, stone_field
from terrain_core import (
    NATIVE_PX,
    RECIPES,
    Style,
    clamp01,
    fbm,
    layout_field,
    recipe,
)


# Brick firings, after the reference but weathered: red, plum, grey-brown,
# dark, rust, all dulled by years of boots and rain.
BRICK_FIRINGS = [
    ((150, 80, 60), 3), ((120, 72, 60), 3), ((134, 94, 72), 3),
    ((98, 60, 50), 2), ((168, 98, 68), 2), ((170, 140, 96), 1), ((74, 50, 48), 1),
]


def firing_of(u):
    """The weighted brick firing colour for a tone in [0, 1)."""
    total = sum(w for _, w in BRICK_FIRINGS)
    r = u * total
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
    the brick face's edge (negative in the joint) and the position along the
    brick (0..1) for lengthwise firing gradients.
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
                    edge[i] = -0.5 * NATIVE_PX
                else:
                    dx = min(u + 0.5, fw - u - 0.5)
                    dy = min(v + 0.5, fh - v - 0.5)
                    e = min(dx, dy)
                    if dx < radius and dy < radius:
                        e = radius - math.hypot(radius - dx, radius - dy)
                    edge[i] = e
                along[i] = (v / bh) if vertical else (u / bw)
    assert min(ids) >= 0
    return bricks, ids, edge, along


@recipe(name="brick_path", group="paths", label="Brick path", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"joint": (72, 58, 46), "grit": (162, 142, 112), "dust": (140, 116, 92), "soil": (74, 60, 46),
                 "crack": (54, 38, 32)},
        style=Style(luma=100, std=20, grain_max=12), periodic=True, grid=8, blocks=4)
def render_brick_path(ctx):
    """Brick path: long clay pavers laid in an axis-aligned herringbone, so the
    path reads the same whichever way it runs, worn by years of use: bricks
    settled and chipped, some cracked across or lost to the soil, sand and
    grit in the joints, dust over the firings and moss where it is damp."""
    p, size = RECIPES["brick_path"].palette, ctx.size
    # Pavers of 2:1 proportion, 8 x 16 native px with their joint: half a
    # tile long, the same range as the trail's setts and the sandstone flags,
    # so a brick is still a brick at 32 px per cell, and every joint lands on
    # whole native pixels, so the tiles stay crisp.
    bricks, ids, edge, along = herringbone(size, 8 * NATIVE_PX, 16 * NATIVE_PX)
    centres = [((x0 + bw / 2) % size, (y0 + bh / 2) % size) for x0, y0, bw, bh, _ in bricks]
    decay = decay_fields(ctx, reclaim=(0.88, 0.97))
    reach = 32 + 16 * NATIVE_PX
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.035, cracked=0.12, sink=0.5, heave=0.3)
    # Bricks crack across their length, not along it.
    across = [(math.pi / 2 if not vertical else 0.0) + (1 if s.kink > 0 else -1) * 0.0
              for (_, _, _, _, vertical), s in zip(bricks, fates)]
    mottle = layout_field(ctx, lambda rng: fbm(rng, 24, 3, size=size))
    # Bricks were laid batch by batch, so firings drift in broad runs.
    batch = stone_field(ctx, centres, reach, lambda rng: fbm(rng, 3, 2, size=size))
    firing = [firing_of(clamp01(0.7 * s.tone + 0.3 * b)) for s, b in zip(fates, batch)]
    rgb = ([0.0] * (size * size), [0.0] * (size * size), [0.0] * (size * size))
    # Each brick one firing, graded end to end by its settling, with a fine
    # mottle across all of them.
    # A few bricks have spalled: their fired skin is gone and the dark, rough
    # core shows.
    spalled = [s.tone > 0.9 for s in fates]
    for i in range(size * size):
        k = ids[i]
        s = fates[k]
        c = firing[k]
        f = (1 + 0.09 * s.cast * (along[i] - 0.5) * 2) * (1 + 0.12 * (mottle[i] - 0.5) * 2)
        if spalled[k]:
            f *= 0.7 + 0.25 * (mottle[i] - 0.5) * 2
        for ch in range(3):
            rgb[ch][i] = c[ch] * f
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 4 * NATIVE_PX,
                joint=(0.0, 1.5), creep=0.0, chipping=0.8, bevel=1.5, bevel_strength=0.3,
                crack_along=across, moss=0.25, weeds=0.3, lichen=0.2, grit=0.95, grit_cover=0.22, dust=0.18, lip=0.2)
    return rgb, None
