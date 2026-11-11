# SPDX-License-Identifier: GPL-3.0-or-later
"""The boardwalk terrain material."""
import math

import numpy as np

from ._paving import decay_fields, dress, per_stone, rectangle_geometry, stone_fates, uneven_rows
from terrain_core import (
    N,
    NATIVE_PX,
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
    stamp_stroke,
)


def plank_rectangles(size, rng):
    """Planks in runs of uneven width, each 2 to 3.6 cells long, wrapping
    the block, with their knot positions."""
    planks, knots = [], []
    # Rows start a few native px into the block so no plank gap lies on a
    # cell cut, where it would read as a tile seam.
    y = 3 * NATIVE_PX
    for width in uneven_rows(size, rng, heights=(6, 7, 9, 10), offset=y):
        start = rng.random() * size
        x, row = start, []
        while True:
            length = rng.uniform(2.0, 3.6) * N
            if start + size - (x + length) < 1.6 * N:
                row.append((x, start + size - x))
                break
            row.append((x, length))
            x += length
        for x0, length in row:
            planks.append((x0, y, length, width, 1.5))
            if rng.random() < 0.6:
                knots.append(((x0 + rng.uniform(0.5, 1.6) * N) % size, y + rng.uniform(0.3, 0.7) * width,
                              rng.uniform(4, 7)))
        y += width
    return planks, knots


@recipe(name="boardwalk", group="paths", label="Boardwalk", profile="crisp", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (80, 68, 54), "mid": (126, 108, 84), "light": (160, 142, 116), "gap": (40, 34, 28),
                 "warm": (134, 98, 62), "bleached": (182, 172, 150), "stain": (140, 96, 54), "rubble": (118, 104, 88),
                 "damp": (54, 46, 38),
                 "knot": (70, 62, 54), "nail": (52, 50, 50), "rust": (112, 70, 44), "rot": (62, 54, 44),
                 "silver": (148, 144, 136), "joint": (42, 36, 30), "grit": (104, 92, 76),
                 "dust": (124, 118, 108), "crack": (58, 50, 42)},
        style=Style(luma=108, std=20, grain_max=11, match=0.7), periodic=True, grid=8, blocks=4,
        note="planks run one way: accepted compromise")
def render_boardwalk(ctx):
    """Long weathered planks in runs of uneven width, silvered grey-brown, with
    grain, knots and rusting nails: cupped and tilted as they have settled,
    some split along the grain, rotting at damp ends, a few missing over the
    soil, with grit and moss in the gaps.

    Plank ends, knots and widths come from the shared stream, so every layout
    has the same joints; only the wood grain and wear inside vary."""
    p, size = RECIPES["boardwalk"].palette, ctx.size
    shared, own = ctx.shared_rng, ctx.rng
    planks, knot_list = plank_rectangles(size, shared)
    centres, ids, edge, axial, across = rectangle_geometry(size, planks)
    decay = decay_fields(ctx, reclaim=(0.8, 0.96))
    reach = 32 + 3.6 * N
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.05, cracked=0.16, sink=0.3, heave=0.3)
    # A lost plank has rotted away from one end, not vanished whole.
    pixel = np.arange(size * size)
    column = pixel % size
    broken = np.zeros(size * size)
    for k, ((x0, y0, w, h, _), s) in enumerate(zip(planks, fates)):
        if not s.lost:
            continue
        span = w * (0.2 + 0.3 * s.tone)
        here = ids == k
        dx = (column[here] + 0.5 - x0) % size
        broken[here] = np.where((dx < span) if s.kink > 0 else (dx > w - span), 1.0, 0.0)
    # Each plank samples its own stretch of grain, so neighbouring planks
    # never share a grain line.
    shifts = [int(shared.random() * size) for _ in planks]
    grain_common = fbm(shared, 2, 3, cells_y=40, size=size, array=True)
    grain_own = fbm(own, 2, 3, cells_y=40, size=size, array=True)
    streak_c = lattice_noise(shared, 4, 64, size=size, array=True)
    streak_o = lattice_noise(own, 4, 64, size=size, array=True)
    interior = ctx.interior
    knots, nails, rust = (np.zeros(size * size) for _ in range(3))
    # Weathering is per board: each plank has gone its own way, darker where
    # it lies damp, silvered where it dried in the sun, and always along the grain.
    def plank_value(field, common_field):
        out = []
        for cx, cy in centres:
            at = int(cy) % size * size + int(cx) % size
            out.append(float(common_field[at] if min(cx, cy, size - cx, size - cy) < reach else field[at]))
        return np.array(out)
    plank_damp = plank_value(decay.damp, decay.common_damp)
    plank_sun = plank_value(decay.wear, decay.common_wear)
    for kx, ky, kr in knot_list:
        stamp_disc(knots, kx % size, ky % size, kr, softness=0.6)
    # Nails in pairs near each plank end, with a rust streak running down.
    for (x0, y0, w, h, _), s in zip(planks, fates):
        for px in (x0 + 6.0, x0 + w - NATIVE_PX - 6.0):
            for fy in (0.28, 0.72):
                stamp_disc(nails, px % size, y0 + h * fy, 1.6, softness=0.3)
                if s.sink > 0.12:
                    stamp_stroke(rust, px % size, y0 + h * fy + 1.5, math.pi / 2, 3.0 + 10.0 * s.sink, 1.4, 0.6)
    sample = (pixel // size) * size + (column + np.array(shifts)[ids]) % size
    g = grain_common[sample] + (grain_own[sample] - grain_common[sample]) * interior
    st = streak_c[sample] + (streak_o[sample] - streak_c[sample]) * interior
    bright = per_stone(fates, lambda s: 1 + (s.tone - 0.5) * 0.5)[ids]
    # Cupped planks: one edge lifted toward the light, the other sunk.
    cup = 1 + 0.1 * per_stone(fates, lambda s: s.cast)[ids] * (across - 0.5) * 2
    tone = clamp01((0.45 * g + 0.55 * st) * bright * cup)
    streak = smoothstep(0.35, 0.75, st)
    dark = smoothstep(0.5, 0.85, plank_damp)[ids] * (0.55 + 0.45 * g)
    # Ends rot where the ground is damp.
    rot = (1 - smoothstep(0.0, 14.0, axial)) * smoothstep(0.45, 0.85, decay.growth)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 6, 128, size=size, array=True))
    rgb = rgb * (1 + 0.14 * (fine - 0.5) * 2)
    # Silvering along the grain where the sun has had the board longest;
    # planks that kept their colour stay a warmer brown, damp ones go dark.
    hue = per_stone(fates, lambda s: s.hue)[ids]
    rgb = mix(rgb, p["warm"], np.maximum(0.0, hue) * 0.55, 1.0)
    rgb = mix(rgb, p["silver"], smoothstep(0.4, 0.8, plank_sun)[ids] * (0.25 + 0.5 * np.maximum(0.0, -hue))
              * (0.4 + 0.6 * streak), 1.0)
    rgb = rgb * (1 - 0.28 * dark)
    rgb = mix(rgb, p["damp"], dark, 0.2)
    # Planks bleached pale by the sun, and a few gone orange with rust and sap.
    stain = per_stone(fates, lambda s: s.stain)
    rgb = mix(rgb, p["bleached"], smoothstep(0.86, 0.95, stain)[ids], 0.6)
    rgb = mix(rgb, p["stain"], np.where(stain < 0.06, 1.0, 0.0)[ids], 0.35)
    rgb = mix(rgb, p["knot"], knots, 0.55)
    # Some planks have rotted dark right through.
    rotten = per_stone(fates, lambda s: 1.0 if s.cast < -0.75 else 0.0)[ids]
    rgb = mix(rgb, p["rot"], np.maximum(rot, 0.7 * rotten), 0.6)
    rgb = mix(rgb, p["rust"], rust, 0.5)
    rgb = mix(rgb, p["nail"], nails, 0.6)
    # Splits run along the grain from a plank end.
    along = [0.0 if s.kink > 0 else math.pi for s in fates]
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 32.0,
                joint=(0.0, 2.0), creep=1.0, tilt=0.006, chipping=0.8, bevel=1.5, bevel_strength=0.2,
                crack_along=along, moss=0.2, weeds=0.3, lichen=0.1, grit=0.4, dust=0.1, lip=0.2,
                shadow=0.35, finish_grain=0.05, broken=broken, damp=0.0)
    return rgb, None
