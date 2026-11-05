# SPDX-License-Identifier: GPL-3.0-or-later
"""The glacier ice terrain material."""
import math
import numpy as np

from terrain_core import (
    N,
    RECIPES,
    Style,
    TAU,
    blend_fields,
    domain_warp,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
)


def wrapped_window(size, cx, cy, reach):
    """Pixel rows/columns within `reach` of a point, wrapped, with the
    unwrapped offsets of their centres from the point."""
    r = int(math.ceil(reach)) + 1
    ux = np.arange(int(math.floor(cx)) - r, int(math.floor(cx)) + r + 1)
    uy = np.arange(int(math.floor(cy)) - r, int(math.floor(cy)) + r + 1)
    ox, oy = np.meshgrid(ux + 0.5 - cx, uy + 0.5 - cy)
    return np.ix_(uy % size, ux % size), ox, oy


def stamp_fracture(core, glow, x0, y0, a, span, w, reach, side, bright, t0=0.0, total=None):
    """One straight fracture segment: a bright line and a one-sided glow sheet.

    `t0` and `total` place the segment within a longer chain, so only the
    chain's own ends fade out and its joints stay at full strength.
    """
    size = core.shape[0]
    total = span if total is None else total
    dx, dy = math.cos(a), math.sin(a)
    index, ox, oy = wrapped_window(size, x0 + dx * span / 2, y0 + dy * span / 2, span / 2 + 5 * reach)
    t = ox * dx + oy * dy + span / 2
    n = oy * dx - ox * dy
    chain = t0 + t
    fade = np.clip(np.minimum(chain, total - chain) / (0.15 * total), 0, 1)
    # Overrun the segment ends by the line width so joints between segments close.
    along = (t > -w) & (t < span + w)
    line = np.where(along, np.clip(1.5 - np.abs(n) / w, 0, 1) * fade, 0)
    sheet = np.where(along, np.exp(-np.maximum(n * side, 0) / reach - np.maximum(-n * side, 0) / (0.25 * reach)), 0) * fade
    core[index] = np.maximum(core[index], line * bright)
    glow[index] = np.maximum(glow[index], sheet * bright)


def fracture_planes(rng, size, count, length, width, halo):
    """Fracture planes that may kink once and rarely throw a short branch.

    Returns (core, glow): a bright line and the feathered sheet of light the
    fracture throws to one side, as cracks deep in clear ice look from above.
    Primaries are at most a tile or so long; their branches are shorter,
    thinner and dimmer, so the cracks enclose calm plates about a tile
    across. Segments wrap across the canvas.
    """
    core = np.zeros((size, size))
    glow = np.zeros((size, size))
    for _ in range(round(count * (size // N) ** 2)):
        x, y = rng.random() * size, rng.random() * size
        a = rng.random() * TAU
        total = rng.uniform(*length)
        w = width * rng.uniform(0.8, 1.2)
        reach = halo * rng.uniform(0.7, 1.3)
        side = 1.0 if rng.random() < 0.5 else -1.0
        bright = rng.choice((0.45, 0.6, 0.8, 1.0, 1.0))
        pieces = rng.randint(1, 2)
        done = 0.0
        for k in range(pieces):
            span = total / pieces * rng.uniform(0.8, 1.2) if k < pieces - 1 else total - done
            stamp_fracture(core, glow, x, y, a, span, w, reach, side, bright, done, total)
            # Branches leave the primary at a sharp angle and die out quickly.
            if rng.random() < 0.15:
                f = rng.uniform(0.2, 0.8) * span
                b = a + (1 if rng.random() < 0.5 else -1) * rng.uniform(0.45, 1.0)
                stamp_fracture(core, glow, x + math.cos(a) * f, y + math.sin(a) * f, b,
                               rng.uniform(0.12, 0.3) * total, w * 0.7, reach * 0.6, side, bright * 0.7)
            x, y = x + math.cos(a) * span, y + math.sin(a) * span
            done += span
            a += rng.gauss(0, 0.22)
    return core.ravel().tolist(), glow.ravel().tolist()


def flat_bubbles(rng, size, clusters):
    """Small flattened air pockets: a pale body under a bright soft rim."""
    mask = np.zeros((size, size))
    for _ in range(round(clusters * (size // N) ** 2)):
        cx, cy = rng.random() * size, rng.random() * size
        a = rng.random() * TAU
        for _ in range(rng.randint(1, 2)):
            step = rng.gauss(0, 6)
            x, y = cx + step * math.cos(a), cy + step * math.sin(a)
            rx, ry = rng.uniform(5.0, 9.0), rng.uniform(3.0, 5.0)
            b = a + rng.gauss(0, 0.4)
            index, ox, oy = wrapped_window(size, x, y, rx)
            u, v = ox * math.cos(b) + oy * math.sin(b), oy * math.cos(b) - ox * math.sin(b)
            d = np.sqrt((u / rx) ** 2 + (v / ry) ** 2)
            body = np.clip((1 - d) / 0.25, 0, 1) * (0.7 + 0.3 * np.clip((d - 0.55) / 0.3, 0, 1))
            mask[index] = np.maximum(mask[index], body)
    return mask.ravel().tolist()


@recipe(name="glacier_ice", group="ice", label="Glacier ice", profile="frozen", border_class="solid",
        seam={'height': 4, 'cast_q8': 64, 'cast_width_q8': 640, 'fringe': [214, 232, 246], 'fringe_q8': 96,
              'fringe_width_q8': 384},
        palette={"abyss": (30, 92, 152), "deep": (52, 124, 184), "mid": (84, 160, 212), "pale": (138, 196, 236),
                 "glow": (178, 220, 246), "white": (238, 248, 253), "rime": (204, 230, 248)},
        style=Style(luma=145, std=22, grain_max=16, match=0.7), periodic=True, grid=8, blocks=4)
def render_glacier_ice(ctx):
    """Blue glacier ice: bright cyan-blue depth in calm plates, crossed by a
    web of short kinked white cracks, each well under a tile long, that throw
    a feathered glow to one side, with small flattened bubbles and fine rime
    speckle. Feature sizes are what a glob would see underfoot: no crack or
    bubble spans more than a tile."""
    p, size = RECIPES["glacier_ice"].palette, ctx.size
    # Depth: broad cloudy and clear zones, with soft darker clear pockets.
    depth = layout_field(ctx, lambda rng: domain_warp(fbm(rng, 3, 4, size=size), fbm(rng, 3, 2, size=size),
                                                      fbm(rng, 3, 2, size=size), 24))
    rgb = ramp(depth, [(0.0, p["abyss"]), (0.3, p["deep"]), (0.7, p["mid"]), (1.0, p["pale"])])
    pockets = layout_field(ctx, lambda rng: fbm(rng, 6, 2, size=size))
    rgb = mix(rgb, p["abyss"], [smoothstep(0.6, 0.8, v) for v in pockets], 0.3)
    # Fractures: under one per tile, mostly single straight runs between half
    # a tile and a tile and a half long, so the ice reads as crevassed rather
    # than scratched.
    core, glow = layout_field_pair(ctx, lambda rng: fracture_planes(rng, size, 0.7, (64, 190), 2.0, 9.0))
    wobble_x = layout_field(ctx, lambda rng: fbm(rng, 12, 2, size=size))
    wobble_y = layout_field(ctx, lambda rng: fbm(rng, 12, 2, size=size))
    core, glow = (domain_warp(f, wobble_x, wobble_y, 2) for f in (core, glow))
    rgb = mix(rgb, p["glow"], glow, 0.7)
    rgb = mix(rgb, p["white"], core, 0.75)
    # Small flattened bubbles and rime speckle frozen into the surface.
    rgb = mix(rgb, p["white"], layout_field(ctx, lambda rng: flat_bubbles(rng, size, 1.5)), 0.85)
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 96, size=size))
    rgb = mix(rgb, p["rime"], [smoothstep(0.72, 0.92, v) for v in speck], 0.3)
    grain_field = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))
    rgb = [[c * (1 + 0.05 * (v - 0.5) * 2) for c, v in zip(channel, grain_field)] for channel in rgb]
    return rgb, None


def layout_field_pair(ctx, make):
    """`layout_field` for a maker returning two fields from one stream."""
    common, own, mask = make(ctx.shared_rng), make(ctx.rng), ctx.interior_mask
    return tuple(blend_fields(c, o, mask) for c, o in zip(common, own))
