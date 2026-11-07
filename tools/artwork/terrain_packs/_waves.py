# SPDX-License-Identifier: GPL-3.0-or-later
"""Periodic open-water waves shared by water, deep water and dark water.

Water is opaque animated tiles in the colour of the retired scrolling ocean
backdrop (content/data/gfx/water0.png averaged about (69, 52, 200), luma 74).
The waters are cells of one 4x4-cell periodic block (`variant_grid`),
so a wave field four cells across continues over every cell edge. The field is
a sum of plane waves with whole wave numbers on that block, each advancing a
whole number of cycles per loop: every cell animates locally and all cells
share the clock, so crests roll across open water in one direction. A swell
toward the lower right sets the bands; finer cross chop breaks their crests
into moving glints. Deep water reads the same field (same seed and wind), so
crests carry on across the shallow/deep blend; so does dark water. Statistics
only, no pixels read."""
import math
import random

from terrain_core import N, TAU, blend_fields, fnv1a32, mix, ramp, smoothstep


OCEAN = (69, 52, 200)


WAVE_GRID = 4


WAVE_PHASES = 16


WAVE_SIZE = WAVE_GRID * N


# Whole-period sine table: wave numbers and the per-phase shift are integers,
# so every sample is a table lookup.
WAVE_SIN = [math.sin(TAU * i / WAVE_SIZE) for i in range(WAVE_SIZE)]


# (kx, ky, cycles per loop, amplitude): cycles per block; a wave moves toward +k.
WAVE_SWELL = [(2, 1, 1, 1.0), (3, 1, 1, 0.45), (3, 2, 1, 0.35), (4, 2, 2, 0.30)]


# At most four cycles per loop: a term moves no more than a quarter of its
# wavelength per phase, so fine chop reads as motion rather than flicker.
WAVE_CHOP = [(5, 1, 2, 0.5), (4, 4, 2, 0.45), (6, -1, 2, 0.4), (3, 6, 2, 0.35), (7, 3, 3, 0.35),
             (8, -3, 3, 0.3), (5, 7, 3, 0.3), (9, 2, 3, 0.25), (11, 5, 4, 0.2), (6, 10, 4, 0.2),
             (12, -2, 4, 0.18), (13, 7, 4, 0.14), (15, 4, 4, 0.14), (10, 13, 4, 0.12),
             (17, -5, 4, 0.1), (16, 11, 4, 0.1)]


# A static periodic warp (integer render px) bends the crests.
WAVE_WARP = [(1, 2, 12), (2, -1, 9), (1, -1, 6)]


_WAVE_CACHE = {}


def wave_fields(phase, phases=WAVE_PHASES, layout=0):
    """Periodic ocean geometry shared by both depths; layout0 defines edges.

    Integer sine-table samples preserve the loop exactly. Vectorization keeps
    all phases/layouts practical at1024px without changing wavelength/speed.
    """
    import numpy as np

    key = (phase, phases)
    if layout == 0 and key in _WAVE_CACHE:
        return _WAVE_CACHE[key]
    seed = "water:waves" if layout == 0 else f"water:waves:{layout}"
    rng = random.Random(fnv1a32(seed))
    size, quarter = WAVE_SIZE, WAVE_SIZE // 4
    scale = WAVE_GRID // 4
    swell = [(kx * scale, ky * scale, c, a, rng.randrange(size)) for kx, ky, c, a in WAVE_SWELL]
    chop = [(kx * scale, ky * scale, c, a, rng.randrange(size)) for kx, ky, c, a in WAVE_CHOP]
    # Odd warp harmonics make the larger block genuinely span eight cells;
    # doubling every frequency would silently repeat its four-cell quadrants.
    warp = [(kx * scale + int(scale > 1 and i == 0),
             ky * scale + int(scale > 1 and i == 1), a, rng.randrange(size))
            for i, (kx, ky, a) in enumerate(WAVE_WARP)]
    yy, xx = np.indices((size, size), dtype=np.int64)
    table = np.asarray(WAVE_SIN)
    offsets = []
    for axis in (0, 1):
        value = np.zeros((size, size))
        for i, (kx, ky, amplitude, ph) in enumerate(warp):
            value += amplitude * table[(kx * xx + ky * yy + ph + quarter * axis * (i + 1)) % size]
        offsets.append(np.rint(value).astype(np.int64))
    xw, yw = xx + offsets[0], yy + offsets[1]
    out = []
    for components in (swell, chop):
        total = sum(c[3] for c in components)
        values = np.zeros((size, size))
        for kx, ky, cycles, amplitude, ph in components:
            values += (amplitude / total) * table[(kx * xw + ky * yw + ph - cycles * phase * (size // phases)) % size]
        out.append(values.ravel().tolist())
    swell_values = np.asarray(out[0]).reshape(size, size)
    front = (np.roll(swell_values, 2, axis=1) + np.roll(swell_values, 1, axis=0)
             - np.roll(swell_values, -2, axis=1) - np.roll(swell_values, -1, axis=0)) * 6
    out.append(front.ravel().tolist())
    result = tuple(out)
    if layout == 0:
        _WAVE_CACHE.clear()
        _WAVE_CACHE[key] = result
    return result


def wave_cell(ctx, values):
    """The N x N cell of a block field that variant `ctx.variant` shows."""
    if ctx.size == WAVE_SIZE:
        return values
    gx, gy = ctx.variant % WAVE_GRID, ctx.variant // WAVE_GRID
    out = []
    for y in range(gy * N, gy * N + N):
        row = y * WAVE_SIZE + gx * N
        out.extend(values[row:row + N])
    return out


def wave_layers(ctx):
    """The swell, chop and swell-front fields of the cell or block `ctx` shows."""
    common = wave_fields(ctx.phase, ctx.phases)
    if ctx.size == WAVE_SIZE and ctx.variant:
        own = wave_fields(ctx.phase, ctx.phases, ctx.variant)
        fields = tuple(blend_fields(a, b, ctx.interior_mask) for a, b in zip(common, own))
    else:
        fields = common
    return tuple(wave_cell(ctx, values) for values in fields)


def render_waves(ctx, palette, swell_weight, chop_weight, glint, front):
    """Swell bands lit on their leading face, with glints where chop crests
    ride swell crests."""
    s, c, f = wave_layers(ctx)
    t = [0.5 + swell_weight * a + chop_weight * b for a, b in zip(s, c)]
    rgb = ramp([smoothstep(0.05, 0.5, v) for v in t], [(0.0, palette["deep"]), (1.0, palette["base"])])
    rgb = mix(rgb, palette["light"], [0.8 * smoothstep(0.45, 0.85, v) + front * smoothstep(0, 1, d)
                                      for v, d in zip(t, f)], 1.0)
    glints = [smoothstep(0.0, 0.6, a) * smoothstep(0.22, 0.55, b) for a, b in zip(s, c)]
    return mix(rgb, palette["glint"], glints, glint)
