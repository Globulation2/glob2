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


def herringbone(size, pairs):
    """Axis-aligned herringbone over a periodic block, in render pixels.

    Bricks are 2:1, `width` = size / (2 * pairs) by twice that (joint
    included), so the pattern repeats over the block: horizontal bricks sit
    on the lattice (width, width), (length, -length), which closes when
    size / 2 is a whole number of widths and of lengths. Each vertical brick
    hangs below its horizontal partner's left end. Axis-aligned rather than
    at 45 degrees, so a path reads the same running across or down the
    screen. Brick edges need not fall on whole pixels; each pixel takes the
    brick its centre lies in.

    Returns the brick list and, per pixel, the brick index, the distance from
    the brick face's edge (negative in the joint) and the position along the
    brick (0..1) for lengthwise firing gradients.
    """
    width = size / (2 * pairs)
    length = 2 * width
    per = 2 * pairs  # lattice units of one width across the block
    origins = set()
    for i in range(per):
        for j in range(per):
            origins.add(((i + 2 * j) % per, (i - 2 * j) % per))
    bricks = []
    # Shift by half a brick width so joints never run along a cell edge.
    shift = width / 2
    for ox, oy in sorted(origins):
        x, y = (ox * width + shift) % size, (oy * width + shift) % size
        bricks.append((x, y, length, width, 0))
        bricks.append((x, (y + width) % size, width, length, 1))
    ids = [-1] * (size * size)
    edge = [0.0] * (size * size)
    along = [0.0] * (size * size)
    gap = NATIVE_PX  # one native pixel of joint on each brick's right and bottom side
    radius = 0.75 * NATIVE_PX
    for k, (x0, y0, bw, bh, vertical) in enumerate(bricks):
        fw, fh = bw - gap, bh - gap
        for py in range(math.floor(y0), math.ceil(y0 + bh) + 1):
            v = py + 0.5 - y0
            if not 0 <= v < bh:
                continue
            row = (py % size) * size
            for px in range(math.floor(x0), math.ceil(x0 + bw) + 1):
                u = px + 0.5 - x0
                if not 0 <= u < bw:
                    continue
                i = row + px % size
                ids[i] = k
                if u >= fw or v >= fh:
                    edge[i] = -0.5 * NATIVE_PX
                else:
                    dx = min(u, fw - u)
                    dy = min(v, fh - v)
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
        style=Style(luma=100, std=20, grain_max=12), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_brick_path(ctx):
    """Brick path: long clay pavers laid in an axis-aligned herringbone, so the
    path reads the same whichever way it runs, worn by years of use: bricks
    settled and chipped, some cracked across or lost to the soil, sand and
    grit in the joints, dust over the firings and moss where it is damp."""
    p, size = RECIPES["brick_path"].palette, ctx.size
    # Pavers of 2:1 proportion, 6.4 x 12.8 native px with their joint: two
    # fifths of a tile long, so a brick is still a brick at 32 px per cell.
    bricks, ids, edge, along = herringbone(size, 20)
    centres = [((x0 + bw / 2) % size, (y0 + bh / 2) % size) for x0, y0, bw, bh, _ in bricks]
    decay = decay_fields(ctx, reclaim=(0.88, 0.97))
    reach = 32 + 13 * NATIVE_PX
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
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 3.2 * NATIVE_PX,
                joint=(0.0, 1.5), creep=0.0, chipping=0.8, bevel=1.5, bevel_strength=0.3,
                crack_along=across, moss=0.25, weeds=0.3, lichen=0.2, grit=0.95, grit_cover=0.22, dust=0.18, lip=0.2)
    return rgb, None
