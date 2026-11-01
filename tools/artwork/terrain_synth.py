#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise original procedural terrain materials for the terrain catalog.

Usage (with the pinned development dependencies and Pillow 12.2.0):

    uv venv artifacts/asset-tools/venv
    uv pip install --python artifacts/asset-tools/venv/bin/python -r tools/asset-requirements.txt
    ENC=artifacts/asset-tools/venv/bin/python
    "$ENC" tools/artwork/terrain_synth.py                    # write all frames
    "$ENC" tools/artwork/terrain_synth.py --material mud --contact-sheet artifacts/terrain/mud.png
    "$ENC" tools/artwork/terrain_synth.py --check            # re-synthesise and compare
    "$ENC" tools/artwork/terrain_synth.py --emit-catalog     # print catalog blocks
    "$ENC" tools/artwork/terrain_synth.py --write-catalog    # merge them into tileset.json
    "$ENC" tools/artwork/terrain_synth.py --hd               # 128x128 renders under artifacts/

Every material is synthesised from scratch: periodic value noise, fractal sums,
domain warps, Worley cells, scattered stamps and palette ramps, all driven by
`random.Random(seed).random()` with `seed = fnv1a32("<material>:<variant>:<phase>")`.
No pixel of an existing tile is ever read into an output; the native grass, sand,
trail, ice and water tiles are only measured for the style targets (luma mean and
spread, neighbour grain, saturation) that keep the new art low-contrast and
painterly beside them.

Ground materials render periodic grid blocks, sliced into 128x128 positional
tiles (`variant_grid`) and box-downsampled to 32x32. Static materials use
8x8 blocks with four seeded interiors and common boundaries; lava and ember
field use one 8x8 block, and water and deep water one 4x4 block, to keep their
animated frame banks small. Feature
counts and noise frequencies scale with canvas size to keep their native density.
Gravel, flower meadow and marsh instead share periodic outer object bands.
The interchangeable blocks preserve shared edge geometry while varying their
interiors. Frames are phase-major, block-major, then
row-major cell: `cell + cells * block + (cells * blocks) * phase`, where
`cells = grid * grid`. Animated materials
render four phases (sixteen for water and deep water).
Outputs are `data/gfx/terrain-<name>N.png`, the 128x128 HD frames the classic
tiles were downsampled from (`data/highres/v1/terrain-<name>N.png`, registered in
the pack through tools/artwork/highres_pack.py) and `content-src/gfx/<name>/provenance.json`.

Pixel-exact reproduction (`--check`) is pinned to the encoder interpreter on
Linux x86-64: besides Pillow's resampling kernels, the renders depend on the C
library's sin/atan2/hypot and on round() boundaries, so another platform may
differ in a few low bits. The provenance records the platform.

Python 3, Pillow, NumPy and SciPy (development-only dependencies).
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import platform
import random
import struct
import sys
from dataclasses import dataclass, field, asdict
from functools import lru_cache
from pathlib import Path

import PIL
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from material_tiles import (  # noqa: E402
    ROOT,
    SHEET,
    STYLE_REFERENCES,
    TILE,
    color_distance,
    fnv1a32,
    luma,
    mean_color,
    pixel_sha256,
    reference_stats,
    reference_tiles,
    runtime_blend,
    share_perimeter,
    style_stats,
    tile_hashes,
    tiles_to_sheet,
    write_frames,
)
from terrain_tileset import BUILTIN_NAMES, LEGACY_BINDINGS  # noqa: E402

PILLOW_VERSION = "12.2.0"
N = SHEET  # render size; four render pixels per native pixel
VARIANTS = 16
FRAME_PREFIX = "data/gfx/terrain-"
HD_PREFIX = "data/highres/v1/terrain-"
HD_CATEGORY = "procedural-materials"
HD_RECIPE = "procedural terrain synthesis v1"
XS = [i % N for i in range(N * N)]
YS = [i // N for i in range(N * N)]
TAU = 2 * math.pi
# Render pixels from an edge over which the perimeter treatment pulls the very
# low frequencies toward the tile mean (BAND, about 2.5 native px); stamped
# features keep at least STAMP_MARGIN from the edge so variant 0's perimeter,
# which the runtime repeats on every tile, carries only ordinary texture. See
# `neutral_band`.
BAND = 10
STAMP_MARGIN = 12

LEGACY_NAMES = list(LEGACY_BINDINGS)
# Catalogue order of the built-in types, from the engine's generated name list.
BUILTIN_ORDER = [name for name in json.loads(BUILTIN_NAMES.read_text()) if name not in LEGACY_BINDINGS]
# Legacy bindings whose material is synthesised here too: regular water is an
# ordinary animated tile set rather than a scrolling backdrop.
SYNTHESISED_LEGACY = list(LEGACY_BINDINGS)
SYNTH_ORDER = BUILTIN_ORDER + SYNTHESISED_LEGACY


# --------------------------------------------------------------------------
# Field library: flat lists of N*N floats, periodic on the 128x128 canvas.
# --------------------------------------------------------------------------


def clamp01(v):
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


def smoothstep(e0, e1, v):
    if e1 == e0:
        return 0.0 if v < e0 else 1.0
    t = clamp01((v - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def lattice_noise(rng, cells, cells_y=None, *, size=N):
    """Periodic value noise: a random lattice, tiled 3x3 and bicubically resized.

    Pillow does the interpolation in one C call; the centre crop of the 3x3
    tiling is periodic by construction. Values are clamped to [0, 1].
    """
    cells_y = cells_y or cells
    cells, cells_y = cells * size / N, cells_y * size / N
    cells = max(1, min(size, int(cells)))
    cells_y = max(1, min(size, int(cells_y or cells)))
    values = [rng.random() for _ in range(cells * cells_y)]
    rows = []
    for _ in range(3):
        for y in range(cells_y):
            row = values[y * cells : (y + 1) * cells]
            rows.extend(row * 3)
    image = Image.frombytes("F", (cells * 3, cells_y * 3), struct.pack(f"{len(rows)}f", *rows))
    big = image.resize((size * 3, size * 3), Image.Resampling.BICUBIC)
    crop = big.crop((size, size, 2 * size, 2 * size))
    data = getattr(crop, "get_flattened_data", crop.getdata)()
    return [clamp01(v) for v in data]


def fbm(rng, cells, octaves=3, gain=0.5, lacunarity=2.0, cells_y=None, *, size=N):
    """Fractal sum of lattice noise, centred on 0.5 and roughly in [0.1, 0.9]."""
    total = [0.0] * (size * size)
    amplitude, norm = 1.0, 0.0
    cy = cells_y or cells
    for _ in range(octaves):
        layer = lattice_noise(rng, cells, cy, size=size)
        total = [t + amplitude * (v - 0.5) for t, v in zip(total, layer)]
        norm += amplitude
        amplitude *= gain
        cells = min(N, cells * lacunarity)
        cy = min(N, cy * lacunarity)
    return [clamp01(0.5 + t / norm) for t in total]


def normalize(values, low=0.01, high=0.99):
    """Stretch a field so its `low`/`high` quantiles map to 0 and 1."""
    ordered = sorted(values)
    a = ordered[int(low * (len(ordered) - 1))]
    b = ordered[int(high * (len(ordered) - 1))]
    if b - a < 1e-9:
        return [0.5] * len(values)
    scale = 1.0 / (b - a)
    return [clamp01((v - a) * scale) for v in values]


def normalize_like(values, reference, low=0.01, high=0.99):
    """Normalize all interiors using one shared field's quantile range."""
    ordered = sorted(reference)
    a, b = ordered[int(low * (len(ordered) - 1))], ordered[int(high * (len(ordered) - 1))]
    if b - a < 1e-9:
        return [0.5] * len(values)
    return [clamp01((v - a) / (b - a)) for v in values]


def blend_fields(common, own, mask):
    """Vary scalar geometry fields, preserving exact common boundary samples."""
    return [a + (b - a) * w for a, b, w in zip(common, own, mask)]


def sample(values, fx, fy, size=None):
    """Bilinear, wrapping sample of a field at a fractional position."""
    size = math.isqrt(len(values)) if size is None else size
    x0 = math.floor(fx)
    y0 = math.floor(fy)
    tx = fx - x0
    ty = fy - y0
    x0 %= size
    y0 %= size
    x1 = (x0 + 1) % size
    y1 = (y0 + 1) % size
    top = values[y0 * size + x0] * (1 - tx) + values[y0 * size + x1] * tx
    bottom = values[y1 * size + x0] * (1 - tx) + values[y1 * size + x1] * tx
    return top * (1 - ty) + bottom * ty


def domain_warp(values, warp_x, warp_y, amount, amount_y=None):
    """Re-sample `values` displaced by two noise fields (±`amount` pixels)."""
    size = math.isqrt(len(values))
    amount_y = amount if amount_y is None else amount_y
    return [
        sample(values, x + (wx - 0.5) * 2 * amount, y + (wy - 0.5) * 2 * amount_y, size)
        for x, y, wx, wy in ((i % size, i // size, wx, wy) for i, (wx, wy) in enumerate(zip(warp_x, warp_y)))
    ]


def box_blur(values, radius):
    """Separable wrapping box blur, O(N^2) through running sums."""
    size = 2 * radius + 1
    rows = [0.0] * (N * N)
    for y in range(N):
        base = y * N
        total = sum(values[base + (x % N)] for x in range(-radius, radius + 1))
        for x in range(N):
            rows[base + x] = total / size
            total += values[base + (x + radius + 1) % N] - values[base + (x - radius) % N]
    out = [0.0] * (N * N)
    for x in range(N):
        total = sum(rows[(y % N) * N + x] for y in range(-radius, radius + 1))
        for y in range(N):
            out[y * N + x] = total / size
            total += rows[((y + radius + 1) % N) * N + x] - rows[((y - radius) % N) * N + x]
    return out


def _rotated_distance(dx, dy, rot, stretch):
    c, sn = rot
    u = dx * c + dy * sn
    v = (-dx * sn + dy * c) * stretch
    return max(abs(u), abs(v))


def _worley_from_points(points, metric, rotations=None, stretch=1.0, *, size=N):
    """Nearest and second-nearest wrapped distance (in pixels) and nearest id."""
    # NumPy keeps the full-block free-point search practical without changing
    # point order, distance arithmetic or the first/second-nearest tie rules.
    import numpy as np

    if size > 4 * N and len(points) > 1:
        return _worley_indexed(points, metric, rotations, stretch, size=size)
    yy, xx = np.indices((size, size), dtype=np.float64)
    best = np.full((size, size), 1e9)
    second = np.full((size, size), 1e9)
    ids = np.zeros((size, size), dtype=np.int64)
    half = size / 2
    for pid, (px, py) in enumerate(points):
        dx, dy = px - xx, py - yy
        dx = np.where(dx > half, dx - size, np.where(dx < -half, dx + size, dx))
        dy = np.where(dy > half, dy - size, np.where(dy < -half, dy + size, dy))
        if metric == "angular":
            c, sn = rotations[pid]
            d = np.maximum(np.abs(dx * c + dy * sn), np.abs((-dx * sn + dy * c) * stretch))
        elif metric == "chebyshev":
            d = np.maximum(np.abs(dx), np.abs(dy))
        else:
            d = np.sqrt(dx * dx + dy * dy)
        closer = d < best
        second = np.where(closer, best, np.minimum(second, d))
        ids = np.where(closer, pid, ids)
        best = np.minimum(best, d)
    return best.ravel().tolist(), second.ravel().tolist(), ids.ravel().tolist()



def _worley_indexed(points, metric, rotations=None, stretch=1.0, *, size=N):
    """Exact periodic nearest-two search, accelerated by Euclidean candidates.

    The metric's Euclidean lower bound certifies when no omitted point can
    replace the second nearest. Uncertified samples expand their candidate
    list, and sorting IDs preserves the exhaustive search's tie order.
    """
    import numpy as np
    from scipy.spatial import cKDTree

    if len(points) < 2:
        return _worley_from_points(points, metric, rotations, stretch, size=size)
    positions = np.asarray(points, dtype=np.float64)
    turns = np.asarray(rotations, dtype=np.float64) if rotations is not None else None
    tree = cKDTree(positions, boxsize=size)
    factor = 1.0 if metric == "euclid" else (1 / math.sqrt(1 + 1 / stretch ** 2)
                                               if metric == "angular" else 1 / math.sqrt(2))
    best_out, second_out, ids_out = [], [], []
    for start in range(0, size * size, 16384):
        linear = np.arange(start, min(start + 16384, size * size))
        samples = np.column_stack((linear % size, linear // size))
        best, second = np.empty(len(samples)), np.empty(len(samples))
        nearest = np.empty(len(samples), dtype=np.int64)
        pending, count = np.arange(len(samples)), min(16, len(points))
        while len(pending):
            distances, candidates = tree.query(samples[pending], k=count)
            candidates = np.sort(candidates, axis=1)
            delta = positions[candidates] - samples[pending, None, :]
            delta = np.where(delta > size / 2, delta - size,
                             np.where(delta < -size / 2, delta + size, delta))
            dx, dy = delta[:, :, 0], delta[:, :, 1]
            if metric == "angular":
                c, sn = turns[candidates, 0], turns[candidates, 1]
                values = np.maximum(np.abs(dx * c + dy * sn), np.abs((-dx * sn + dy * c) * stretch))
            elif metric == "chebyshev":
                values = np.maximum(np.abs(dx), np.abs(dy))
            else:
                values = np.sqrt(dx * dx + dy * dy)
            first_index = np.argmin(values, axis=1)
            second_values = np.partition(values, 1, axis=1)[:, 1]
            certified = (distances[:, -1] * factor > second_values * (1 + 1e-12)
                         if count < len(points) else np.ones(len(pending), dtype=bool))
            done = pending[certified]
            row = np.flatnonzero(certified)
            best[done] = values[row, first_index[certified]]
            second[done] = second_values[certified]
            nearest[done] = candidates[row, first_index[certified]]
            pending = pending[~certified]
            count = min(len(points), count * 2)
        best_out.extend(best.tolist()); second_out.extend(second.tolist()); ids_out.extend(nearest.tolist())
    return best_out, second_out, ids_out

@lru_cache(maxsize=1)
def _block_worley_master(size, count, metric, stretch, state):
    rng = random.Random()
    rng.setstate(state)
    points = [(rng.random() * size, rng.random() * size) for _ in range(count)]
    angles = [rng.random() * TAU for _ in range(count)] if metric == "angular" else []
    turns = [(math.cos(a), math.sin(a)) for a in angles] if angles else None
    first, second, ids = _worley_from_points(points, metric, turns, stretch, size=size)
    band = 64  # Includes the shared strip, domain-warps and lighting samples.
    reach = max(second[i] for i in range(size * size)
                if min(i % size, i // size, size - 1 - i % size, size - 1 - i // size) < band)
    factor = (1.0 if metric == "euclid" else
              1 / math.sqrt(1 + 1 / stretch ** 2) if metric == "angular" else 1 / math.sqrt(2))
    guard = math.ceil(band + reach / factor) + 1
    cell = size / math.sqrt(count)
    return (points, turns, [v / cell for v in first], [v / cell for v in second], ids,
            guard, rng.getstate())


def block_worley(ctx, count, metric="euclid", stretch=1.0):
    """Shared boundary cells, independently placed interior cells, exact F1/F2.

    Returns own F1/F2/IDs followed by their common references. The certified
    geometry guard keeps every layout identical throughout the outer64 pixels.
    """
    size, count = ctx.size, count * ctx.area_scale
    points, turns, common1, common2, common_ids, guard, state = _block_worley_master(
        size, count, metric, stretch, ctx.shared_rng.getstate())
    ctx.shared_rng.setstate(state)
    if ctx.variant == 0:
        ctx.rng.setstate(state)
        return common1, common2, common_ids, common1, common2, common_ids
    positions, rotations = list(points), list(turns) if turns is not None else None
    for i, (x, y) in enumerate(points):
        if min(x, y, size - x, size - y) <= guard:
            continue
        positions[i] = (ctx.rng.uniform(guard, size - guard), ctx.rng.uniform(guard, size - guard))
        if rotations is not None:
            a = ctx.rng.random() * TAU
            rotations[i] = (math.cos(a), math.sin(a))
    first, second, ids = _worley_from_points(positions, metric, rotations, stretch, size=size)
    cell = size / math.sqrt(count)
    return [v / cell for v in first], [v / cell for v in second], ids, common1, common2, common_ids


def worley_points(rng, count, metric="euclid", stretch=1.0, *, size=N):
    """Worley cells from `count` free points; distances in units of the mean cell.

    `angular` rotates a Chebyshev metric by a random angle per point (and
    optionally stretches it), giving angular chips without a shared orientation.
    """
    count *= (size // N) ** 2
    points = [(rng.random() * size, rng.random() * size) for _ in range(count)]
    rotations = None
    if metric == "angular":
        rotations = []
        for _ in range(count):
            angle = rng.random() * TAU
            rotations.append((math.cos(angle), math.sin(angle)))
    f1, f2, ids = _worley_from_points(points, metric, rotations, stretch, size=size)
    cell = size / math.sqrt(count)
    return [v / cell for v in f1], [v / cell for v in f2], ids, points


def worley(rng, cells, metric="euclid", jitter=1.0, *, size=N):
    """Jittered-grid Worley cells; F1/F2 in units of the cell size.

    Each pixel examines the nine surrounding cells with wrapped indices, so the
    field is periodic and the cost stays linear in the canvas size. Metrics:
    `euclid` (round cells), `chebyshev` (axis-aligned squares) and `angular`
    (a Chebyshev metric rotated by a random angle per cell).
    """
    canvas = size
    cells = max(1, int(cells * canvas / N))
    size = canvas / cells
    px = [[0.0] * cells for _ in range(cells)]
    py = [[0.0] * cells for _ in range(cells)]
    rot = [[(1.0, 0.0)] * cells for _ in range(cells)]
    for cy in range(cells):
        for cx in range(cells):
            px[cy][cx] = (cx + 0.5 + (rng.random() - 0.5) * jitter) * size
            py[cy][cx] = (cy + 0.5 + (rng.random() - 0.5) * jitter) * size
            if metric == "angular":
                angle = rng.random() * TAU
                rot[cy][cx] = (math.cos(angle), math.sin(angle))
    f1 = [0.0] * (canvas * canvas)
    f2 = [0.0] * (canvas * canvas)
    ids = [0] * (canvas * canvas)
    chebyshev = metric == "chebyshev"
    angular = metric == "angular"
    for i in range(canvas * canvas):
        x, y = i % canvas, i // canvas
        cx0 = int(x / size)
        cy0 = int(y / size)
        best, second, best_id = 1e9, 1e9, 0
        for oy in (-1, 0, 1):
            cy = (cy0 + oy) % cells
            wrap_y = ((cy0 + oy) // cells) * canvas
            for ox in (-1, 0, 1):
                cx = (cx0 + ox) % cells
                wrap_x = ((cx0 + ox) // cells) * canvas
                dx = px[cy][cx] + wrap_x - x
                dy = py[cy][cx] + wrap_y - y
                if angular:
                    d = _rotated_distance(dx, dy, rot[cy][cx], 1.0)
                elif chebyshev:
                    d = max(abs(dx), abs(dy))
                else:
                    d = math.sqrt(dx * dx + dy * dy)
                if d < best:
                    second, best, best_id = best, d, cy * cells + cx
                elif d < second:
                    second = d
        f1[i], f2[i], ids[i] = best / size, second / size, best_id
    return f1, f2, ids


def stamp_disc(mask, cx, cy, radius, value=1.0, softness=0.5, mode="max"):
    """Stamp a soft disc into `mask` with wrap-around; falloff over `softness`."""
    size = math.isqrt(len(mask))
    r = int(math.ceil(radius)) + 1
    inner = radius * (1 - softness)
    for oy in range(-r, r + 1):
        uy = int(math.floor(cy)) + oy
        y = uy % size
        for ox in range(-r, r + 1):
            ux = int(math.floor(cx)) + ox
            x = ux % size
            # Distances use the unwrapped pixel so stamps wrap across edges.
            d = math.hypot(ux + 0.5 - cx, uy + 0.5 - cy)
            if d >= radius:
                continue
            weight = 1.0 if d <= inner else 1 - smoothstep(inner, radius, d)
            i = y * size + x
            if mode == "max":
                if weight * value > mask[i]:
                    mask[i] = weight * value
            else:
                mask[i] = clamp01(mask[i] + weight * value)


def disc_profile(cx, cy, radius, lumps=(), *, size=N):
    """Dome height field of one disc (1 at the centre, 0 at the rim), wrapped.

    `lumps` is a list of (harmonic, amplitude, phase) radius modulations that
    turn the circle into an irregular stone outline.
    """
    height = [0.0] * (size * size)
    r = int(math.ceil(radius * 1.4)) + 1
    for oy in range(-r, r + 1):
        uy = int(math.floor(cy)) + oy
        y = uy % size
        for ox in range(-r, r + 1):
            ux = int(math.floor(cx)) + ox
            x = ux % size
            dx, dy = ux + 0.5 - cx, uy + 0.5 - cy
            angle = math.atan2(dy, dx)
            local = radius * (1 + sum(a * math.sin(h * angle + ph) for h, a, ph in lumps))
            d = math.hypot(dx, dy) / local
            if d < 1:
                height[y * size + x] = max(height[y * size + x], math.sqrt(1 - d * d))
    return height


def scatter(ctx, count, radius_min, radius_max, softness=0.5):
    """Random soft discs, with native density on either a tile or a full block.

    Returns the union mask and (x, y, r, u) tuples. Full-block stamps can
    cross all cell edges and wrap the block; independent tiles keep a margin.
    """
    margin = STAMP_MARGIN + radius_max
    mask = [0.0] * (ctx.size * ctx.size)
    discs = []
    for _ in range(count * ctx.area_scale):
        cx, cy = ctx.interior_point(margin)
        radius = radius_min + ctx.rng.random() * (radius_max - radius_min)
        discs.append((cx, cy, radius, ctx.rng.random()))
    for cx, cy, radius, _ in discs:
        stamp_disc(mask, cx, cy, radius, 1.0, softness)
    return mask, discs


def stamp_stroke(mask, x0, y0, angle, length, width, value=1.0):
    """A straight stroke as a chain of discs, wrapped."""
    steps = max(2, int(length / max(0.5, width * 0.4)))
    dx, dy = math.cos(angle), math.sin(angle)
    for s in range(steps + 1):
        t = s / steps
        stamp_disc(mask, x0 + dx * length * t, y0 + dy * length * t, width / 2 + 0.5, value, 0.6)


def wrapped_distance(ax, ay, bx, by):
    dx = abs(ax - bx) % N
    dy = abs(ay - by) % N
    return math.hypot(min(dx, N - dx), min(dy, N - dy))


def edge_distance(x, y):
    return min(x, y, N - x, N - y)


def object_field(ctx, count, radius, spacing=0.85, attempts=40):
    """Round objects for a periodic material: [(x, y, r, rng_value)].

    Objects touching the shared outer band come from the material's base rng
    and are identical in every variant (they wrap across the tile edge); each
    variant adds its own objects entirely inside the interior. Variants thus
    differ everywhere except the band, with whole objects and no ghosting.
    `count` is the approximate number of objects per tile.
    """
    placed = []

    def fits(x, y, r):
        return all(wrapped_distance(x, y, px, py) > (r + pr) * spacing for px, py, pr, _ in placed)

    base, rng = ctx.base_rng, ctx.rng
    # Shared band objects: centres near an edge (wrapping), drawn first.
    # The shared quota follows the band's share of the tile area, so the
    # band is no denser than the interior and draws no grid.
    reach = PERIODIC_BAND + sum(radius) / 2
    quota = count * (1 - ((N - 2 * reach) / N) ** 2)
    for _ in range(count * attempts):
        if len(placed) >= quota:
            break
        r = base.uniform(*radius)
        x, y = base.random() * N, base.random() * N
        if edge_distance(x, y) < PERIODIC_BAND + r and fits(x, y, r):
            placed.append((x, y, r, base.random()))
    shared = len(placed)
    for _ in range(count * attempts):
        if len(placed) >= count:
            break
        r = rng.uniform(*radius)
        x, y = rng.random() * N, rng.random() * N
        if edge_distance(x, y) > PERIODIC_FADE + r + 1 and fits(x, y, r):
            placed.append((x, y, r, rng.random()))
    return placed[:shared], placed[shared:]


def block_object_field(ctx, count, radius, spacing=0.85, attempts=40):
    """Whole shared perimeter objects and independent interior objects.

    A periodic spatial hash keeps placement local at1024px. Every layout has
    the same full-density boundary bed, without fading or cutting objects.
    """
    size, target = ctx.size, count * ctx.area_scale
    bins = max(1, int(size / max(1, 2 * radius[1] * spacing)))
    step = size / bins
    buckets = {}

    def add(obj):
        x, y, _, _ = obj
        buckets.setdefault((int(x / step) % bins, int(y / step) % bins), []).append(obj)

    def fits(x, y, r):
        gx, gy = int(x / step), int(y / step)
        for by in range(gy - 1, gy + 2):
            for bx in range(gx - 1, gx + 2):
                for px, py, pr, _ in buckets.get((bx % bins, by % bins), ()):
                    dx, dy = abs(x - px), abs(y - py)
                    if math.hypot(min(dx, size - dx), min(dy, size - dy)) <= (r + pr) * spacing:
                        return False
        return True

    def place(rng, quota, interior=False):
        out = []
        for _ in range(max(1, quota) * attempts):
            if len(out) >= quota:
                break
            r = rng.uniform(*radius)
            x, y = rng.random() * size, rng.random() * size
            if interior and min(x, y, size - x, size - y) <= 100 + r:
                continue
            if fits(x, y, r):
                obj = (x, y, r, rng.random())
                add(obj); out.append(obj)
        return out

    master = place(ctx.base_rng, target)
    shared = [o for o in master if min(o[0], o[1], size - o[0], size - o[1]) <= 100 + o[2]]
    if ctx.variant == 0:
        own = [o for o in master if min(o[0], o[1], size - o[0], size - o[1]) > 100 + o[2]]
    else:
        buckets.clear()
        for obj in shared: add(obj)
        own = place(ctx.rng, target - len(shared), interior=True)
    return shared, own


def render_block_domes(objects, tone_of, lumps_of=None, texture=None, *, size):
    """Rasterize only each object's wrapped bounding box, not a full canvas."""
    height = [0.0] * (size * size)
    rgb = [[0.0] * (size * size) for _ in range(3)]
    for cx, cy, radius, u in sorted(objects, key=lambda o: o[1]):
        lumps = lumps_of(u) if lumps_of else ()
        tone = tone_of(u)
        reach = int(math.ceil(radius * 1.4)) + 1
        for uy in range(math.floor(cy) - reach, math.floor(cy) + reach + 1):
            for ux in range(math.floor(cx) - reach, math.floor(cx) + reach + 1):
                dx, dy = ux + 0.5 - cx, uy + 0.5 - cy
                angle = math.atan2(dy, dx)
                local = radius * (1 + sum(a * math.sin(h * angle + ph) for h, a, ph in lumps))
                d = math.hypot(dx, dy) / local
                if d >= 1: continue
                h = math.sqrt(1 - d * d)
                index = (uy % size) * size + ux % size
                height[index] = h
                value = texture(u, angle, h) if texture else 1.0
                for k in range(3): rgb[k][index] = tone[k] * value
    return height, rgb


def render_domes(objects, tone_of, lumps_of=None, texture=None, *, size=N):
    """Painter-ordered domes: (height, rgb) fields from objects.

    `texture(u, angle, h)` optionally scales the tone per pixel, from the
    pixel's angle around the object centre and its dome height."""
    if size != N:
        return render_block_domes(objects, tone_of, lumps_of, texture, size=size)
    height = [0.0] * (N * N)
    rgb = [[0.0] * (N * N) for _ in range(3)]
    for x, y, r, u in sorted(objects, key=lambda o: o[1]):
        lumps = lumps_of(u) if lumps_of else ()
        dome = disc_profile(x, y, r, lumps)
        tone = tone_of(u)
        for i, h in enumerate(dome):
            if h > 0.0:
                height[i] = h
                scale = 1.0
                if texture:
                    dx = (XS[i] + 0.5 - x + N / 2) % N - N / 2
                    dy = (YS[i] + 0.5 - y + N / 2) % N - N / 2
                    scale = texture(u, math.atan2(dy, dx), h)
                for c in range(3):
                    rgb[c][i] = tone[c] * scale
    return height, rgb


def shifted(values, dx, dy):
    """The field moved by (dx, dy) whole render pixels, wrapped."""
    size = math.isqrt(len(values))
    return [values[((y - dy) % size) * size + (x - dx) % size] for x, y in ((i % size, i // size) for i in range(len(values)))]


def interior_weight(margin, fade=8):
    """0 within `margin` render px of the edge, rising to 1 over `fade`."""
    w = band_weight(margin, margin + fade)
    return [1 - v for v in w]


def stamp_flower(masks, cx, cy, radius, petals, angle):
    """Petals (discs around the centre) into masks["petal"], centre into
    masks["centre"], a soft drop shadow into masks["shadow"]. Wrapped."""
    # Overlapping petals make a round rosette: at 32 px it reads as a round
    # bloom with a centre, never as a stroke.
    pr = radius * 0.55
    for k in range(petals):
        a = angle + TAU * k / petals
        px, py = cx + math.cos(a) * radius * 0.45, cy + math.sin(a) * radius * 0.45
        stamp_disc(masks["petal"], px, py, pr, 1.0, 0.3)
    stamp_disc(masks["shadow"], cx + 1.5, cy + 2.0, radius * 1.05, 0.8, 0.7)
    stamp_disc(masks["centre"], cx, cy, max(2.0, radius * 0.3), 1.0, 0.3)


def band(values, edge, width):
    """1 inside `values < edge`, falling to 0 over `width` beyond it."""
    return [1 - smoothstep(edge, edge + width, v) for v in values]


def ramp(values, stops):
    """Map a [0,1] field through colour stops via a 256-entry LUT."""
    lut = []
    for i in range(256):
        t = i / 255
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1 or (t1, c1) == stops[-1]:
                f = 0.0 if t1 == t0 else clamp01((t - t0) / (t1 - t0))
                lut.append(tuple(a + (b - a) * f for a, b in zip(c0, c1)))
                break
    indexed = [lut[int(clamp01(v) * 255 + 0.5)] for v in values]
    return [c[0] for c in indexed], [c[1] for c in indexed], [c[2] for c in indexed]


def flat(color, size=N):
    return [float(color[0])] * (size * size), [float(color[1])] * (size * size), [float(color[2])] * (size * size)


def mix(rgb, color_or_rgb, mask, strength=1.0):
    """Blend toward a colour (or another RGB triple of fields) by mask*strength."""
    if isinstance(color_or_rgb, tuple) and len(color_or_rgb) == 3 and not isinstance(color_or_rgb[0], list):
        target = flat(color_or_rgb, math.isqrt(len(rgb[0])))
    else:
        target = color_or_rgb
    out = []
    for channel, tchannel in zip(rgb, target):
        out.append([c + (t - c) * clamp01(m * strength) for c, t, m in zip(channel, tchannel, mask)])
    return out


def desaturate(rgb, amount):
    """Pull colours toward their luma by `amount` (0..1)."""
    out = [[], [], []]
    for r, g, b in zip(*rgb):
        grey = luma(r, g, b)
        out[0].append(r + (grey - r) * amount)
        out[1].append(g + (grey - g) * amount)
        out[2].append(b + (grey - b) * amount)
    return out


def gradient_light(height, light=(-0.7, -0.7), reference_height=None):
    """Normalised directional gradient of a height field in [-1, 1]."""
    size = math.isqrt(len(height))
    lx, ly = light
    g = [0.0] * (size * size)
    for i in range(size * size):
        x, y = i % size, i // size
        gx = height[y * size + (x + 1) % size] - height[y * size + (x - 1) % size]
        gy = height[((y + 1) % size) * size + x] - height[((y - 1) % size) * size + x]
        g[i] = gx * lx + gy * ly
    if reference_height is None:
        rms = math.sqrt(sum(v * v for v in g) / len(g)) or 1.0
    else:
        # A common lighting scale keeps interchangeable block boundaries identical.
        reference_g = []
        for i in range(size * size):
            x, y = i % size, i // size
            gx = reference_height[y * size + (x + 1) % size] - reference_height[y * size + (x - 1) % size]
            gy = reference_height[((y + 1) % size) * size + x] - reference_height[((y - 1) % size) * size + x]
            reference_g.append(gx * lx + gy * ly)
        rms = math.sqrt(sum(v * v for v in reference_g) / len(reference_g)) or 1.0
    return [max(-1.0, min(1.0, v / (2.5 * rms))) for v in g]


def shade(rgb, height, strength, light=(-0.7, -0.7), reference_height=None):
    """Bevel: brighten slopes facing `light`, darken the others."""
    g = gradient_light(height, light, reference_height)
    return [[c * (1 - strength * v) for c, v in zip(channel, g)] for channel in rgb]


def grain(rgb, rng, amount=0.12, cells=48):
    """Fine per-pixel grain, as the native tiles have (about one native pixel)."""
    fine = lattice_noise(rng, cells, size=math.isqrt(len(rgb[0])))
    return [[c * (1 + amount * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]


def cell_jitter(rng, count, spread, hue=0.25):
    """Per-cell brightness jitter with a small, hue-neutral colour drift."""
    jitters = []
    for _ in range(count):
        value = 1 + (rng.random() - 0.5) * 2 * spread
        warm = (rng.random() - 0.5) * 2 * spread * hue
        jitters.append((value + warm, value, value - warm))
    return jitters


def apply_cell_jitter(rgb, ids, jitters):
    return [
        [c * jitters[i][k] for c, i in zip(channel, ids)] for k, channel in enumerate(rgb)
    ]


def alpha_from(values, low, high):
    return [low + (high - low) * clamp01(v) for v in values]


# --------------------------------------------------------------------------
# Perimeter, downsampling and style
# --------------------------------------------------------------------------

BAND_WEIGHTS = {}


def band_weight(inner, outer):
    """1 at the tile edge, falling to 0 between `inner` and `outer` render px."""
    key = (inner, outer)
    if key not in BAND_WEIGHTS:
        BAND_WEIGHTS[key] = [
            1 - smoothstep(float(inner), float(outer), min(x + 0.5, y + 0.5, N - x - 0.5, N - y - 0.5))
            for x, y in zip(XS, YS)
        ]
    return BAND_WEIGHTS[key]


def neutral_band(rgb, alpha, radius=10, strength=0.8):
    """Keep the perimeter's broad tone near the tile mean, texture untouched.

    The runtime blends the outer four native pixels of every variant toward
    variant 0's reflected pixels with weights 1, 3/4, 1/2, 1/4
    (TerrainCompositor / `seamless_sources`), so whatever variant 0 carries
    there is repeated on every tile. Only the very low frequencies (a box
    blur of radius `radius` render pixels) are pulled toward the tile mean,
    over BAND render pixels with the strength tapering to zero inward, so no
    light or dark blotch sits on an edge while grain, chips and colour
    variation stay: the runtime then converges every edge onto ordinary
    texture rather than onto a flat frame. Alpha gets the same treatment.
    """
    weight = [t * strength for t in band_weight(0, BAND)]
    count = len(rgb[0])
    lum = [luma(r, g, b) for r, g, b in zip(*rgb)]
    mean = sum(lum) / count
    low = box_blur(lum, radius)
    shift = [(mean - lo) * t for lo, t in zip(low, weight)]
    rgb = [[c + s for c, s in zip(channel, shift)] for channel in rgb]
    if alpha is not None:
        mean_a = sum(alpha) / count
        low_a = box_blur(alpha, radius)
        alpha = [a + (mean_a - lo) * t for a, lo, t in zip(alpha, low_a, weight)]
    return rgb, alpha


def box_down_fields(rgb, alpha, factor=N // TILE):
    """Area-average render fields to native size on premultiplied colour."""
    size = N // factor
    out_rgb = [[0.0] * (size * size) for _ in range(3)]
    out_alpha = [0.0] * (size * size)
    inv = 1.0 / (factor * factor)
    for oy in range(size):
        for ox in range(size):
            acc = [0.0, 0.0, 0.0]
            acc_a = 0.0
            for dy in range(factor):
                base = (oy * factor + dy) * N + ox * factor
                for dx in range(factor):
                    i = base + dx
                    a = alpha[i] if alpha is not None else 255.0
                    acc_a += a
                    acc[0] += rgb[0][i] * a
                    acc[1] += rgb[1][i] * a
                    acc[2] += rgb[2][i] * a
            o = oy * size + ox
            out_alpha[o] = acc_a * inv
            if acc_a > 0:
                out_rgb[0][o] = acc[0] / acc_a
                out_rgb[1][o] = acc[1] / acc_a
                out_rgb[2][o] = acc[2] / acc_a
    return out_rgb, out_alpha


def style_transform(rgb, target_luma, target_std, strength=1.0):
    """Affine luma correction pulling mean and spread toward the recipe targets.

    Returns (channel means, contrast factor, shift); `apply_style` applies the
    same transform to every variant of a material so they share one tone.
    Statistics only: nothing is read from existing art.
    """
    count = len(rgb[0])
    lum = [luma(r, g, b) for r, g, b in zip(*rgb)]
    mean = sum(lum) / count
    std = math.sqrt(sum((v - mean) ** 2 for v in lum) / count)
    k = 1.0 if std < 1e-6 else max(0.5, min(2.0, target_std / std))
    k = 1 + (k - 1) * strength
    means = [sum(channel) / count for channel in rgb]
    shift = (target_luma - mean) * strength
    return means, k, shift


def apply_style(rgb, transform):
    means, k, shift = transform
    return [[m + (c - m) * k + shift for c in channel] for channel, m in zip(rgb, means)]


def fields_to_image(rgb, alpha, size):
    data = bytearray()
    for i in range(size * size):
        for channel in rgb:
            data.append(int(min(255, max(0, round(channel[i])))))
        a = 255.0 if alpha is None else alpha[i]
        data.append(int(min(255, max(0, round(a)))))
    return Image.frombytes("RGBA", (size, size), bytes(data))


# --------------------------------------------------------------------------
# Recipes
# --------------------------------------------------------------------------


@dataclass
class Style:
    luma: float
    std: float
    grain_max: float = 10.0
    match: float = 1.0


@dataclass
class Recipe:
    name: str
    group: str
    label: str
    profile: str
    seam: dict
    palette: dict
    style: Style
    render: object = field(repr=False, compare=False)
    phases: int = 1
    animation_ticks: int = 8
    alpha_range: tuple = (255, 255)
    preview: tuple = None
    minimap: tuple = None
    placeholder_only: bool = False
    # Periodic edges: every variant shares one periodic base whose outer band is
    # identical across variants, so any two variants continue each other across
    # a shared edge and the runtime skips its border blend ("edges": "periodic").
    # Per-variant features stay inside the interior.
    periodic: bool = False
    # Positional variants ("variant_grid") slice each periodic GRID x GRID
    # block in row-major order. Whole blocks select seeded interiors while
    # their common outer bands continue across every edge. Requires periodic.
    grid: int = 0
    blocks: int = 1
    block_group: str = ""
    note: str = ""

    @property
    def cells(self):
        return self.grid * self.grid if self.grid else VARIANTS

    def describe(self):
        d = asdict(self)
        d.pop("render")
        return d


RECIPES = {}


def recipe(**spec):
    def register(fn):
        RECIPES[spec["name"]] = Recipe(render=fn, **spec)
        return fn

    return register


BLOCK_INTERIOR_MASKS = {}


def block_interior_mask(size):
    if size not in BLOCK_INTERIOR_MASKS:
        BLOCK_INTERIOR_MASKS[size] = [
            smoothstep(32.0, 96.0, min(x + 0.5, y + 0.5, size - x - 0.5, size - y - 0.5))
            for y in range(size) for x in range(size)
        ]
    return BLOCK_INTERIOR_MASKS[size]


class Ctx:
    """Per-render canvas, geometry rng, per-phase rng and phase time."""

    def __init__(self, name, variant, phase, phases, size=N):
        self.size = size
        self.area_scale = (size // N) ** 2
        self.name, self.variant, self.phase, self.phases = name, variant, phase, phases
        self.rng = random.Random(fnv1a32(f"{name}:{variant}:0"))
        self.shared_rng = random.Random(fnv1a32(f"{name}:0:0"))
        self.phase_rng = random.Random(fnv1a32(f"{name}:{variant}:{phase}"))
        self.t = phase / phases
        # Shared by every variant of a periodic material (and every phase).
        self.base_rng = random.Random(fnv1a32(f"{name}:base"))

    @property
    def interior_mask(self):
        return block_interior_mask(self.size)

    def interior_point(self, margin):
        """A position at least `margin` render pixels from every edge."""
        if self.size > N:
            return self.rng.random() * self.size, self.rng.random() * self.size
        span = max(1.0, N - 2 * margin)
        return margin + self.rng.random() * span, margin + self.rng.random() * span


def ground(ctx, cells, octaves, anchors, warp=0.0, stretch=(0.02, 0.98), grit=0.12):
    """Warped fractal noise through a dark/mid/light ramp: the painterly base."""
    rng = ctx.rng
    f = fbm(rng, cells, octaves, size=ctx.size)
    if warp:
        f = domain_warp(f, fbm(rng, 3, 2, size=ctx.size), fbm(rng, 3, 2, size=ctx.size), warp)
    f = normalize(f, *stretch)
    rgb = ramp(f, [(0.0, anchors[0]), (0.5, anchors[1]), (1.0, anchors[2])])
    if grit:
        rgb = grain(rgb, rng, grit)
    return rgb, f


def soft_blobs(ctx, count, radius_min, radius_max, core=0.0):
    """Blooms that survive the 4x downsample: 2-3 native px with a soft halo.

    Returns (halo mask, core mask, discs); the core is a small darker centre.
    """
    halo, discs = scatter(ctx, count, radius_min, radius_max, softness=0.75)
    cores = [0.0] * (ctx.size * ctx.size)
    if core:
        for cx, cy, _, _ in discs:
            stamp_disc(cores, cx, cy, core, 1.0, 0.5)
    return halo, cores, discs


# Barren ground: muted warm neutrals, inhibits growth like sand but is not a shore.

@recipe(name="dirt", group="barren", label="Dirt", profile="soft",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (104, 82, 56), "mid": (136, 108, 74), "light": (160, 132, 96), "pebble": (86, 68, 48)},
        style=Style(luma=116, std=9, grain_max=8), periodic=True, grid=8, blocks=4)
def render_dirt(ctx):
    p = RECIPES["dirt"].palette
    common, common_grain, common_pebbles = dirt_fields(ctx.shared_rng, ctx.size)
    own, own_grain, own_pebbles = dirt_fields(ctx.rng, ctx.size)
    # A shared reference range keeps the common earth and tiny stones at
    # exactly the same tone, independent of each layout's interior statistics.
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common,
                            low=0.02, high=0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    pebbles = blend_fields(common_pebbles, own_pebbles, ctx.interior_mask)
    return mix(rgb, p["pebble"], pebbles, 0.8), None


def dirt_fields(rng, size):
    """Periodic earth, fine grain and pebble masks at the original density."""
    height = fbm(rng, 5, 4, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 10)
    fine = lattice_noise(rng, 48, size=size)
    pebbles = [0.0] * (size * size)
    for _ in range(40 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(pebbles, x, y, rng.uniform(1.6, 2.6), softness=0.6)
    return height, fine, pebbles


@recipe(name="clay", group="barren", label="Clay", profile="soft",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (134, 88, 60), "mid": (166, 114, 80), "light": (186, 138, 100), "crack": (116, 74, 52)},
        style=Style(luma=124, std=8, grain_max=8), periodic=True, grid=8, blocks=4)
def render_clay(ctx):
    """Cracked clay with shared boundary plates and independent interiors."""
    p = RECIPES["clay"].palette
    size, shared, own = ctx.size, ctx.shared_rng, ctx.rng
    f1, f2, _, common1, common2, _ = block_worley(ctx, 6)
    common_noise = fbm(shared, 4, 3, size=size)
    noise = blend_fields(common_noise, fbm(own, 4, 3, size=size), ctx.interior_mask)
    common_x, common_y = fbm(shared, 4, 2, size=size), fbm(shared, 4, 2, size=size)
    wx = blend_fields(common_x, fbm(own, 4, 2, size=size), ctx.interior_mask)
    wy = blend_fields(common_y, fbm(own, 4, 2, size=size), ctx.interior_mask)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 5)
    common_plates_raw = domain_warp([1 - v for v in common1], common_x, common_y, 5)
    plates_raw = domain_warp([1 - v for v in f1], wx, wy, 5)
    common_plates = normalize_like(common_plates_raw, common_plates_raw)
    plates = normalize_like(plates_raw, common_plates_raw)
    common_tone = [0.6 * a + 0.4 * b for a, b in zip(common_noise, common_plates)]
    tone = normalize_like([0.6 * a + 0.4 * b for a, b in zip(noise, plates)], common_tone)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grit = lattice_noise(shared, 48, size=size)
    grit = blend_fields(common_grit, lattice_noise(own, 48, size=size), ctx.interior_mask)
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    cracks = band(edge, 0.03, 0.05)
    rgb = mix(rgb, p["crack"], cracks, 0.45)
    return rgb, None


@recipe(name="gravel", group="barren", label="Gravel", profile="rock",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"fines": (98, 86, 70), "gap": (50, 43, 36),
                 "stones": [(168, 156, 136), (146, 138, 126), (178, 162, 136), (128, 122, 114),
                            (160, 140, 118), (132, 133, 134), (200, 188, 162), (156, 128, 104),
                            (176, 150, 120), (214, 202, 176)]},
        style=Style(luma=128, std=32, grain_max=26, match=0.8), periodic=True, grid=8, blocks=4)
def render_gravel(ctx):
    """Rounded pebbles, after close-up photographs of pebble beds: a dark bed
    of fines under packed, overlapping stones of mixed grey and warm tones,
    each lit from the upper left with a dark contact shadow to the lower
    right, and a few larger stones on top."""
    p = RECIPES["gravel"].palette
    size = ctx.size
    common_bed = fbm(ctx.shared_rng, 12, 2, size=size)
    own_bed = fbm(ctx.rng, 12, 2, size=size)
    bed = normalize_like(blend_fields(common_bed, own_bed, ctx.interior_mask), common_bed)
    rgb = ramp(bed, [(0.0, p["gap"]), (1.0, p["fines"])])
    speck = blend_fields(lattice_noise(ctx.shared_rng, 64, size=size),
                         lattice_noise(ctx.rng, 64, size=size), ctx.interior_mask)
    master_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=size)

    def tone(u):
        colour = p["stones"][int(u * 997) % len(p["stones"])]
        value = 0.86 + 0.26 * ((u * 7919) % 1.0)
        return [c * value for c in colour]

    def lumps(u):
        return [(h, (0.05 + 0.06 * ((u * 31 * h) % 1.0)) * 3 / (h + 1), u * TAU * h) for h in (2, 3, 5)]

    # Grit, stones and a few large stones: a size hierarchy with the dark bed
    # showing between them, which is what separates stones at 32 px.
    for count, radius, spacing in ((40, (4.0, 6.0), 1.05), (18, (8.0, 11.0), 1.0), (3, (11.0, 14.0), 0.95)):
        shared, own = block_object_field(ctx, count, radius, spacing=spacing)
        master_shared, master_own = block_object_field(master_ctx, count, radius, spacing=spacing)
        height, stones = render_domes(shared + own, tone, lumps, size=size)
        if ctx.variant == 0:
            master_height = height
        else:
            master_height, master_stones = render_domes(master_shared + master_own, tone, lumps, size=size)
            del master_stones
        stones = shade(stones, height, 0.5, reference_height=master_height)
        stones = [[c * (0.92 + 0.16 * v) for c, v in zip(channel, speck)] for channel in stones]
        cover = [smoothstep(0.0, 0.2, h) for h in height]
        shadow = shifted(cover, 2, 3)
        rgb = mix(rgb, p["gap"], [s * (1 - c) for s, c in zip(shadow, cover)], 0.7)
        rgb = mix(rgb, stones, cover, 1.0)
    return rgb, None


@recipe(name="flower_meadow", group="barren", label="Flower meadow", profile="brush",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (26, 98, 28), "mid": (34, 116, 34), "light": (54, 134, 46),
                 "shadow": (18, 60, 20),
                 "flowers": [(250, 250, 244), (252, 214, 64), (236, 120, 176), (178, 120, 230),
                             (240, 92, 72), (120, 160, 246)],
                 "centres": [(248, 196, 60), (120, 70, 30), (250, 236, 150)]},
        style=Style(luma=92, std=24, grain_max=18, match=0.3), periodic=True, grid=8, blocks=4)
def render_flower_meadow(ctx):
    """Whole flowers cross common block edges; independently seeded drifts fill the interior."""
    p, size = RECIPES["flower_meadow"].palette, ctx.size
    common = fbm(ctx.shared_rng, 8, 3, size=size)
    own = fbm(ctx.rng, 8, 3, size=size)
    turf = normalize_like(blend_fields(common, own, ctx.interior_mask), common)
    rgb = ramp(turf, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_blades = lattice_noise(ctx.shared_rng, 48, size=size)
    own_blades = lattice_noise(ctx.rng, 48, size=size)
    blades = blend_fields(common_blades, own_blades, ctx.interior_mask)
    rgb = [[c * (0.88 + 0.24 * v) for c, v in zip(channel, blades)] for channel in rgb]

    common_patch = fbm(ctx.shared_rng, 4, 2, size=size)
    own_patch = fbm(ctx.rng, 4, 2, size=size)
    patch_reference = common_patch
    common_patch = normalize_like(common_patch, patch_reference)
    own_patch = normalize_like(own_patch, patch_reference)
    # Start color/density variation beyond every shared flower's full extent.
    # Heads stay whole: their palette and petal geometry never cross-fade.
    shared, own_objects = block_object_field(ctx, 24, (6.5, 8.0), spacing=1.05)
    for cx, cy, radius, u in sorted(shared + own_objects, key=lambda o: o[1]):
        x, y = int(cx) % size, int(cy) % size
        index = y * size + x
        weight = smoothstep(112.0, 192.0, min(cx, cy, size - cx, size - cy))
        patch = common_patch[index] + (own_patch[index] - common_patch[index]) * weight
        if patch < 0.30:
            continue  # Open grass between drifts, rather than a uniform flower carpet.
        color_id = min(len(p["flowers"]) - 1, int(patch * len(p["flowers"])))
        if int(u * 65535) % 5 == 0:
            color_id = int(u * 7919) % len(p["flowers"])  # Scattered singles among same-color patches.
        colour = p["flowers"][color_id]
        centre = p["centres"][int(u * 997) % len(p["centres"])]
        flower_meadow_paint(rgb, size, cx, cy, radius, u, colour, centre, p["shadow"])
    return rgb, None


def flower_meadow_paint(rgb, size, cx, cy, radius, u, colour, centre, shadow):
    """Render a flower in its small wrapped bounding box, independent of canvas size."""
    side = 32
    left, top = math.floor(cx) - side // 2, math.floor(cy) - side // 2
    masks = {name: [0.0] * (side * side) for name in ("petal", "centre", "shadow")}
    stamp_flower(masks, cx - left, cy - top, radius, 4 + int(u * 65521) % 3, (u * 171 % 1) * TAU)
    # Per-head bevel normalization is local and therefore identical for a
    # shared boundary flower, irrespective of the rest of the block's layout.
    lighting = gradient_light(masks["petal"])
    for i, (petal, core, cast) in enumerate(zip(masks["petal"], masks["centre"], masks["shadow"])):
        if not (petal or core or cast):
            continue
        target = ((top + i // side) % size) * size + (left + i % side) % size
        for k in range(3):
            value = rgb[k][target]
            value += (shadow[k] - value) * cast * 0.55
            value += (colour[k] * (1 - 0.25 * lighting[i]) - value) * petal
            value += (centre[k] - value) * core
            rgb[k][target] = value


# Rough ground: slow going; cool and desaturated apart from mud.

@recipe(name="mud", group="rough", label="Mud", profile="soft",
        seam={"height": 1},
        palette={"dark": (62, 48, 36), "mid": (88, 70, 52), "light": (116, 96, 74), "sheen": (150, 132, 112)},
        style=Style(luma=76, std=8, grain_max=7), periodic=True, grid=8, blocks=4)
def render_mud(ctx):
    p = RECIPES["mud"].palette
    common, common_grit, common_pools = mud_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_pools = mud_fields(ctx.rng, ctx.size)
    # One reference range for all layouts keeps their common perimeter exact.
    # Blend the height and wetness fields before colouring rather than fading
    # between finished images; pools and mud contours can cross cell edges.
    ordered = sorted(common)
    low = ordered[int(0.02 * (len(ordered) - 1))]
    high = ordered[int(0.98 * (len(ordered) - 1))]
    span = max(high - low, 1e-9)
    height = [clamp01((a + (b - a) * w - low) / span)
              for a, b, w in zip(common, own, ctx.interior_mask)]
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    grit = [a + (b - a) * w for a, b, w in zip(common_grit, own_grit, ctx.interior_mask)]
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    pools = [a + (b - a) * w for a, b, w in zip(common_pools, own_pools, ctx.interior_mask)]
    rgb = mix(rgb, p["sheen"], pools, 0.4)
    return rgb, None


def mud_fields(rng, size):
    """Periodic mud height, grain and wet pools at the original feature scale."""
    height = fbm(rng, 4, 4, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 14)
    grit = lattice_noise(rng, 48, size=size)
    pools = [0.0] * (size * size)
    # Pools may wrap around the block, so shared boundary pools remain whole.
    for _ in range(rng.randint(2, 3) * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(pools, x, y, rng.uniform(16, 24), softness=0.85)
    return height, grit, pools


@recipe(name="marsh", group="rough", label="Marsh", profile="soft",
        seam={"height": 1},
        palette={"tussock": (138, 140, 72), "dry": (176, 156, 96), "green": (100, 124, 58),
                 "moss": (78, 92, 52), "water": (112, 128, 146), "sky": (156, 170, 184),
                 "rim": (46, 50, 40), "reed": (132, 100, 60)},
        style=Style(luma=104, std=22, grain_max=16, match=0.45), periodic=True, grid=8, blocks=4)
def render_marsh(ctx):
    """Whole shared boundary tussocks, pools and reeds over varied wet moss."""
    p, size = RECIPES["marsh"].palette, ctx.size
    common = fbm(ctx.shared_rng, 6, 3, size=size)
    own = fbm(ctx.rng, 6, 3, size=size)
    bed = normalize_like(blend_fields(common, own, ctx.interior_mask), common)
    rgb = ramp(bed, [(0.0, p["moss"]), (0.6, p["green"]), (1.0, p["tussock"])])
    shared, interior = block_object_field(ctx, 22, (6.0, 9.5), spacing=0.9)
    blades, blade_rgb = marsh_fans(shared + interior, size, p)
    rgb = mix(rgb, p["moss"], [b * 0.6 for b in shifted(blades, 2, 2)], 0.5)
    rgb = mix(rgb, blade_rgb, blades, 1.0)

    # Two independent pool beds, retaining half of the second, preserve the
    # original average of one-and-a-half pools per native tile.
    shared, interior = block_object_field(ctx, 1, (12.0, 18.0), spacing=1.0)
    pool_objects = shared + interior
    shared, interior = block_object_field(ctx, 1, (12.0, 18.0), spacing=1.0)
    pool_objects += [obj for obj in shared + interior if obj[3] < 0.5]
    pools = marsh_pools(pool_objects, size)
    rim = [smoothstep(0.0, 0.12, v) for v in pools]
    inner = [smoothstep(0.12, 0.3, v) for v in pools]
    common_water = fbm(ctx.shared_rng, 4, 2, size=size)
    own_water = fbm(ctx.rng, 4, 2, size=size)
    sky = normalize_like(blend_fields(common_water, own_water, ctx.interior_mask), common_water)
    water = mix(flat(p["water"], size=size), p["sky"], sky, 0.6)
    rgb = mix(rgb, p["rim"], rim, 0.9)
    rgb = mix(rgb, water, inner, 1.0)

    shared, interior = block_object_field(ctx, 1, (8.0, 12.0), spacing=1.0)
    reed_objects = shared + interior
    shared, interior = block_object_field(ctx, 1, (8.0, 12.0), spacing=1.0)
    reed_objects += [obj for obj in shared + interior if obj[3] < 0.5]
    reeds, _ = marsh_fans(reed_objects, size, p, reeds=True)
    rgb = mix(rgb, p["rim"], shifted(reeds, 2, 2), 0.35)
    return mix(rgb, p["reed"], reeds, 0.9), None


def marsh_fans(objects, size, palette, reeds=False):
    """Rasterize each fan on a small local patch, then wrap onto the block."""
    mask = [0.0] * (size * size)
    rgb = [[0.0] * (size * size) for _ in range(3)] if not reeds else None
    patch_size, centre = 64, 32
    for x, y, radius, u in sorted(objects, key=lambda obj: obj[1]):
        local = [0.0] * (patch_size * patch_size)
        count = 5 + int(u * 5) if reeds else 9
        for j in range(count):
            if reeds:
                angle = -math.pi / 2 + (((u * 97 + j * 0.618) % 1) - 0.5) * 1.2
                x0 = centre + (((u * 43 + j * 0.37) % 1) - 0.5) * 10
                y0 = centre + (((u * 17 + j * 0.53) % 1) - 0.5) * 8
                length = 7 + 5 * ((u * 29 + j * 0.47) % 1)
                stamp_stroke(local, x0, y0, angle, length, 1.8)
            else:
                t = (u * 97 + j * 0.618) % 1
                angle = -math.pi / 2 + (t - 0.5) * 2.6
                length = radius * (0.8 + 0.6 * ((u * 13 + j * 0.37) % 1))
                stamp_stroke(local, centre, centre + radius * 0.35, angle, length, 2.2)
        tone = [mix_channel(palette["green"][k], palette["tussock"][k], palette["dry"][k], (u * 5.3) % 1)
                for k in range(3)] if not reeds else None
        shade = 0.85 + 0.3 * ((u * 7.7) % 1)
        origin_x, origin_y = math.floor(x) - centre, math.floor(y) - centre
        for i, value in enumerate(local):
            if not value:
                continue
            index = ((origin_y + i // patch_size) % size) * size + (origin_x + i % patch_size) % size
            if value > mask[index]:
                mask[index] = value
                if rgb is not None:
                    for k in range(3):
                        rgb[k][index] = tone[k] * shade * (0.85 + 0.25 * value)
    return mask, rgb


def marsh_pools(objects, size):
    """Wrapped irregular pool domes; no full-canvas allocation per pool."""
    heights = [0.0] * (size * size)
    for x, y, radius, u in objects:
        lumps = [(h, 0.08 + 0.08 * ((u * 31 + h * 0.37) % 1), TAU * ((u * 53 + h * 0.47) % 1))
                 for h in (2, 3, 4)]
        reach = math.ceil(radius * 1.5) + 1
        for uy in range(math.floor(y) - reach, math.floor(y) + reach + 1):
            for ux in range(math.floor(x) - reach, math.floor(x) + reach + 1):
                dx, dy = ux + 0.5 - x, uy + 0.5 - y
                angle = math.atan2(dy, dx)
                local = radius * (1 + sum(a * math.sin(h * angle + ph) for h, a, ph in lumps))
                distance = math.hypot(dx, dy) / local
                if distance < 1:
                    index = (uy % size) * size + ux % size
                    heights[index] = max(heights[index], math.sqrt(1 - distance * distance))
    return heights


def mix_channel(a, b, c, t):
    """Three-stop ramp for one channel at t in [0, 1]."""
    return a + (b - a) * (t / 0.5) if t < 0.5 else b + (c - b) * ((t - 0.5) / 0.5)


@recipe(name="deep_snow", group="rough", label="Deep snow", profile="soft",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512, "fringe": [196, 210, 232], "fringe_q8": 64,
              "fringe_width_q8": 384},
        palette={"dark": (156, 170, 210), "mid": (204, 208, 234), "light": (236, 236, 250), "sparkle": (252, 252, 255)},
        style=Style(luma=203, std=9, grain_max=6), periodic=True, grid=8, blocks=4)
def render_deep_snow(ctx):
    """Interchangeable snow blocks: common edge drifts, varied interior mounds.

    Height, grain and sparkle masks share one periodic field at the perimeter.
    Only the interior's scalar fields vary; their common quantile range and
    lighting scale keep a layout's snow from retinting its shared boundaries.
    """
    p = RECIPES["deep_snow"].palette
    shared_rng, own_rng = ctx.shared_rng, ctx.rng
    common_raw = fbm(shared_rng, 3, 3, size=ctx.size)
    own_raw = fbm(own_rng, 3, 3, size=ctx.size)
    ordered = sorted(common_raw)
    low, high = ordered[int(0.02 * (len(ordered) - 1))], ordered[int(0.98 * (len(ordered) - 1))]
    span = high - low or 1.0
    common_height = [clamp01((v - low) / span) for v in common_raw]
    own_height = [clamp01((v - low) / span) for v in own_raw]
    interior = ctx.interior_mask
    height = [a + (b - a) * w for a, b, w in zip(common_height, own_height, interior)]
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(own_rng, 48, size=ctx.size)
    fine = [a + (b - a) * w for a, b, w in zip(common_grain, own_grain, interior)]
    rgb = [[c * (1 + 0.05 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    rgb = shade(rgb, height, 0.12, light=(-0.6, -0.8), reference_height=common_height)
    shared_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared_ctx.rng = shared_rng
    common_sparkle, _, _ = soft_blobs(shared_ctx, 6, 3.0, 4.0)
    own_sparkle, _, _ = soft_blobs(ctx, 6, 3.0, 4.0)
    sparkle = [a + (b - a) * w for a, b, w in zip(common_sparkle, own_sparkle, interior)]
    rgb = mix(rgb, p["sparkle"], sparkle, 0.6)
    return rgb, None


@recipe(name="scree", group="rough", label="Scree", profile="rock",
        seam={"height": 1},
        palette={"dark": (84, 86, 92), "mid": (118, 120, 124), "light": (152, 154, 156), "dirt": (62, 56, 50)},
        style=Style(luma=118, std=14, grain_max=12), periodic=True, grid=8, blocks=4)
def render_scree(ctx):
    p = RECIPES["scree"].palette
    size, shared, own = ctx.size, ctx.shared_rng, ctx.rng
    count = shared.randint(12, 16) * ctx.area_scale
    points = [(shared.random() * size, shared.random() * size) for _ in range(count)]
    angles = [shared.random() * TAU for _ in range(count)]
    rotations = [(math.cos(a), math.sin(a)) for a in angles]
    cell = size / math.sqrt(count)

    def chip_fields(positions, turns):
        first, second, nearest = _worley_from_points(positions, "angular", turns, 1.35, size=size)
        return [v / cell for v in first], [(b - a) / cell for a, b in zip(first, second)], nearest

    master_first, master_edge, ids = chip_fields(points, rotations)
    f1, edge = master_first, master_edge
    if ctx.variant:
        # Boundary chips are the same actual stones in every layout. Only
        # points safely inside the block move; no unrelated RGB images are
        # cross-faded, so chip outlines and their bevels remain intact.
        # Certify that a moved point cannot change F1 or F2 in the shared
        # band, including the extra pixel sampled by bevel lighting. The
        # angular metric is at least Euclidean / sqrt(1 + 1 / stretch**2).
        # A fixed guard can fail in a sparse patch when the block gets larger.
        shared_second = max((a + b) * cell for i, (a, b) in enumerate(zip(master_first, master_edge))
                            if min(i % size, i // size, size - 1 - i % size, size - 1 - i // size) < 33)
        guard = math.ceil(33 + shared_second * math.sqrt(1 + 1 / 1.35 ** 2)) + 1
        interior = [(i, x, y) for i, (x, y) in enumerate(points)
                    if min(x, y, size - x, size - y) > guard]
        positions, turns = list(points), list(rotations)
        for i, _, _ in interior:
            positions[i] = (own.uniform(guard, size - guard), own.uniform(guard, size - guard))
            angle = own.random() * TAU
            turns[i] = (math.cos(angle), math.sin(angle))
        f1, edge, ids = chip_fields(positions, turns)

    master_grit = fbm(shared, 10, 2, size=size)
    grit = master_grit
    weight = ctx.interior_mask
    if ctx.variant:
        grit = [a + (b - a) * w for a, b, w in zip(master_grit, fbm(own, 10, 2, size=size), weight)]
    master_tone = [0.25 * (1 - a) + 0.75 * g for a, g in zip(master_first, master_grit)]
    ordered = sorted(master_tone)
    low, high = (ordered[int(q * (len(ordered) - 1))] for q in (0.05, 0.95))
    # One shared stretch prevents a different interior changing edge colours.
    tone = [clamp01((0.25 * (1 - a) + 0.75 * g - low) / (high - low)) for a, g in zip(f1, grit)]
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(shared, 16 * ctx.area_scale, 0.14))
    top = [smoothstep(0.08, 0.30, e) for e in edge]
    master_top = [smoothstep(0.08, 0.30, e) for e in master_edge]
    rgb = shade(rgb, top, 0.45, light=(-0.7, -0.7), reference_height=master_top)
    gaps = band(edge, 0.06, 0.06)
    rgb = mix(rgb, p["dirt"], gaps, 0.85)
    fine = lattice_noise(shared, 48, size=size)
    if ctx.variant:
        fine = [a + (b - a) * w for a, b, w in zip(fine, lattice_noise(own, 48, size=size), weight)]
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    return rgb, None


# Paths: warm and light, faster movement.

@recipe(name="dirt_track", group="paths", label="Dirt track", profile="sand",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (142, 116, 82), "mid": (170, 142, 104), "light": (192, 166, 126)},
        style=Style(luma=146, std=7, grain_max=6), periodic=True, grid=8, blocks=4)
def render_dirt_track(ctx):
    """Shared edge soil and rut lines, with varied wear and wobble inside."""
    p = RECIPES["dirt_track"].palette
    common, common_fine, common_worn, common_wobble, centre = dirt_track_fields(ctx.shared_rng, ctx.size)
    own, own_fine, own_worn, own_wobble, _ = dirt_track_fields(ctx.rng, ctx.size)
    interior = ctx.interior_mask
    height = normalize_like(blend_fields(common, own, interior), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_fine, own_fine, interior)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    worn = blend_fields(common_worn, own_worn, interior)
    rgb = mix(rgb, p["light"], worn, 0.5)
    # Keep one common pair of centre lines per tile-height period. Scalar
    # wobble changes smoothly inside each layout without splitting the ruts.
    wobble = blend_fields(common_wobble, own_wobble, interior)
    ruts = []
    for i in range(ctx.size * ctx.size):
        y = i // ctx.size + (wobble[i] - 0.5) * 6
        d = min(abs((y - centre + N / 2) % N - N / 2),
                abs((y - centre - 32 + N / 2) % N - N / 2))
        ruts.append(1 - smoothstep(2.0, 6.0, d))
    rgb = [[c * (1 - 0.04 * r) for c, r in zip(channel, ruts)] for channel in rgb]
    return rgb, None


def dirt_track_fields(rng, size):
    """Periodic soil, fine grain, worn patches and rut wobble at native scale."""
    height = fbm(rng, 4, 3, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    fine = lattice_noise(rng, 48, size=size)
    worn = [0.0] * (size * size)
    for _ in range(3 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(worn, x, y, rng.uniform(14, 22), softness=0.9)
    centre = 40 + rng.random() * 16
    wobble = fbm(rng, 2, 2, size=size)
    return height, fine, worn, wobble, centre


@recipe(name="boardwalk", group="paths", label="Boardwalk", profile="crisp",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (108, 90, 68), "mid": (156, 134, 104), "light": (182, 160, 128), "gap": (76, 60, 44)},
        style=Style(luma=134, std=11, grain_max=10, match=0.6), periodic=True, grid=8, blocks=4,
        note="planks run one way: accepted compromise")
def render_boardwalk(ctx):
    """Whole common planks and joints, with independently varied interior wood."""
    p = RECIPES["boardwalk"].palette
    shared, own = ctx.shared_rng, ctx.rng
    size = ctx.size
    planks = 4 * (size // N)
    height = size // planks
    common_raw = fbm(shared, 2, 3, cells_y=24, size=size)
    own_raw = fbm(own, 2, 3, cells_y=24, size=size)
    common_wood = normalize_like(common_raw, common_raw)
    own_wood = normalize_like(own_raw, common_raw)
    tone = [0.0] * (size * size)
    gaps = [0.0] * (size * size)
    # Joint geometry and plank lighting are common, so an alternate interior
    # never fades between incompatible plank ends or cuts a boundary board.
    shifts = [int(shared.random() * size) for _ in range(planks)]
    brightness = [1 + (shared.random() - 0.5) * 0.18 for _ in range(planks)]
    joints = [shared.random() * N for _ in range(planks)]
    interior = ctx.interior_mask
    for i in range(size * size):
        x, y = i % size, i // size
        k = y // height
        sample = y * size + (x + shifts[k]) % size
        wood = common_wood[sample] + (own_wood[sample] - common_wood[sample]) * interior[i]
        tone[i] = clamp01(wood * brightness[k])
        edge = min(y - k * height, (k + 1) * height - y)
        gap = 1 - smoothstep(1.0, 3.0, edge + 0.5)
        jx = abs(((x + 0.5 - joints[k]) + N / 2) % N - N / 2)
        gaps[i] = max(gap, 1 - smoothstep(1.0, 2.5, jx))
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared, 48, size=size)
    own_grain = lattice_noise(own, 48, size=size)
    fine = blend_fields(common_grain, own_grain, interior)
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    return mix(rgb, p["gap"], gaps, 0.85), None


# Fertile ground: warm, saturated undertone; the new strategic resource.

@recipe(name="loam", group="fertile", label="Loam", profile="soft",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (52, 38, 24), "mid": (74, 58, 36), "light": (98, 80, 52), "fleck": (96, 108, 56)},
        style=Style(luma=66, std=8, grain_max=8), periodic=True, grid=8, blocks=4)
def render_loam(ctx):
    """Common periodic soil and flecks at the perimeter, varied interior beds."""
    p = RECIPES["loam"].palette
    common, common_grit, common_flecks = loam_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_flecks = loam_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    # Green-brown flecks remain soft, sparse and about two native pixels wide.
    flecks = blend_fields(common_flecks, own_flecks, ctx.interior_mask)
    return mix(rgb, p["fleck"], flecks, 0.8), None


def loam_fields(rng, size):
    """Periodic loam height, fine grain and flecks at the existing native scale."""
    height = fbm(rng, 5, 4, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    grit = lattice_noise(rng, 48, size=size)
    flecks = [0.0] * (size * size)
    for _ in range(22 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = 4.0 + rng.random() * 1.5
        rng.random()  # Retain the per-disc choice draw used by soft_blobs.
        stamp_disc(flecks, x, y, radius, softness=0.75)
    return height, grit, flecks


@recipe(name="moss", group="fertile", label="Moss", profile="soft",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (26, 60, 38), "mid": (38, 84, 50), "light": (62, 110, 68)},
        style=Style(luma=68, std=7, grain_max=6), periodic=True, grid=8, blocks=4)
def render_moss(ctx):
    """Shared edge pillows with independent moss cushions farther inside."""
    p = RECIPES["moss"].palette
    f1, _, _, common_f1, _, _ = block_worley(ctx, 12)
    common_pillows_raw = [1 - v for v in common_f1]
    common_pillows = normalize_like(common_pillows_raw, common_pillows_raw)
    pillows = normalize_like([1 - v for v in f1], common_pillows_raw)
    common_detail = fbm(ctx.shared_rng, 6, 3, size=ctx.size)
    own_detail = fbm(ctx.rng, 6, 3, size=ctx.size)
    detail = blend_fields(common_detail, own_detail, ctx.interior_mask)
    common_raw = [0.6 * a + 0.4 * b for a, b in zip(common_pillows, common_detail)]
    raw = [0.6 * a + 0.4 * b for a, b in zip(pillows, detail)]
    common_height = normalize_like(common_raw, common_raw)
    height = normalize_like(raw, common_raw)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, height, 0.2, reference_height=common_height)
    common_grain = lattice_noise(ctx.shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(ctx.rng, 48, size=ctx.size)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.10 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    return rgb, None


@recipe(name="spring_meadow", group="fertile", label="Spring meadow", profile="brush",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (78, 118, 50), "mid": (104, 146, 66), "light": (134, 170, 90), "dot": (236, 228, 170)},
        style=Style(luma=105, std=7, grain_max=7), periodic=True, grid=8, blocks=4)
def render_spring_meadow(ctx):
    """Common edge turf and blooms, with independently seeded interiors."""
    p = RECIPES["spring_meadow"].palette
    shared_rng, own_rng = ctx.shared_rng, ctx.rng
    common_raw = fbm(shared_rng, 8, 3, size=ctx.size)
    own_raw = fbm(own_rng, 8, 3, size=ctx.size)
    common_height = normalize_like(common_raw, common_raw, 0.02, 0.98)
    own_height = normalize_like(own_raw, common_raw, 0.02, 0.98)
    height = blend_fields(common_height, own_height, ctx.interior_mask)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    common_grain = lattice_noise(shared_rng, 48, size=ctx.size)
    own_grain = lattice_noise(own_rng, 48, size=ctx.size)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    rgb = desaturate(rgb, 0.18)
    shared_ctx = Ctx(ctx.name, 0, ctx.phase, ctx.phases, size=ctx.size)
    shared_ctx.rng = shared_rng
    common_dots, _, _ = soft_blobs(shared_ctx, 10, 4.0, 5.5)
    own_dots, _, _ = soft_blobs(ctx, 10, 4.0, 5.5)
    dots = blend_fields(common_dots, own_dots, ctx.interior_mask)
    rgb = mix(rgb, p["dot"], dots, 0.7)
    return rgb, None


# Grass and sand looks: the same ground as the classic grass and sand, built
# from the same common-edge and per-layout fields, so they keep the soft (grass)
# and speckled (sand) grain and only shift the colour and a few small accents.

@recipe(name="lush_grass", group="grass", label="Lush grass", profile="sand",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (14, 80, 40), "mid": (26, 104, 52), "light": (48, 128, 64), "clump": (10, 66, 34)},
        style=Style(luma=72, std=6, grain_max=6), periodic=True, grid=8, blocks=4)
def render_lush_grass(ctx):
    """Classic grass grain in deeper greens, with darker clumps for body."""
    p, size = RECIPES["lush_grass"].palette, ctx.size
    rgb = grass_look(ctx, p)
    clumps = blend_fields(fbm(ctx.shared_rng, 18, 2, size=size), fbm(ctx.rng, 18, 2, size=size),
                          ctx.interior_mask)
    return mix(rgb, p["clump"], [smoothstep(0.58, 0.74, v) for v in clumps], 0.45), None


@recipe(name="dry_grass", group="grass", label="Dry grass", profile="sand",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (92, 100, 36), "mid": (120, 124, 52), "light": (150, 146, 74), "straw": (176, 164, 96)},
        style=Style(luma=114, std=7, grain_max=7), periodic=True, grid=8, blocks=4)
def render_dry_grass(ctx):
    """Classic grass grain in olive, with short pale straw streaks."""
    p, size = RECIPES["dry_grass"].palette, ctx.size
    rgb = grass_look(ctx, p)
    # Streaks: noise stretched along x, kept to its brightest ridges.
    straw = blend_fields(fbm(ctx.shared_rng, 40, 2, cells_y=10, size=size),
                         fbm(ctx.rng, 40, 2, cells_y=10, size=size), ctx.interior_mask)
    return mix(rgb, p["straw"], [smoothstep(0.66, 0.8, v) for v in straw], 0.5), None


def grass_look(ctx, p):
    """Classic grass turf through another dark/mid/light palette."""
    common, common_grain = grass_fields(ctx.shared_rng, ctx.size)
    own, own_grain = grass_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    return [[c * (1 + 0.05 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]


@recipe(name="dune_sand", group="sand", label="Dune sand", profile="sand",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (176, 120, 60), "mid": (206, 152, 84), "light": (230, 186, 118)},
        style=Style(luma=158, std=20, grain_max=26, match=0.5), periodic=True, grid=8, blocks=4)
def render_dune_sand(ctx):
    """Classic sand grain in warm orange, with its mineral flecks."""
    p = RECIPES["dune_sand"].palette
    rgb, flecks = sand_look(ctx, p)
    return mix(rgb, p["dark"], flecks, 0.18), None


@recipe(name="pale_sand", group="sand", label="Pale sand", profile="sand",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (190, 180, 146), "mid": (214, 204, 170), "light": (234, 226, 196), "shell": (250, 246, 232)},
        style=Style(luma=198, std=14, grain_max=24, match=0.5), periodic=True, grid=8, blocks=4)
def render_pale_sand(ctx):
    """Classic sand grain bleached pale, its flecks turned to shell fragments."""
    p = RECIPES["pale_sand"].palette
    rgb, flecks = sand_look(ctx, p)
    return mix(rgb, p["shell"], flecks, 0.7), None


def sand_look(ctx, p):
    """Sand dunes and lit pebble grains through a palette, and its flecks."""
    common, common_flecks = sand_fields(ctx.shared_rng, ctx.size)
    own, own_flecks = sand_fields(ctx.rng, ctx.size)
    # Lit grains two or three native pixels across, as the original tiles
    # have, rather than independent single-pixel noise.
    count = SAND_GRAINS
    first, second, ids, common_first, common_second, common_ids = block_worley(ctx, count)
    tones = [ctx.shared_rng.random() for _ in range(count * ctx.area_scale)]
    def grains(f1, f2, cells):
        return [smoothstep(0.0, 0.35, b - a) * (0.55 + 0.45 * tones[i]) for a, b, i in zip(f1, f2, cells)]
    own_grains, common_grains = grains(first, second, ids), grains(common_first, common_second, common_ids)
    surface = [SAND_DUNES * s + (1 - SAND_DUNES) * g for s, g in zip(blend_fields(common, own, ctx.interior_mask), own_grains)]
    reference = [SAND_DUNES * s + (1 - SAND_DUNES) * g for s, g in zip(common, common_grains)]
    height = normalize_like(surface, reference, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, own_grains, SAND_BEVEL, reference_height=common_grains)
    return rgb, blend_fields(common_flecks, own_flecks, ctx.interior_mask)


# Water: opaque animated tiles in the colour of the retired scrolling ocean
# backdrop (data/gfx/water0.png averaged about (69, 52, 200), luma 74). Water
# and deep water are cells of one 4x4-cell periodic block (`variant_grid`), so
# a wave field four cells across continues over every cell edge. The field is a
# sum of plane waves with whole wave numbers on that block, each advancing a
# whole number of cycles per loop: every cell animates locally and all cells
# share the clock, so crests roll across open water in one direction. A swell
# toward the lower right sets the bands; finer cross chop breaks their crests
# into moving glints. Deep water reads the same field (same seed and wind), so
# crests carry on across the shallow/deep blend. Statistics only, no pixels
# read.

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


def render_waves(ctx, palette, swell_weight, chop_weight, glint, front):
    """Swell bands lit on their leading face, with glints where chop crests
    ride swell crests."""
    common = wave_fields(ctx.phase, ctx.phases)
    if ctx.size == WAVE_SIZE and ctx.variant:
        own = wave_fields(ctx.phase, ctx.phases, ctx.variant)
        fields = tuple(blend_fields(a, b, ctx.interior_mask) for a, b in zip(common, own))
    else:
        fields = common
    s, c, f = (wave_cell(ctx, values) for values in fields)
    t = [0.5 + swell_weight * a + chop_weight * b for a, b in zip(s, c)]
    rgb = ramp([smoothstep(0.05, 0.5, v) for v in t], [(0.0, palette["deep"]), (1.0, palette["base"])])
    rgb = mix(rgb, palette["light"], [0.8 * smoothstep(0.45, 0.85, v) + front * smoothstep(0, 1, d)
                                      for v, d in zip(t, f)], 1.0)
    glints = [smoothstep(0.0, 0.6, a) * smoothstep(0.22, 0.55, b) for a, b in zip(s, c)]
    return mix(rgb, palette["glint"], glints, glint)


@recipe(name="water", group="water", label="Water", profile="sand",
        seam={"height": 5, "cast_q8": 72, "cast_width_q8": 640},
        palette={"deep": (50, 36, 168), "base": (68, 52, 198), "light": (86, 72, 216), "glint": (156, 150, 244)},
        style=Style(luma=74, std=8, grain_max=8, match=0.0), phases=WAVE_PHASES, animation_ticks=6,
        periodic=True, grid=WAVE_GRID, preview=(70, 50, 191), minimap=(0, 40, 120))
def render_water(ctx):
    return render_waves(ctx, RECIPES["water"].palette, 0.35, 0.2, 0.55, 0.2), None


@recipe(name="deep_water", group="deep_water", label="Deep water", profile="soft",
        seam={"height": 6, "cast_q8": 72, "cast_width_q8": 640},
        palette={"deep": (40, 28, 132), "base": (52, 38, 152), "light": (62, 48, 170), "glint": (110, 96, 214)},
        style=Style(luma=56, std=6, grain_max=7, match=0.0), phases=WAVE_PHASES, animation_ticks=6,
        periodic=True, grid=WAVE_GRID, preview=(44, 34, 120), minimap=(24, 18, 84))
def render_deep_water(ctx):
    return render_waves(ctx, RECIPES["deep_water"].palette, 0.4, 0.12, 0.3, 0.15), None


@recipe(name="dark_water", group="deep_water", label="Dark water", profile="soft",
        seam={"height": 7, "cast_q8": 80, "cast_width_q8": 704},
        palette={"tint": (40, 30, 104), "murk": (32, 24, 84), "speck": (20, 15, 52)},
        style=Style(luma=38, std=4, grain_max=5, match=0.5), periodic=True, grid=8, blocks=4,
        preview=(32, 26, 76), minimap=(16, 12, 52))
def render_dark_water(ctx):
    p = RECIPES["dark_water"].palette
    common, common_specks = dark_water_fields(ctx.shared_rng, ctx.size)
    own, own_specks = dark_water_fields(ctx.rng, ctx.size)
    murk = normalize_like(blend_fields(common, own, ctx.interior_mask), common)
    rgb = mix(flat(p["tint"], size=ctx.size), p["murk"], murk, 0.6)
    specks = blend_fields(common_specks, own_specks, ctx.interior_mask)
    rgb = mix(rgb, p["speck"], specks, 0.6)
    return rgb, None


def dark_water_fields(rng, size):
    """Static periodic murk and sparse soft specks at the native feature scale."""
    murk = fbm(rng, 3, 2, size=size)
    specks = [0.0] * (size * size)
    for _ in range(8 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = rng.uniform(3.0, 4.5)
        rng.random()  # The original scatter's per-speck choice draw.
        stamp_disc(specks, x, y, radius, softness=0.75)
    return murk, specks


@recipe(name="ridge_rock", group="ridges", label="Ridge", profile="rock",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={"dark": (72, 72, 76), "mid": (104, 104, 106), "light": (138, 138, 136), "crack": (46, 46, 50)},
        style=Style(luma=102, std=12, grain_max=10), periodic=True, grid=8, blocks=4)
def render_ridge_rock(ctx):
    p = RECIPES["ridge_rock"].palette
    common, common_grain, common_ridges, common_widths = ridge_rock_fields(ctx.shared_rng, ctx.size)
    own, own_grain, own_ridges, own_widths = ridge_rock_fields(ctx.rng, ctx.size)
    weight = ctx.interior_mask
    # Shared scalar strata and crack geometry join before palette mapping;
    # the common stretch and lighting scale keep the perimeter unchanged.
    h = normalize_like(blend_fields(common, own, weight), common, 0.03, 0.97)
    common_h = normalize_like(common, common, 0.03, 0.97)
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, weight)
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    ridged = blend_fields(common_ridges, own_ridges, weight)
    widths = blend_fields(common_widths, own_widths, weight)
    cracks = [1 - smoothstep(0.0, 0.05 + 0.10 * w, r) for r, w in zip(ridged, widths)]
    rgb = shade(rgb, h, 0.4, light=(-1.0, -0.3), reference_height=common_h)
    rgb = mix(rgb, p["crack"], cracks, 0.6)
    return rgb, None


def ridge_rock_fields(rng, size):
    """Periodic strata, grain and sparse variable-width cracks at native scale."""
    strata = fbm(rng, 2, 3, cells_y=8, gain=0.4, size=size)
    strata = domain_warp(strata, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 12, 3)
    base = fbm(rng, 5, 3, size=size)
    height = [0.75 * s + 0.25 * b for s, b in zip(strata, base)]
    fine = lattice_noise(rng, 48, size=size)
    ridged = [abs(2 * v - 1) for v in domain_warp(
        fbm(rng, 2, 2, cells_y=5, size=size),
        fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8, 3)]
    widths = fbm(rng, 3, 2, size=size)
    return height, fine, ridged, widths


@recipe(name="outcrop", group="ridges", label="Outcrop", profile="rock",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={"dark": (80, 80, 76), "mid": (110, 112, 108), "light": (140, 142, 136), "lichen": (120, 130, 70)},
        style=Style(luma=108, std=12, grain_max=10), periodic=True, grid=8, blocks=4,
        note="placeholder until the image-generated outcrop lands")
def render_outcrop(ctx):
    """Whole boundary slabs with independently cracked and weathered interiors."""
    p = RECIPES["outcrop"].palette
    f1, f2, ids, common_f1, common_f2, _ = block_worley(ctx, 5)
    common_wx = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    common_wy = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    wx = blend_fields(common_wx, fbm(ctx.rng, 4, 2, size=ctx.size), ctx.interior_mask)
    wy = blend_fields(common_wy, fbm(ctx.rng, 4, 2, size=ctx.size), ctx.interior_mask)
    common_edge = domain_warp([b - a for a, b in zip(common_f1, common_f2)], common_wx, common_wy, 6)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 6)
    common_grit = fbm(ctx.shared_rng, 10, 3, size=ctx.size)
    grit = blend_fields(common_grit, fbm(ctx.rng, 10, 3, size=ctx.size), ctx.interior_mask)
    tone = normalize_like(grit, common_grit, 0.05, 0.95)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, 5 * ctx.area_scale, 0.10))
    # Reference lighting keeps shared slab bevels identical when interior slabs move.
    rim = [smoothstep(0.0, 0.3, e) for e in edge]
    common_rim = [smoothstep(0.0, 0.3, e) for e in common_edge]
    rgb = shade(rgb, rim, 0.4, light=(-0.8, -0.8), reference_height=common_rim)
    gaps = band(edge, 0.03, 0.05)
    rgb = mix(rgb, p["dark"], gaps, 0.65)
    common_fine = lattice_noise(ctx.shared_rng, 48, size=ctx.size)
    fine = blend_fields(common_fine, lattice_noise(ctx.rng, 48, size=ctx.size), ctx.interior_mask)
    rgb = [[c * (1 + 0.10 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    common_lichen = outcrop_lichen(ctx.shared_rng, ctx.size, 10 * ctx.area_scale)
    own_lichen = outcrop_lichen(ctx.rng, ctx.size, 10 * ctx.area_scale)
    lichen = blend_fields(common_lichen, own_lichen, ctx.interior_mask)
    lichen = [l * (1 - g) for l, g in zip(lichen, gaps)]
    rgb = mix(rgb, p["lichen"], lichen, 0.75)
    return rgb, None


def outcrop_lichen(rng, size, count):
    """Small periodic lichen flecks, retaining their native density and scale."""
    mask = [0.0] * (size * size)
    for _ in range(count):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(mask, x, y, rng.uniform(2.6, 4.2), softness=0.6)
    return mask


# Void: nothing crosses, nothing flies. The depth cue is a cast onto every
# neighbour (rank above all other materials), since a hole this dark cannot
# show a lip of its own.

@recipe(name="void_hole", group="void", label="Hole", profile="crisp",
        seam={"height": 8, "cast_q8": 96, "cast_width_q8": 512},
        palette={"dark": (6, 6, 10), "mid": (10, 10, 14), "light": (14, 14, 20)},
        style=Style(luma=10, std=2, grain_max=3), periodic=True, grid=8, blocks=4,
        preview=(28, 24, 36), minimap=(16, 14, 22))
def render_void_hole(ctx):
    p = RECIPES["void_hole"].palette
    common = fbm(ctx.shared_rng, 3, 2, size=ctx.size)
    own = fbm(ctx.rng, 3, 2, size=ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    # Keep the hole quiet and nearly black, with only subtle interior depth.
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    return rgb, None


@recipe(name="chasm", group="void", label="Chasm", profile="cliff",
        seam={"height": 8, "cast_q8": 96, "cast_width_q8": 512},
        palette={"dark": (26, 20, 24), "mid": (46, 38, 42), "light": (74, 64, 66), "floor": (6, 4, 8)},
        style=Style(luma=40, std=9, grain_max=8), periodic=True, grid=8, blocks=4,
        preview=(48, 38, 44), minimap=(30, 22, 28))
def render_chasm(ctx):
    p = RECIPES["chasm"].palette
    # Boundary channels retain the same actual Voronoi cells in every layout.
    # Interior cells move without cross-fading unrelated rock-wall images.
    f1, _, ids, _, _, _ = block_worley(ctx, 6, metric="angular", stretch=2.6)
    count = 6 * ctx.area_scale
    deep = [1.0 if k < 2 * ctx.area_scale else 0.0
            for k in ctx.shared_rng.sample(range(count), count)]
    if ctx.variant == 0:
        ctx.rng.setstate(ctx.shared_rng.getstate())
    common, common_grain = chasm_fields(ctx.shared_rng, ctx.size)
    own, own_grain = chasm_fields(ctx.rng, ctx.size)
    h = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.03, 0.97)
    common_h = normalize_like(common, common, 0.03, 0.97)
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, h, 0.7, light=(-1.0, 0.0), reference_height=common_h)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    floor = [(1 - smoothstep(0.18, 0.34, d)) * deep[i] for d, i in zip(f1, ids)]
    rgb = mix(rgb, p["floor"], floor, 0.9)
    return rgb, None


def chasm_fields(rng, size):
    """Periodic layered rock walls and fine grain at the original scale."""
    strata = domain_warp(fbm(rng, 2, 3, cells_y=6, size=size),
                         fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8, 3)
    base = fbm(rng, 4, 3, size=size)
    height = [0.6 * s + 0.4 * b for s, b in zip(strata, base)]
    fine = lattice_noise(rng, 48, size=size)
    return height, fine


# Obstacles: height cues (lit top, cast shadow); placeholders for image-generated art.

@recipe(name="boulders", group="obstacles", label="Boulders", profile="rock",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"ground_dark": (112, 100, 84), "ground": (128, 114, 96), "ground_light": (144, 130, 110),
                 "rock_dark": (84, 88, 92), "rock_light": (138, 142, 142), "shadow": (40, 36, 34)},
        style=Style(luma=108, std=14, grain_max=12, match=0.5),
        periodic=True, grid=8, blocks=4,
        note="placeholder until the image-generated boulders land")
def render_boulders(ctx):
    """Dusty ground under the raised boulder decor: quiet warm earth with fine
    grit and a few faint stones, so the decor sprites carry the boulders."""
    p = RECIPES["boulders"].palette
    common, common_grit, common_pebbles = boulders_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_pebbles = boulders_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["ground_dark"]), (0.5, p["ground"]), (1.0, p["ground_light"])])
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = [[c * (1 + 0.1 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    pebbles = blend_fields(common_pebbles, own_pebbles, ctx.interior_mask)
    rgb = mix(rgb, p["ground_dark"], pebbles, 0.35)
    return rgb, None


def boulders_fields(rng, size):
    """Periodic dusty earth and faint pebbles under the unchanged raised decor."""
    height = fbm(rng, 6, 3, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 6)
    grit = lattice_noise(rng, 48, size=size)
    pebbles = [0.0] * (size * size)
    for _ in range(10 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = rng.uniform(2.5, 4.0)
        rng.random()  # Retain soft_blobs' per-disc choice draw.
        stamp_disc(pebbles, x, y, radius, softness=0.75)
    return height, grit, pebbles


@recipe(name="hedge", group="obstacles", label="Hedge", profile="canopy",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"dark": (22, 50, 24), "mid": (36, 76, 36), "light": (64, 108, 52), "gap": (10, 26, 14)},
        style=Style(luma=56, std=10, grain_max=9), periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_hedge(ctx):
    """Leaf litter and dark soil under the raised hedge decor."""
    p = RECIPES["hedge"].palette
    common, common_grit, common_leaves = hedge_fields(ctx.shared_rng, ctx.size)
    own, own_grit, own_leaves = hedge_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["gap"]), (0.5, p["dark"]), (1.0, p["mid"])])
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = [[c * (1 + 0.14 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    leaves = blend_fields(common_leaves, own_leaves, ctx.interior_mask)
    rgb = mix(rgb, p["light"], leaves, 0.25)
    return rgb, None


def hedge_fields(rng, size):
    """Periodic litter height, grain and leaf flecks beneath the raised hedge."""
    height = fbm(rng, 8, 3, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    grit = lattice_noise(rng, 48, size=size)
    leaves = [0.0] * (size * size)
    for _ in range(14 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        radius = rng.uniform(2.5, 4.5)
        rng.random()  # Retain soft_blobs' per-disc choice draw.
        stamp_disc(leaves, x, y, radius, softness=0.75)
    return height, grit, leaves


@recipe(name="thicket", group="obstacles", label="Thicket", profile="canopy",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"dark": (40, 54, 26), "mid": (62, 80, 38), "light": (92, 110, 58),
                 "twig": (96, 78, 52), "twig_dark": (52, 42, 30)},
        style=Style(luma=65, std=11, grain_max=10, match=0.7),
        periodic=True, grid=8, blocks=4,
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_thicket(ctx):
    """Shared undergrowth and twig fields beneath independently raised decor."""
    p = RECIPES["thicket"].palette
    common, common_grain, common_twigs = thicket_fields(ctx.shared_rng, ctx.size)
    own, own_grain, own_twigs = thicket_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    twigs = blend_fields(common_twigs, own_twigs, ctx.interior_mask)
    return mix(rgb, p["twig_dark"], twigs, 0.35), None


def thicket_fields(rng, size):
    """Periodic soil, grain and faint twig masks at the original native scale."""
    height = fbm(rng, 7, 3, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    fine = lattice_noise(rng, 48, size=size)
    twigs = [0.0] * (size * size)
    for _ in range(rng.randint(3, 5) * (size // N) ** 2):
        length = 14 + rng.random() * 12
        x, y = rng.random() * size, rng.random() * size
        angle = rng.random() * TAU
        stamp_stroke(twigs, x - math.cos(angle) * length / 2, y - math.sin(angle) * length / 2,
                     angle, length, 2.5)
    return height, fine, twigs


@recipe(name="lava", group="lava", label="Lava", profile="fractured",
        seam={"height": 4, "fringe": [214, 110, 40], "fringe_q8": 128, "fringe_width_q8": 640},
        palette={"crust_dark": (36, 18, 16), "crust": (50, 24, 20), "crust_warm": (96, 42, 26),
                 "glow": (170, 70, 24), "glow_bright": (220, 150, 60)},
        style=Style(luma=58, std=17, grain_max=13, match=0.8), phases=4, animation_ticks=8,
        periodic=True, grid=8,
        preview=(168, 72, 30), minimap=(176, 68, 28),
        note="placeholder until the image-generated lava lands")
def render_lava(ctx):
    """Whole common boundary fractures with independently seeded interior crust."""
    p = RECIPES["lava"].palette
    f1, f2, ids, common1, common2, _ = block_worley(ctx, 4)
    common_wx = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    common_wy = fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    own_wx = fbm(ctx.rng, 4, 2, size=ctx.size)
    own_wy = fbm(ctx.rng, 4, 2, size=ctx.size)
    common_edge = domain_warp([b - a for a, b in zip(common1, common2)], common_wx, common_wy, 10)
    own_edge = domain_warp([b - a for a, b in zip(f1, f2)], own_wx, own_wy, 10)
    edge = blend_fields(common_edge, own_edge, ctx.interior_mask)
    common_crust = fbm(ctx.shared_rng, 6, 3, size=ctx.size)
    own_crust = fbm(ctx.rng, 6, 3, size=ctx.size)
    crust_noise = normalize_like(blend_fields(common_crust, own_crust, ctx.interior_mask), common_crust)
    rgb = ramp(crust_noise, [(0.0, p["crust_dark"]), (1.0, p["crust"])])
    rgb = shade(rgb, [1 - v for v in f1], 0.2, reference_height=[1 - v for v in common1])
    common_grit = lattice_noise(ctx.shared_rng, 48, size=ctx.size)
    own_grit = lattice_noise(ctx.rng, 48, size=ctx.size)
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = [[c * (1 + 0.10 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    channel = band(edge, 0.03, 0.045)
    warm = band(edge, 0.08, 0.10)
    rgb = mix(rgb, p["crust_warm"], warm, 0.35)
    # IDs on the common boundary refer to the same crust plates in every layout.
    offsets = [ctx.shared_rng.random() for _ in range(4 * ctx.area_scale)]
    time = (ctx.phase % ctx.phases) / ctx.phases
    pulse = [0.5 + 0.5 * math.sin(TAU * (time + offsets[i])) for i in ids]
    glow = [[a + (b - a) * q for q in pulse] for a, b in zip(p["glow"], p["glow_bright"])]
    return mix(rgb, glow, channel, 0.9), None


@recipe(name="ember_field", group="lava", label="Ember field", profile="fractured",
        seam={"height": 4, "fringe": [160, 80, 40], "fringe_q8": 96, "fringe_width_q8": 512},
        palette={"dark": (50, 28, 24), "mid": (60, 34, 28), "light": (80, 44, 32),
                 "ember_dim": (120, 50, 30), "ember": (230, 120, 40), "ember_core": (250, 200, 80)},
        style=Style(luma=48, std=10, grain_max=9, match=0.7), phases=4, animation_ticks=8,
        periodic=True, grid=8,
        preview=(110, 52, 36), minimap=(120, 48, 30),
        note="placeholder until the image-generated ember field lands")
def render_ember_field(ctx):
    """Common boundary fractures and whole embers, varied interior crust."""
    p = RECIPES["ember_field"].palette
    common, common_grain = ember_field_soil(ctx.shared_rng, ctx.size)
    own, own_grain = ember_field_soil(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.12 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    # Cracks carrying a dim steady glow, sharing lava's DNA.
    f1, f2, _, _, _, _ = block_worley(ctx, 16)
    common_x, common_y = fbm(ctx.shared_rng, 4, 2, size=ctx.size), fbm(ctx.shared_rng, 4, 2, size=ctx.size)
    own_x, own_y = fbm(ctx.rng, 4, 2, size=ctx.size), fbm(ctx.rng, 4, 2, size=ctx.size)
    edge = domain_warp([b - a for a, b in zip(f1, f2)],
                       blend_fields(common_x, own_x, ctx.interior_mask),
                       blend_fields(common_y, own_y, ctx.interior_mask), 6)
    cracks = band(edge, 0.03, 0.04)
    rgb = mix(rgb, p["ember_dim"], cracks, 0.35)
    # Ten embers per native tile matches the former eight-to-twelve average.
    # Perimeter embers are whole shared objects rather than faded RGB images.
    shared, own = block_object_field(ctx, 10, (3.5, 5.0), spacing=0.85)
    glow = [0.0] * (ctx.size * ctx.size)
    core = [0.0] * (ctx.size * ctx.size)
    for cx, cy, r, offset in shared + own:
        intensity = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(TAU * (ctx.t % 1.0 + offset)))
        stamp_disc(glow, cx, cy, r * 2.6, intensity * 0.7, 0.9)
        stamp_disc(core, cx, cy, r, intensity, 0.6)
    rgb = mix(rgb, p["ember_dim"], glow, 1.0)
    ember_color = [
        [a + (b - a) * c for c in core] for a, b in zip(p["ember"], p["ember_core"])
    ]
    rgb = mix(rgb, ember_color, core, 1.0)
    return rgb, None


def ember_field_soil(rng, size):
    """Periodic crust height and fine grain, independent of animation time."""
    height = fbm(rng, 5, 4, size=size)
    height = domain_warp(height, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    return height, lattice_noise(rng, 48, size=size)


# Legacy surfaces keep their original palette/style statistics.

@recipe(name='grass', group="legacy", label='Grass', profile='sand',
        seam={'height': 2, 'cast_q8': 56, 'cast_width_q8': 512}, palette={'dark': (28, 104, 28), 'mid': (30, 113, 31), 'light': (33, 124, 34)}, style=Style(luma=79.3, std=5.0, grain_max=4),
        periodic=True, grid=8, blocks=4, preview=(30, 113, 30), minimap=(0, 90, 0))
def render_grass(ctx):
    """Quiet green turf with common edge tufts and varied interior growth."""
    p = RECIPES["grass"].palette
    common, common_grain = grass_fields(ctx.shared_rng, ctx.size)
    own, own_grain = grass_fields(ctx.rng, ctx.size)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.05 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    return rgb, None


def grass_fields(rng, size):
    """Small tufts over broad growth patches, using only measured color targets."""
    growth = fbm(rng, 8, 3, size=size)
    growth = domain_warp(growth, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 6)
    blades = fbm(rng, 48, 2, cells_y=24, size=size)
    height = [0.82 * a + 0.18 * b for a, b in zip(growth, blades)]
    return height, lattice_noise(rng, 48, size=size)


SAND_GRAINS, SAND_DUNES, SAND_BEVEL = 160, 0.25, 0.15


@recipe(name='sand', group="legacy", label='Sand', profile='sand',
        seam={'height': 1}, palette={'dark': (141, 130, 33), 'mid': (183, 169, 47), 'light': (225, 209, 67)}, style=Style(luma=160.1, std=27.0, grain_max=36.3),
        periodic=True, grid=8, blocks=4, preview=(182, 168, 48), minimap=(170, 170, 0))
def render_sand(ctx):
    """Warm granular sand with common edge dunes and independent interiors."""
    p = RECIPES["sand"].palette
    rgb, flecks = sand_look(ctx, p)
    # The tiny brown mineral flecks remain part of the scalar surface, with
    # no plants or large deposits introduced into the legacy sand identity.
    return mix(rgb, p["dark"], flecks, 0.18), None


def sand_fields(rng, size):
    """Dunes, native-pixel grains and sparse sub-pixel mineral flecks."""
    dunes = fbm(rng, 3, 3, size=size)
    dunes = domain_warp(dunes, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 8)
    grains = lattice_noise(rng, 32, size=size)
    speckle = lattice_noise(rng, 64, size=size)
    height = [0.24 * a + 0.66 * b + 0.10 * c for a, b, c in zip(dunes, grains, speckle)]
    flecks = [0.0] * (size * size)
    for _ in range(5 * (size // N) ** 2):
        x, y = rng.random() * size, rng.random() * size
        stamp_disc(flecks, x, y, rng.uniform(1.4, 2.4), softness=0.7)
    return height, flecks


@recipe(name='road', group="legacy", label='Road', profile='cobblestone',
        seam={'height': 3, 'cast_q8': 48, 'cast_width_q8': 512}, palette={'dark': (95, 84, 71), 'mid': (135, 122, 103), 'light': (165, 149, 128)}, style=Style(luma=121.2, std=23.2, grain_max=22.9),
        periodic=True, grid=8, blocks=4, preview=(133, 120, 101), minimap=(176, 138, 98))
def render_road(ctx):
    """Warm cobbled paving: whole edge stones and varied interior cobbles.

    The small irregular stones retain a shared bevel/mortar network near the
    block perimeter. Only interior point positions and scalar stone texture
    vary; common lighting and tone references keep every edge interchangeable.
    """
    p = RECIPES["road"].palette
    first, second, ids, common_first, common_second, _ = block_worley(ctx, 36)
    edge = [b - a for a, b in zip(first, second)]
    common_edge = [b - a for a, b in zip(common_first, common_second)]
    common_texture = fbm(ctx.shared_rng, 8, 3, size=ctx.size)
    own_texture = fbm(ctx.rng, 8, 3, size=ctx.size)
    texture = normalize_like(blend_fields(common_texture, own_texture, ctx.interior_mask),
                             common_texture, 0.03, 0.97)
    rgb = ramp(texture, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, 36 * ctx.area_scale, 0.12))
    height = [smoothstep(0.025, 0.16, v) for v in edge]
    common_height = [smoothstep(0.025, 0.16, v) for v in common_edge]
    rgb = shade(rgb, height, 0.24, reference_height=common_height)
    mortar = [1 - smoothstep(0.015, 0.075, v) for v in edge]
    rgb = mix(rgb, tuple(c * 0.62 for c in p["dark"]), mortar, 0.9)
    common_grit = lattice_noise(ctx.shared_rng, 48, size=ctx.size)
    own_grit = lattice_noise(ctx.rng, 48, size=ctx.size)
    grit = blend_fields(common_grit, own_grit, ctx.interior_mask)
    rgb = [[c * (1 + 0.09 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    return rgb, None


@recipe(name='ice', group="legacy", label='Ice', profile='frozen',
        seam={'height': 4, 'cast_q8': 64, 'cast_width_q8': 640, 'fringe': [228, 240, 248], 'fringe_q8': 96, 'fringe_width_q8': 384}, palette={'dark': (186, 214, 231), 'mid': (195, 221, 235), 'light': (208, 229, 241)}, style=Style(luma=215.4, std=6.2, grain_max=5.6),
        periodic=True, grid=8, blocks=4, preview=(190, 225, 240), minimap=(190, 225, 240))
def render_ice(ctx):
    """Quiet blue frozen ground, with common whorls and interior frost grain."""
    p = RECIPES["ice"].palette
    common, common_frost, common_grain = ice_fields(ctx.shared_rng, ctx.size)
    own, own_frost, own_grain = ice_fields(ctx.rng, ctx.size)
    common_height = normalize_like(common, common, 0.02, 0.98)
    height = normalize_like(blend_fields(common, own, ctx.interior_mask), common, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, height, 0.035, light=(-0.6, -0.8), reference_height=common_height)
    frost = normalize_like(blend_fields(common_frost, own_frost, ctx.interior_mask), common_frost)
    rgb = mix(rgb, p["light"], frost, 0.10)
    fine = blend_fields(common_grain, own_grain, ctx.interior_mask)
    rgb = [[c * (1 + 0.025 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]
    return rgb, None


def ice_fields(rng, size):
    """Periodic glassy whorls, soft crystalline frost and fine cold grain."""
    height = fbm(rng, 3, 3, cells_y=6, size=size)
    height = domain_warp(height, fbm(rng, 2, 2, size=size), fbm(rng, 2, 2, size=size), 10, 6)
    frost = [abs(2 * v - 1) for v in fbm(rng, 12, 2, size=size)]
    fine = lattice_noise(rng, 48, size=size)
    return height, frost, fine


assert set(SYNTH_ORDER) == set(RECIPES), "every built-in needs a recipe and vice versa"

# Pair treatments (B5): boundary families chosen for specific contacts.
# The grass and sand looks take the same edges as classic grass and sand.
GRASS_LOOKS = ("grass", "lush_grass", "dry_grass")
SAND_LOOKS = ("sand", "dune_sand", "pale_sand")
PAIR_TREATMENTS = (
    [{"a": name, "b": look, "profile": "rock"} for name in ("boulders", "ridge_rock", "outcrop") for look in GRASS_LOOKS]
    + [{"a": name, "b": look, "profile": "brush"} for name in ("hedge", "thicket") for look in GRASS_LOOKS]
    + [
        {"a": "water", "b": "deep_water", "profile": "soft"},
        {"a": "deep_water", "b": "dark_water", "profile": "soft"},
    ]
    # Low-roughness waterlines that the shoreline surf can wash up.
    + [{"a": name, "b": look, "profile": "beach"} for name in ("water", "deep_water", "dark_water")
       for look in SAND_LOOKS]
    + [
        {"a": name, "b": ground_name, "profile": "fractured"}
        for name in ("lava", "ember_field")
        for ground_name in GRASS_LOOKS + SAND_LOOKS + ("dirt", "gravel")
    ]
    + [
        {"a": name, "b": other, "profile": "crisp"}
        for name in ("void_hole", "chasm")
        for other in LEGACY_NAMES + [n for n in BUILTIN_ORDER if n not in ("void_hole", "chasm")]
    ]
)


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------


# Periodic materials take variant 0's pixels in the outer band: fully within
# PERIODIC_BAND render px of the edge, fading to the variant over PERIODIC_FADE.
PERIODIC_BAND = 4
PERIODIC_FADE = 6


def render_fields(name, variant, phase):
    rec = RECIPES[name]
    ctx = Ctx(name, variant, phase, rec.phases)
    rgb, alpha = rec.render(ctx)
    if rec.periodic:
        return rgb, alpha
    return neutral_band(rgb, alpha)


def force_periodic_band(renders):
    """Give every render variant 0's outer band so all variants share edges."""
    weight = band_weight(PERIODIC_BAND, PERIODIC_FADE)
    master_rgb, master_alpha = renders[0]
    out = [renders[0]]
    for rgb, alpha in renders[1:]:
        rgb = [[c + (m - c) * w for c, m, w in zip(channel, master, weight)]
               for channel, master in zip(rgb, master_rgb)]
        if alpha is not None:
            alpha = [a + (m - a) * w for a, m, w in zip(alpha, master_alpha, weight)]
        out.append((rgb, alpha))
    return out


def perimeter_structure(rgb, depth=16):
    """Luma spread of the outer `depth` render pixels: how much a perimeter
    carries that the runtime would repeat on every tile."""
    values = [
        luma(r, g, b)
        for r, g, b, x, y in zip(*rgb, XS, YS)
        if min(x, y, N - 1 - x, N - 1 - y) < depth
    ]
    mean = sum(values) / len(values)
    return math.sqrt(sum((v - mean) ** 2 for v in values) / len(values))


def render_phase(name, phase):
    """Render every layout's tiles for one phase at native and HD resolution.

    Grid blocks are sliced after rendering continuous fields. One style
    transform pools every layout so common boundaries keep identical colors.
    Nonperiodic fallback sources still choose the quietest perimeter as their
    master. Both resolutions come from the same floating-point fields.
    """
    rec = RECIPES[name]
    if rec.grid:
        size = rec.grid * N
        blocks = [rec.render(Ctx(name, layout, phase, rec.phases, size=size))
                  for layout in range(rec.blocks)]

        def cell(values, variant):
            gx, gy = variant % rec.grid, variant // rec.grid
            return [values[y * size + x]
                    for y in range(gy * N, (gy + 1) * N)
                    for x in range(gx * N, (gx + 1) * N)]

        renders = [([cell(channel, v) for channel in rgb], None if alpha is None else cell(alpha, v))
                   for rgb, alpha in blocks for v in range(rec.cells)]
    else:
        renders = [render_fields(name, variant, phase) for variant in range(VARIANTS)]
    if rec.periodic:
        order = list(range(len(renders)))
    elif rec.phases == 1:
        order = sorted(range(VARIANTS), key=lambda v: (perimeter_structure(renders[v][0]), v))
    else:
        # Animated phases must keep one variant order; phase 0 decides it.
        order = sorted(range(VARIANTS), key=lambda v: (perimeter_structure(render_fields(name, v, 0)[0]), v))
    renders = [renders[v] for v in order]
    if rec.periodic and not rec.grid:
        renders = force_periodic_band(renders)
    small = [box_down_fields(rgb, alpha) for rgb, alpha in renders]
    pooled = [sum((rgb[k] for rgb, _ in small), []) for k in range(3)]
    transform = style_transform(pooled, rec.style.luma, rec.style.std, rec.style.match)
    native, hd = [], []
    for (rgb, alpha), (small_rgb, small_alpha) in zip(renders, small):
        hd.append(fields_to_image(apply_style(rgb, transform), alpha, N))
        native.append(fields_to_image(apply_style(small_rgb, transform),
                                      small_alpha if alpha is not None else None, TILE))
    return native, hd


def _render_job(args):
    name, phase = args
    native, hd = render_phase(name, phase)
    return (name, phase, [t.tobytes() for t in native], native[0].size,
            [t.tobytes() for t in hd], hd[0].size)


def synthesize_both(names, jobs=None):
    """Render and perimeter-share all variants at both resolutions:
    ({name: {phase: [native tiles]}}, {name: {phase: [HD tiles]}})."""
    tasks = [(name, phase) for name in names for phase in range(RECIPES[name].phases)]
    # Each 1024px seeded block keeps several scalar and RGB fields alive.
    # Bound automatic concurrency on high-core CI/development machines;
    # contributors can select more workers explicitly when memory permits.
    jobs = jobs or min(2, os.cpu_count() or 1)
    if jobs > 1 and len(tasks) > 1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(tasks))) as pool:
            rendered = list(pool.map(_render_job, tasks))
    else:
        rendered = [_render_job(task) for task in tasks]
    native, hd = {}, {}
    for name, phase, small, small_size, large, large_size in rendered:
        tiles = [Image.frombytes("RGBA", small_size, d) for d in small]
        periodic = RECIPES[name].periodic
        native.setdefault(name, {})[phase] = tiles if periodic else share_perimeter(tiles, 2)
        tiles = [Image.frombytes("RGBA", large_size, d) for d in large]
        hd.setdefault(name, {})[phase] = tiles if periodic else share_perimeter(tiles, 2 * (N // TILE))
    return native, hd


def synthesize(names, jobs=None, hd=False):
    """Render and perimeter-share all variants: {name: {phase: [tiles]}}."""
    return synthesize_both(names, jobs)[1 if hd else 0]


def frames_of(name, phases):
    """Flatten phases; each contains block-major, row-major cell tiles."""
    return [tile for phase in sorted(phases) for tile in phases[phase]]


def preview_colors(name, phases):
    rec = RECIPES[name]
    mean = mean_color(phases[0])
    preview = tuple(rec.preview) if rec.preview else mean
    minimap = tuple(rec.minimap) if rec.minimap else preview
    return [int(c) for c in preview], [int(c) for c in minimap], mean


# --------------------------------------------------------------------------
# Catalog and provenance
# --------------------------------------------------------------------------


def material_block(name, phases):
    rec = RECIPES[name]
    preview, minimap, _ = preview_colors(name, phases)
    block = {
        "key": name,
        "sprite": f"{FRAME_PREFIX}{name}",
        "profile": rec.profile,
        "preview": list(preview),
        "variants": [{"frame": i, "weight": 1} for i in range(rec.cells * rec.blocks)],
    }
    if rec.phases > 1:
        block["animation_frames"] = rec.phases
        block["animation_stride"] = rec.cells * rec.blocks
        block["animation_ticks"] = rec.animation_ticks
    if rec.periodic:
        block["edges"] = "periodic"
    if rec.grid:
        block["variant_grid"] = rec.grid
    if rec.blocks > 1:
        block["variant_blocks"] = rec.blocks
    if rec.block_group:
        block["variant_block_group"] = rec.block_group
    block["minimap"] = list(minimap)
    block["seam"] = dict(rec.seam)
    return block


def catalog_fragment(results):
    names = [n for n in SYNTH_ORDER if n in results]
    return {
        "materials": [material_block(name, results[name]) for name in names],
        "bindings": {name: name for name in names},
        "pair_treatments": [p for p in PAIR_TREATMENTS if p["a"] in results or p["b"] in results],
    }


def merge_catalog(document, fragment):
    """Replace or append the fragment's materials, bindings and pair treatments.

    Everything else in the catalog (version, profiles, warp, existing
    materials) is left untouched, so re-running after a recipe change only
    moves the blocks that terrain_synth.py owns.
    """
    by_key = {m["key"]: i for i, m in enumerate(document["materials"])}
    for block in fragment["materials"]:
        if block["key"] in by_key:
            previous = document["materials"][by_key[block["key"]]]
            # Raised decor and shoreline surf are authored elsewhere; refreshing
            # ground must retain them.
            block = dict(block, **{k: previous[k] for k in ("decor", "surf") if k in previous})
            document["materials"][by_key[block["key"]]] = block
        else:
            document["materials"].append(block)
    document["bindings"].update(fragment["bindings"])
    treatments = document.setdefault("pair_treatments", [])
    existing = {tuple(sorted((p["a"], p["b"]))) for p in treatments}
    for pair in fragment["pair_treatments"]:
        key = tuple(sorted((pair["a"], pair["b"])))
        if key not in existing:
            treatments.append(pair)
            existing.add(key)
    return document


def write_catalog(results, path):
    from terrain_profile_curves import dump_catalog  # noqa: E402

    document = json.loads(path.read_text())
    merge_catalog(document, catalog_fragment(results))
    path.write_text(dump_catalog(document))


def presentation_initialisers(results):
    lines = ["// TerrainPresentations colours: {minimap}, {overview}; derived by terrain_synth.py"]
    for name in BUILTIN_ORDER:
        if name not in results:
            continue
        preview, minimap, _ = preview_colors(name, results[name])
        lines.append(
            f"/* {name:<14} */ {{{{{minimap[0]:3d}, {minimap[1]:3d}, {minimap[2]:3d}}}, "
            f"{{{preview[0]:3d}, {preview[1]:3d}, {preview[2]:3d}}}}},"
        )
    return "\n".join(lines)


def generator_hashes():
    here = Path(__file__).resolve().parent
    return {
        f"tools/artwork/{p.name}": hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (here / "terrain_synth.py", here / "material_tiles.py")
    }


def platform_record():
    """Where the frames were produced; libm and rounding can differ elsewhere."""
    libc = platform.libc_ver()
    return {
        "system": sys.platform,
        "machine": platform.machine(),
        "libc": " ".join(part for part in libc if part) or None,
    }


def provenance_document(name, phases, hashes, root, hd_hashes=None):
    rec = RECIPES[name]
    preview, minimap, mean = preview_colors(name, phases)
    references = reference_stats(root)
    return {
        "generator": "tools/artwork/terrain_synth.py",
        "method": "procedural",
        "generator_sha256": generator_hashes(),
        "material": name,
        "group": rec.group,
        "label": rec.label,
        "recipe": rec.describe(),
        "seed": f'fnv1a32("{name}:<variant>:<phase>")',
        "render_size": N,
        "block_render_size": N * rec.grid if rec.grid else N,
        "tile_size": TILE,
        "variants": rec.cells * rec.blocks,
        "phases": rec.phases,
        "frame_layout": f"cell + {rec.cells} * block + {rec.cells * rec.blocks} * phase",
        "sprite": f"{FRAME_PREFIX}{name}",
        "preview": list(preview),
        "minimap": list(minimap),
        "mean_color": list(mean),
        "style_stats": {str(phase): style_stats(tiles) for phase, tiles in sorted(phases.items())},
        "style_references": {
            f"{prefix}{first}..{first + 15}.png": {"use": "statistics only", "stats": references[key]}
            for key, (prefix, first) in STYLE_REFERENCES.items()
        },
        "python": platform.python_version(),
        "pillow": PIL.__version__,
        "platform": platform_record(),
        "runtime_sha256": hashes,
        "highres_sha256": hd_hashes or {},
    }


def write_material(name, phases, hd_phases, root):
    from highres_pack import register_frames, source_record

    frames = frames_of(name, phases)
    hashes = write_frames(frames, f"{FRAME_PREFIX}{name}", 0, root)
    hd_frames = frames_of(name, hd_phases)
    generators = [source_record(Path(path), root) for path in generator_hashes()]
    register_frames([
        {"id": f"terrain-{name}{i}", "image": tile, "recipe": f"{HD_RECIPE}: {name}", "sources": generators}
        for i, tile in enumerate(hd_frames)
    ], HD_CATEGORY, root)
    hd_hashes = {f"{HD_PREFIX}{name}{i}.png": pixel_sha256(tile) for i, tile in enumerate(hd_frames)}
    document = provenance_document(name, phases, hashes, root, hd_hashes)
    path = root / "content-src/gfx" / name / "provenance.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n")
    return hashes


PLATFORM_NOTE = (
    "pixel-exact reproduction is pinned to the encoder interpreter on Linux x86-64; "
    "libm (sin/atan2/hypot) and round() boundaries can differ elsewhere"
)


def check_material(name, phases, root, hd_phases=None):
    """Compare a fresh synthesis with committed frames and provenance."""
    rec = RECIPES[name]
    problems = []
    frames = frames_of(name, phases)
    fresh = tile_hashes(frames)
    provenance_path = root / "content-src/gfx" / name / "provenance.json"
    if not provenance_path.exists():
        return [f"{name}: missing {provenance_path.relative_to(root)}"]
    provenance = json.loads(provenance_path.read_text())
    if provenance.get("method") != "procedural":
        return [f"{name}: provenance method is {provenance.get('method')!r}; mark the recipe placeholder_only"]
    recorded = provenance.get("runtime_sha256", {})
    expected_names = [f"{FRAME_PREFIX}{name}{i}.png" for i in range(rec.cells * rec.blocks * rec.phases)]
    if list(recorded) != expected_names:
        problems.append(f"{name}: provenance covers {len(recorded)} frames, expected {len(expected_names)}")
    here = platform_record()
    recorded_platform = provenance.get("platform")
    platform_hint = "" if recorded_platform == here else (
        f" (recorded on {recorded_platform}, checking on {here}; {PLATFORM_NOTE})"
    )
    for i, relative in enumerate(expected_names):
        path = root / relative
        if not path.exists():
            problems.append(f"{relative}: missing")
            continue
        with Image.open(path) as image:
            committed = pixel_sha256(image)
        if committed != fresh[i]:
            problems.append(f"{relative}: committed pixels differ from a fresh synthesis{platform_hint}")
        if recorded.get(relative) != committed:
            problems.append(f"{relative}: provenance hash differs from committed pixels")
    if hd_phases is not None:
        recorded_hd = provenance.get("highres_sha256", {})
        for i, tile in enumerate(frames_of(name, hd_phases)):
            relative = f"{HD_PREFIX}{name}{i}.png"
            path = root / relative
            if not path.exists():
                problems.append(f"{relative}: missing HD frame")
                continue
            with Image.open(path) as image:
                committed = pixel_sha256(image)
            if committed != pixel_sha256(tile):
                problems.append(f"{relative}: committed HD pixels differ from a fresh synthesis{platform_hint}")
            if recorded_hd.get(relative) != committed:
                problems.append(f"{relative}: provenance HD hash differs from committed pixels")
    if provenance.get("generator_sha256") != generator_hashes():
        problems.append(f"{name}: generator sha256 differs; re-run terrain_synth.py to refresh provenance")
    if provenance.get("pillow") != PIL.__version__:
        problems.append(f"{name}: provenance Pillow {provenance.get('pillow')} != {PIL.__version__}")
    return problems


# --------------------------------------------------------------------------
# Contact sheet
# --------------------------------------------------------------------------


def _font(size):
    try:
        return ImageFont.load_default(size=size)
    except (TypeError, OSError):
        return ImageFont.load_default()


def contact_sheet(results, root, columns=2):
    """Every material: name/stats, 16 variants, and runtime-neighbour fields.

    Tiles are shown after the runtime's own four-pixel border blend
    (`runtime_blend`, the same arithmetic as `seamless_sources` in
    tools/terrain_tileset.py), so joins and the 3x3 fields look as they do in
    the game. Translucent materials sit over the ocean backdrop's mean colour.
    """
    scale = 4
    blocks = []
    legacy = {key: reference_tiles(key, root) for key in STYLE_REFERENCES}
    entries = [(name, results[name][0], RECIPES[name].group, True) for name in SYNTH_ORDER if name in results]
    entries += [(name, tiles, "legacy", False) for name, tiles in legacy.items()]
    font = _font(13)
    small = _font(11)
    grid_w = 8 * TILE * scale
    join_w = 2 * TILE * scale
    field_w = 3 * TILE * 2
    block_w = grid_w + 16 + join_w + 16 + field_w + 16
    max_variants = max(len(tiles) for _, tiles, _, _ in entries)
    strip_h = ((max_variants + VARIANTS - 1) // VARIANTS) * TILE
    panel_h = ((max_variants + 7) // 8) * TILE * scale
    block_h = 20 + 16 + strip_h + 8 + max(panel_h, 3 * TILE + 8 + 6 * TILE) + 8
    for name, tiles, group, generated in entries:
        if not (generated and RECIPES[name].periodic):
            tiles = runtime_blend(tiles)
        block = Image.new("RGBA", (block_w, block_h), (40, 40, 44, 255))
        draw = ImageDraw.Draw(block)
        stats = style_stats(tiles)
        title = f"{name}  [{group}]" if generated else f"{name}  [native reference]"
        draw.text((4, 2), title, font=font, fill=(240, 240, 240, 255))
        draw.text(
            (4, 18),
            f"luma {stats['luma']}  std {stats['std']}  grain {stats['grain']}  sat {stats['sat']}  alpha {stats['alpha']}",
            font=small, fill=(200, 200, 200, 255),
        )
        y = 36
        block.alpha_composite(tiles_to_sheet(tiles, columns=VARIANTS), (4, y))
        y += strip_h + 8
        block.alpha_composite(tiles_to_sheet(tiles, columns=8, scale=scale), (4, y))
        rng = random.Random(fnv1a32(name + ":join"))
        grid = RECIPES[name].grid if generated else 0

        def field_tiles(width):
            if grid:
                # Include the block's wrap join, with the same positional rule as runtime.
                count = RECIPES[name].blocks
                return [tiles[(fnv1a32(f"{name}:preview:{(x + grid - 1) // grid}:{(y + grid - 1) // grid}") % count)
                              * grid * grid + ((y + grid - 1) % grid) * grid + (x + grid - 1) % grid]
                        for y in range(width) for x in range(width)]
            return [tiles[rng.randrange(len(tiles))] for _ in range(width * width)]

        picks = field_tiles(2)
        block.alpha_composite(tiles_to_sheet(picks, columns=2, scale=scale), (4 + grid_w + 16, y))
        # A field at 1x and 2x using actual runtime neighbours.
        field = field_tiles(3)
        fx = 4 + grid_w + 16 + join_w + 16
        block.alpha_composite(tiles_to_sheet(field, columns=3), (fx, y))
        block.alpha_composite(tiles_to_sheet(field, columns=3, scale=2), (fx, y + 3 * TILE + 8))
        if stats["alpha"] < 250:
            # Translucent materials are shown over the ocean backdrop's mean colour.
            ocean = Image.new("RGBA", block.size, (69, 52, 200, 255))
            ocean.alpha_composite(block)
            block = ocean
        blocks.append(block)
    rows = (len(blocks) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * block_w, rows * block_h), (20, 20, 22, 255))
    for i, block in enumerate(blocks):
        sheet.paste(block, ((i % columns) * block_w, (i // columns) * block_h))
    return sheet


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def require_pillow():
    if PIL.__version__ != PILLOW_VERSION:
        raise SystemExit(
            f"terrain_synth.py needs Pillow {PILLOW_VERSION} (resampling kernels are a determinism input); "
            f"found {PIL.__version__}. Use \"$(python3 tools/package_assets.py --encoder-python)\"."
        )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--material", action="append", help="material name (repeatable; default all)")
    parser.add_argument("--check", action="store_true", help="re-synthesise and compare with committed frames")
    parser.add_argument("--contact-sheet", type=Path, help="write a review sheet PNG (no frames written)")
    parser.add_argument("--emit-catalog", action="store_true", help="print catalog blocks and colour initialisers")
    parser.add_argument("--write-catalog", action="store_true",
                        help="merge the material blocks, bindings and pair treatments into data/terrain/tileset.json")
    parser.add_argument("--hd", action="store_true", help="write 128x128 renders under artifacts/terrain/hd/ (no frames written)")
    parser.add_argument("--write", action="store_true", help="also write frames when another mode is selected")
    parser.add_argument("--jobs", type=int, default=0, help="parallel render processes (default: up to 2, bounded for 1024px block memory)")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to read and write")
    args = parser.parse_args(argv)
    require_pillow()
    names = args.material or list(SYNTH_ORDER)
    unknown = [n for n in names if n not in RECIPES]
    if unknown:
        parser.error(f"unknown material(s): {', '.join(unknown)}; known: {', '.join(SYNTH_ORDER)}")
    root = args.root.resolve()
    jobs = args.jobs or None

    if args.hd:
        hd = synthesize(names, jobs, hd=True)
        out = root / "artifacts/terrain/hd"
        out.mkdir(parents=True, exist_ok=True)
        for name, phases in hd.items():
            for i, tile in enumerate(frames_of(name, phases)):
                tile.save(out / f"terrain-{name}{i}.png")
        print(f"Wrote HD renders for {len(hd)} materials to {out}")

    if args.check:
        checked = [n for n in names if not RECIPES[n].placeholder_only]
        results, hd_results = synthesize_both(checked, jobs)
        problems = [p for name in checked for p in check_material(name, results[name], root, hd_results[name])]
        skipped = [n for n in names if RECIPES[n].placeholder_only]
        for problem in problems:
            print("FAIL", problem)
        if skipped:
            print("skipped placeholder-only recipes:", ", ".join(skipped))
        if problems:
            print(f"NOTE {PLATFORM_NOTE}")
            raise SystemExit(1)
        print(f"PASS {len(checked)} procedural materials match their committed frames and provenance")
        return

    # Review and catalog modes do not write frames unless --write is given;
    # --hd alone is complete after the renders above.
    modes = args.contact_sheet is not None or args.emit_catalog or args.write_catalog or args.hd
    needs_native = args.contact_sheet is not None or args.emit_catalog or args.write_catalog or not modes or args.write
    if not needs_native:
        return
    results, hd_results = synthesize_both(names, jobs)
    if args.contact_sheet is not None:
        args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        contact_sheet(results, root).save(args.contact_sheet)
        print(f"Wrote contact sheet {args.contact_sheet}")
    if args.emit_catalog:
        print(json.dumps(catalog_fragment(results), indent=2))
        print(presentation_initialisers(results))
    if args.write_catalog:
        write_catalog(results, root / "data/terrain/tileset.json")
        print(f"Merged {len(results)} material blocks into data/terrain/tileset.json")
    if not modes or args.write:
        for name in names:
            hashes = write_material(name, results[name], hd_results[name], root)
            stats = style_stats(results[name][0])
            print(f"{name:<14} {len(hashes):3d} frames  luma {stats['luma']:5.1f} std {stats['std']:4.1f} "
                  f"grain {stats['grain']:4.1f} sat {stats['sat']:.2f}")
        print(f"Wrote {len(names)} materials under {root / 'data/gfx'} and {root / 'content-src/gfx'}")


if __name__ == "__main__":
    main()
