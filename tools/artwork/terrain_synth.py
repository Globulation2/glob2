#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise original procedural terrain materials for the terrain catalog.

Usage (with the pinned development dependencies and Pillow 12.2.0):

    uv venv artifacts/asset-tools/venv
    uv pip install --python artifacts/asset-tools/venv/bin/python -r tools/asset-requirements.txt
    ENC=artifacts/asset-tools/venv/bin/python
    "$ENC" tools/artwork/terrain_synth.py                    # write all frames and catalog blocks
    "$ENC" tools/artwork/terrain_synth.py --material mud --contact-sheet artifacts/terrain/mud.png
    "$ENC" tools/artwork/terrain_synth.py --check            # re-synthesise and compare
    "$ENC" tools/artwork/terrain_synth.py --emit-catalog     # print catalog blocks
    "$ENC" tools/artwork/terrain_synth.py --write-catalog    # catalog from committed frames (no render)
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
The interchangeable blocks preserve shared edge geometry while varying their
interiors. Frames are phase-major, block-major, then
row-major cell: `cell + cells * block + (cells * blocks) * phase`, where
`cells = grid * grid`. Animated materials
render four phases (sixteen for water and deep water).
Outputs are `content/data/gfx/terrain-<name>N.png`, the 128x128 HD frames the classic
tiles were downsampled from (`content/data/highres/v1/terrain-<name>N.png`, registered in
the pack through tools/artwork/highres_pack.py) and `content-src/gfx/<name>/provenance.json`.

Pixel-exact reproduction (`--check`) is pinned to the encoder interpreter on
Linux x86-64: besides Pillow's resampling kernels, the renders depend on the C
library's sin/atan2/hypot and on round() boundaries, so another platform may
differ in a few low bits. The provenance records the platform.

Python 3, Pillow, NumPy and SciPy (development-only dependencies).
"""
import argparse
import bisect
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

import numpy as np
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
FRAME_PREFIX = "content/data/gfx/terrain-"
# The catalog names sprites by their runtime path, under the data search root.
SPRITE_PREFIX = "data/gfx/terrain-"
HD_PREFIX = "content/data/highres/v1/terrain-"
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


def shifted(values, dx, dy):
    """The field moved by (dx, dy) whole render pixels, wrapped."""
    size = math.isqrt(len(values))
    return [values[((y - dy) % size) * size + (x - dx) % size] for x, y in ((i % size, i // size) for i in range(len(values)))]


def interior_weight(margin, fade=8):
    """0 within `margin` render px of the edge, rising to 1 over `fade`."""
    w = band_weight(margin, margin + fade)
    return [1 - v for v in w]


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


# Group marks: every material in a gameplay group shares one learnable mark,
# drawn by the same helper, while its colour and material stay free per biome.
# Each helper keeps the block contract: boundary geometry from the shared
# stream, varied interiors, and a common reference for any normalisation.


def layout_field(ctx, make):
    """`make(rng)` from the shared stream at the block edge, varied inside."""
    return blend_fields(make(ctx.shared_rng), make(ctx.rng), ctx.interior_mask)


def group_cracks(ctx, cells=7, width=0.065, gaps=0.30, warp=5):
    """Barren mark: broken dry cracks one to two native pixels wide.

    Warped Voronoi edges, gated by broad noise so the network breaks into
    separate fissures rather than closed plates (clay keeps its own plates).
    """
    f1, f2, _, _, _, _ = block_worley(ctx, cells)
    size = ctx.size
    wx = layout_field(ctx, lambda rng: fbm(rng, 5, 2, size=size))
    wy = layout_field(ctx, lambda rng: fbm(rng, 5, 2, size=size))
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, warp)
    gate = layout_field(ctx, lambda rng: fbm(rng, 6, 2, size=size))
    return [(1 - smoothstep(width * 0.4, width, e)) * smoothstep(gaps - 0.06, gaps + 0.06, g)
            for e, g in zip(edge, gate)]


def barren_mark(ctx, rgb, crack_colour, cells=7, strength=1.0, fade=0.22):
    """Faded colour plus shared dry cracks with a lit lower lip."""
    rgb = desaturate(rgb, fade)
    cracks = group_cracks(ctx, cells)
    lip = [max(0.0, b - a) for a, b in zip(cracks, shifted(cracks, 1, 1))]
    rgb = mix(rgb, crack_colour, cracks, strength)
    return [[c * (1 + 0.20 * l) for c, l in zip(channel, lip)] for channel in rgb]


def worn_edge(rgb, lines, light=(1, 1), strength=0.16):
    """Paths mark: a crisp, lighter worn lip beside every straight line.

    `lines` marks grooves (ruts, joints, plank gaps); the lip is the strip
    just past each groove toward the viewer's light."""
    dx, dy = light
    lip = [max(0.0, b - a) for a, b in zip(lines, shifted(lines, dx, dy))]
    return [[c * (1 + strength * l) for c, l in zip(channel, lip)] for channel in rgb]


def lumps(ctx, cells, pits=0.0, metric="euclid", stretch=1.0, rise=0.6, warp=0):
    """Rough mark: heavy lumps, with some cells sunk into wet pits.

    Returns (height, common height, pit mask, cell ids). Height rises from 0
    at every cell border over `rise` (in F2-F1 units: large for rounded
    lumps, small for flat-topped slabs) and is negative in pit cells. Pits are
    chosen per cell id from the shared stream, so boundary pits are the same
    pits in every layout. `warp` rounds soft lumps (render px); the signed
    height is warped, so pit rims stay continuous. Cell ids stay unwarped.
    """
    f1, f2, ids, common1, common2, common_ids = block_worley(ctx, cells, metric, stretch)
    count = cells * ctx.area_scale
    sunk = [1.0 if ctx.shared_rng.random() < pits else 0.0 for _ in range(count)]

    def dome(a, b, i):
        h = smoothstep(0.0, rise, b - a)
        return -h * 0.8 if sunk[i] else h

    height = [dome(a, b, i) for a, b, i in zip(f1, f2, ids)]
    common = [dome(a, b, i) for a, b, i in zip(common1, common2, common_ids)]
    if warp:
        common_x, common_y = fbm(ctx.shared_rng, 4, 2, size=ctx.size), fbm(ctx.shared_rng, 4, 2, size=ctx.size)
        wx = blend_fields(common_x, fbm(ctx.rng, 4, 2, size=ctx.size), ctx.interior_mask)
        wy = blend_fields(common_y, fbm(ctx.rng, 4, 2, size=ctx.size), ctx.interior_mask)
        height = domain_warp(height, wx, wy, warp)
        common = domain_warp(common, common_x, common_y, warp)
    pit = [smoothstep(0.1, 0.4, -h) for h in height]
    return height, common, pit, ids


def plates(ctx, cells, metric="angular", stretch=1.0, step=0.3, rise=0.1):
    """Overlapping slabs: Worley cells raised to their own level.

    Returns (height, common height, cell ids). Each cell is a flat-topped
    slab at a level drawn per cell id from the shared stream, so boundary
    slabs stand at the same level in every layout and neighbours meet at
    steps that `shade` and `cast` pick out, as if one slab lay over the next.
    `rise` (in F2-F1 units) is the bevel width at each slab's rim.
    """
    f1, f2, ids, common1, common2, common_ids = block_worley(ctx, cells, metric, stretch)
    levels = [ctx.shared_rng.random() for _ in range(cells * ctx.area_scale)]

    def slab(a, b, i):
        return step * levels[i] + smoothstep(0.0, rise, b - a)

    height = [slab(a, b, i) for a, b, i in zip(f1, f2, ids)]
    common = [slab(a, b, i) for a, b, i in zip(common1, common2, common_ids)]
    return height, common, ids


def cast(rgb, height, colour, reach=3, strength=0.5):
    """Shadow a higher neighbour casts down-right onto lower ground: wherever
    the height `reach` px up and to the left stands above this pixel, blend
    toward `colour` in proportion to the step."""
    over = shifted(height, reach, reach)
    drop = [clamp01((o - h) * 4) for o, h in zip(over, height)]
    return mix(rgb, colour, drop, strength)


def jag(ctx, fields, common, amount, cells=16):
    """Break straight cell edges into jagged ones. `fields` (lists) take one
    warp that is common at the block edge and varied inside; their `common`
    references take the common warp alone, so every layout lights against
    the same reference."""
    size = ctx.size
    cx, cy = fbm(ctx.shared_rng, cells, 2, size=size), fbm(ctx.shared_rng, cells, 2, size=size)
    wx = blend_fields(cx, fbm(ctx.rng, cells, 2, size=size), ctx.interior_mask)
    wy = blend_fields(cy, fbm(ctx.rng, cells, 2, size=size), ctx.interior_mask)
    return ([domain_warp(f, wx, wy, amount) for f in fields],
            [domain_warp(f, cx, cy, amount) for f in common])


def wet_pits(rgb, height, common, pit, water, sheen, depth=0.75):
    """Rough mark: standing water in the pits, a bright sheen on the lit rim."""
    rgb = mix(rgb, water, pit, depth)
    glint = [max(0.0, -g) * p for g, p in zip(gradient_light(height, reference_height=common), pit)]
    return mix(rgb, sheen, [smoothstep(0.15, 0.6, g) for g in glint], 0.85)


def ridge_mark(ctx, rgb, spacing=64, wobble=0.5, riser=0.14, lip=0.05, lit=0.3, shadow=0.35):
    """Ridges mark: long, roughly level ledges every `spacing` render px.

    Down the screen each ledge top ends in a narrow lit lip, then a short
    shaded riser drops to the next ledge. Ledge k is where
    `y / spacing + wobble * noise` crosses k; only its fractional part is
    used, so the ledges wrap with the block."""
    size = ctx.size
    noise = layout_field(ctx, lambda rng: fbm(rng, 3, 2, cells_y=2, size=size))
    top, fall = 1 - riser - lip, 1 - riser
    scale = []
    for i, n in enumerate(noise):
        f = ((i // size) / spacing + wobble * (n - 0.5)) % 1
        glint = smoothstep(top - 0.02, top + 0.01, f) * (1 - smoothstep(fall - 0.01, fall + 0.01, f))
        face = smoothstep(fall - 0.01, fall + 0.01, f) * (1 - 0.6 * smoothstep(fall, 1.0, f))
        scale.append(1 + lit * glint - shadow * face)
    return [[c * k for c, k in zip(channel, scale)] for channel in rgb]


# Barren ground: muted warm neutrals, inhibits growth like sand but is not a shore.

@recipe(name="dirt", group="barren", label="Dirt", profile="soft",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (104, 82, 56), "mid": (136, 108, 74), "light": (160, 132, 96), "pebble": (86, 68, 48),
                 "crack": (66, 48, 32)},
        style=Style(luma=116, std=10, grain_max=9), periodic=True, grid=8, blocks=4)
def render_dirt(ctx):
    """The original speckled earth and stones, under the barren group's cracks."""
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
    rgb = mix(rgb, p["pebble"], pebbles, 0.8)
    return barren_mark(ctx, rgb, p["crack"], fade=0.0), None


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
        palette={"dark": (150, 76, 46), "mid": (184, 102, 64), "light": (204, 128, 88), "crack": (112, 56, 34)},
        style=Style(luma=124, std=8, grain_max=8), periodic=True, grid=8, blocks=4)
def render_clay(ctx):
    """Red baked clay in bold plates, split again by the barren group's cracks."""
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
    cracks = band(edge, 0.04, 0.07)
    rgb = mix(rgb, p["crack"], cracks, 0.7)
    return barren_mark(ctx, rgb, p["crack"], cells=9, fade=0.1), None


@recipe(name="shale", group="barren", label="Shale", profile="rock",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"fines": (118, 110, 98), "gap": (78, 72, 64), "crack": (64, 58, 52),
                 "chips": [(162, 156, 146), (148, 144, 138), (170, 160, 144), (138, 134, 128),
                           (156, 146, 132), (176, 170, 160)]},
        style=Style(luma=136, std=16, grain_max=16, match=0.8), periodic=True, grid=8, blocks=4)
def render_shale(ctx):
    """Shale: big flat plates of grey stone, each lying at its own level so
    one overlaps the next, with jagged broken edges, under the barren cracks."""
    p = RECIPES["shale"].palette
    size = ctx.size
    height, common, ids = plates(ctx, 3, stretch=1.7, step=0.35, rise=0.05)
    count = 3 * ctx.area_scale
    tones = [p["chips"][int(ctx.shared_rng.random() * len(p["chips"]))] for _ in range(count)]
    rgb = [[tones[i][k] for i in ids] for k in range(3)]
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, count, 0.08))
    layers = layout_field(ctx, lambda rng: fbm(rng, 3, 2, cells_y=24, size=size))
    rgb = [[c * (0.9 + 0.2 * v) for c, v in zip(channel, layers)] for channel in rgb]
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))
    rgb = [[c * (0.94 + 0.12 * v) for c, v in zip(channel, speck)] for channel in rgb]
    (*rgb, height), (common,) = jag(ctx, [*rgb, height], [common], 6, cells=12)
    rgb = shade(rgb, height, 0.35, reference_height=common)
    rgb = cast(rgb, height, p["gap"], reach=5, strength=0.55)
    return barren_mark(ctx, rgb, p["crack"], cells=4, strength=0.7, fade=0.15), None


@recipe(name="heath", group="barren", label="Heath", profile="brush",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (108, 98, 86), "mid": (126, 116, 102), "light": (138, 128, 114),
                 "crack": (74, 62, 54), "shadow": (78, 68, 60),
                 "stem": (56, 47, 42), "body": (90, 75, 65), "tip": (132, 118, 96),
                 "dead": (92, 86, 78), "deadtip": (124, 118, 106)},
        style=Style(luma=100, std=15, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4)
def render_heath(ctx):
    """Dry heath: clumps of dormant and dead heather cushions, each built of
    lit sprig tufts, over pale, faded, cracked sandy peat."""
    p = RECIPES["heath"].palette
    rgb = heath_ground(ctx, p)
    mat, cover = heather_cushions(ctx, p)
    rgb = mix(rgb, p["shadow"], soft_shadow(cover), 0.5)
    return mix(rgb, mat, cover, 1.0), None


def heath_ground(ctx, p):
    """Pale, faded sandy peat under the barren group's cracks."""
    size = ctx.size
    base = layout_field(ctx, lambda rng: fbm(rng, 4, 3, size=size))
    rgb = ramp(base, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    sand = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (0.93 + 0.14 * v) for c, v in zip(ch, sand)] for ch in rgb]
    return barren_mark(ctx, rgb, p["crack"], fade=0.15)


def soft_shadow(cover):
    """A soft shadow down-right of each covered area."""
    taps = ((3, 4, 0.35), (5, 7, 0.4), (7, 10, 0.25))
    out = [0.0] * len(cover)
    for dx, dy, w in taps:
        out = [o + w * v for o, v in zip(out, shifted(cover, dx, dy))]
    return [o * (1 - c) for o, c in zip(out, cover)]


def heather_cushions(ctx, p, cells=100, tufts=700, threshold=0.5, spread=0.12, light=0.5, part=0.55, dead_share=0.4):
    """Heath bushes: clumps of rounded cushions built of lit sprig tufts,
    a share of them dead grey. Returns (colour, cover)."""
    size, shared = ctx.size, ctx.shared_rng
    f1, _, ids, c1, _, cids = block_worley(ctx, cells)
    t1, _, _, ct1, _, _ = block_worley(ctx, tufts)
    count = cells * ctx.area_scale
    bias = [shared.random() for _ in range(count)]
    kind = [shared.random() for _ in range(count)]
    reach = [shared.uniform(0.55, 0.8) for _ in range(count)]
    patch = layout_field(ctx, lambda rng: fbm(rng, 4, 3, size=size))
    fray = layout_field(ctx, lambda rng: lattice_noise(rng, 40, size=size))
    twig = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))

    def dome(a, i, fr):
        # Rounded but with a finite rim slope, so the tiny field differences
        # just inside the exact block edge cannot jump the lit rim.
        d = a / (reach[i] * (0.75 + 0.5 * fr))
        return (1 - d * d) * (1.5 - 0.5 * d * d) if d < 1 else 0.0

    def tuft(t):
        return max(0.0, 1 - t / 0.75)

    grow = [smoothstep(threshold - 0.04, threshold + 0.04, v + spread * (bias[i] - 0.5))
            for v, i in zip(patch, ids)]
    body = [dome(a, i, fr) * g for a, i, fr, g in zip(f1, ids, fray, grow)]
    height = [b * (0.55 + 0.45 * tuft(t)) for b, t in zip(body, t1)]
    common_height = [dome(a, i, 0.5) * (0.55 + 0.45 * tuft(t)) for a, i, t in zip(c1, cids, ct1)]
    cover = [smoothstep(0.0, 0.35, b) for b in body]
    tone = [0.5 * h + 0.25 * tuft(t) + 0.25 * w for h, t, w in zip(height, t1, twig)]
    live = ramp(tone, [(0.0, p["stem"]), (0.5, p["body"]), (1.0, p["tip"])])
    dead = ramp(tone, [(0.0, p["stem"]), (0.5, p["dead"]), (1.0, p["deadtip"])])
    mat = mix(live, dead, [1.0 if kind[i] < dead_share else 0.0 for i in ids], 1.0)
    mat = apply_cell_jitter(mat, ids, cell_jitter(shared, count, 0.07))
    mat = shade(mat, height, light, reference_height=common_height)
    # Dark gaps where tufts part, deepest toward the bush rim.
    gaps = [smoothstep(0.25, 0.0, tuft(t)) * (1 - 0.6 * b) for t, b in zip(t1, body)]
    mat = mix(mat, p["stem"], gaps, part)
    return mat, cover


@recipe(name="mud", group="rough", label="Mud", profile="soft",
        seam={"height": 1},
        palette={"dark": (62, 48, 36), "mid": (88, 70, 52), "light": (116, 96, 74), "sheen": (150, 132, 112),
                 "water": (46, 38, 32), "glint": (168, 160, 150)},
        style=Style(luma=76, std=11, grain_max=8), periodic=True, grid=8, blocks=4)
def render_mud(ctx):
    """Churned heavy lumps and water-filled pits with a wet sheen."""
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
    rgb = mix(rgb, p["sheen"], pools, 0.25)
    height, common_height, pit, _ = lumps(ctx, 3, pits=0.25, warp=24)
    rgb = shade(rgb, height, 0.5, reference_height=common_height)
    return wet_pits(rgb, height, common_height, pit, p["water"], p["glint"]), None


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


@recipe(name="mudflat", group="rough", label="Mudflat", profile="soft", seam={"height": 1},
        palette={"dark": (128, 114, 96), "mid": (164, 148, 126), "light": (186, 172, 150),
                 "gap": (52, 44, 36), "damp": (96, 84, 70), "salt": (222, 216, 204), "glint": (200, 196, 186)},
        style=Style(luma=146, std=20, grain_max=14, match=0.9), periodic=True, grid=8, blocks=4)
def render_mudflat(ctx):
    """Desiccated mudflat: big curled plates split by deep dark cracks, salt
    bloom on top and damp mud in a few sunken plates."""
    p, size = RECIPES["mudflat"].palette, ctx.size
    # Plates about two tiles across; one in twelve has sunk into a damp hollow.
    f1, f2, ids, c1, c2, cids = block_worley(ctx, 3)
    count = 3 * ctx.area_scale
    sunk = [ctx.shared_rng.random() < 0.08 for _ in range(count)]

    def plate(a, b, i):
        e = b - a
        if sunk[i]:
            return -0.6
        # Flat top that curls up toward its rim, then drops into the crack.
        return smoothstep(0.03, 0.09, e) * (0.65 + 0.35 * (1 - smoothstep(0.09, 0.35, e)))

    height = [plate(a, b, i) for a, b, i in zip(f1, f2, ids)]
    common = [plate(a, b, i) for a, b, i in zip(c1, c2, cids)]
    # Bend the straight Voronoi cracks; the reference takes the common warp so
    # every layout lights against the same field.
    cx, cy = fbm(ctx.shared_rng, 6, 2, size=size), fbm(ctx.shared_rng, 6, 2, size=size)
    sx = blend_fields(cx, fbm(ctx.rng, 6, 2, size=size), ctx.interior_mask)
    sy = blend_fields(cy, fbm(ctx.rng, 6, 2, size=size), ctx.interior_mask)
    height, common = domain_warp(height, sx, sy, 10), domain_warp(common, cx, cy, 10)
    # Dry crust with salt bloom, each plate faintly its own tone.
    tone = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, count, 0.06))
    salt = layout_field(ctx, lambda rng: fbm(rng, 10, 2, size=size))
    rgb = mix(rgb, p["salt"], [smoothstep(0.62, 0.8, v) for v in salt], 0.45)
    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.1 * (v - 0.5) * 2) for c, v in zip(ch, fine)] for ch in rgb]
    rgb = shade(rgb, height, 0.45, reference_height=common)
    # Deep dark cracks between the plates; damp mud in the sunken ones.
    crack = [1 - smoothstep(0.05, 0.35, h) for h in height]
    rgb = mix(rgb, p["gap"], crack, 0.95)
    pit = [smoothstep(0.1, 0.4, -h) for h in height]
    return wet_pits(rgb, height, common, pit, p["damp"], p["glint"], depth=0.6), None


@recipe(name="pumice", group="rough", label="Pumice field", profile="rock", seam={"height": 1},
        palette={"dark": (42, 40, 40), "mid": (70, 67, 65), "light": (102, 98, 95), "hole": (12, 10, 10),
                 "rust": (104, 72, 48)},
        style=Style(luma=56, std=14, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4)
def render_pumice(ctx):
    """Pumice field: the rough group's heavy lumps as rounded blocks of dark
    porous volcanic rock, split by deep black crevices, pitted with vesicles."""
    p, size = RECIPES["pumice"].palette, ctx.size
    height, common, pit, ids = lumps(ctx, 3, pits=0.1, warp=20)
    tone = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, 3 * ctx.area_scale, 0.15))

    def vesicles(rng):
        holes = [0.0] * (size * size)
        for _ in range(110 * (size // N) ** 2):
            x, y = rng.random() * size, rng.random() * size
            r = rng.choice((2.5, 3.0, 3.5, 4.5, 4.5, 6.5)) * rng.uniform(0.8, 1.2)
            stamp_disc(holes, x, y, r, softness=0.35)
        return holes

    holes = layout_field(ctx, vesicles)
    rock = [smoothstep(0.08, 0.3, h) for h in height]
    rgb = mix(rgb, p["hole"], [v * r for v, r in zip(holes, rock)], 0.55)
    rgb = shade(rgb, height, 0.6, reference_height=common)
    rgb = mix(rgb, p["hole"], [1 - smoothstep(0.0, 0.2, h) for h in height], 0.92)
    rgb = mix(rgb, p["hole"], pit, 0.9)
    rust = layout_field(ctx, lambda rng: fbm(rng, 14, 2, size=size))
    return mix(rgb, p["rust"], [smoothstep(0.74, 0.88, v) * r for v, r in zip(rust, rock)], 0.4), None


def warped_edges(ctx, cells, warp, cells_warp=4):
    """Long, wandering fracture lines: Worley edges warped, common at the block edge."""
    size = ctx.size
    f1, f2, _, c1, c2, _ = block_worley(ctx, cells)
    cx, cy = fbm(ctx.shared_rng, cells_warp, 2, size=size), fbm(ctx.shared_rng, cells_warp, 2, size=size)
    wx = blend_fields(cx, fbm(ctx.rng, cells_warp, 2, size=size), ctx.interior_mask)
    wy = blend_fields(cy, fbm(ctx.rng, cells_warp, 2, size=size), ctx.interior_mask)
    return domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, warp)


def bubbles(ctx):
    """Clusters of small air bubbles trapped in the ice."""
    size = ctx.size

    def make(rng):
        mask = [0.0] * (size * size)
        for _ in range(3 * (size // N) ** 2):
            x, y = rng.random() * size, rng.random() * size
            for _ in range(rng.randint(5, 12)):
                stamp_disc(mask, x + rng.gauss(0, 7), y + rng.gauss(0, 7), rng.uniform(1.0, 2.6), softness=0.4)
        return mask
    return layout_field(ctx, make)


@recipe(name="frozen_lake", group="ice", label="Frozen lake", profile="frozen",
        seam={'height': 4, 'cast_q8': 64, 'cast_width_q8': 640, 'fringe': [228, 240, 248], 'fringe_q8': 96,
              'fringe_width_q8': 384},
        palette={"deep": (88, 126, 146), "mid": (130, 168, 184), "pale": (178, 206, 216), "glint": (226, 238, 242),
                 "crack": (206, 226, 232), "white": (242, 248, 250), "frost": (226, 236, 240)},
        style=Style(luma=160, std=18, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4)
def render_frozen_lake(ctx):
    """Frosted lake ice: teal depth under a thin white bloom, a few long white
    pressure cracks, trapped bubbles and patches of frost. It plays exactly
    like ice and stays static, pale and grey-white beside any water."""
    p, size = RECIPES["frozen_lake"].palette, ctx.size
    # Depth: broad clear (dark) and cloudy (pale) zones of thick lake ice.
    depth = layout_field(ctx, lambda rng: fbm(rng, 3, 3, size=size))
    rgb = ramp(depth, [(0.0, p["deep"]), (0.55, p["mid"]), (1.0, p["pale"])])
    # Cold streaks frozen into the ice.
    streak = layout_field(ctx, lambda rng: fbm(rng, 4, 3, cells_y=14, size=size))
    rgb = mix(rgb, p["pale"], [smoothstep(0.55, 0.85, v) for v in streak], 0.25)
    # Soft specular sheen: a broad diagonal glint.
    sheen = layout_field(ctx, lambda rng: fbm(rng, 2, 2, size=size))
    rgb = mix(rgb, p["glint"], [smoothstep(0.6, 0.9, v) for v in sheen], 0.35)
    # Pressure cracks: few, long and bright; a fine fracture net under them.
    major = warped_edges(ctx, 1, 18, 3)
    fine = warped_edges(ctx, 6, 6, 8)
    gate = layout_field(ctx, lambda rng: fbm(rng, 5, 2, size=size))
    net = [(1 - smoothstep(0.012, 0.03, e)) * smoothstep(0.42, 0.55, g) for e, g in zip(fine, gate)]
    rgb = mix(rgb, p["crack"], net, 0.27)
    # Only some ridges survive: long pressure cracks, not a full net.
    keep = layout_field(ctx, lambda rng: fbm(rng, 2, 2, size=size))
    ridge = [smoothstep(0.45, 0.6, k) for k in keep]
    line = [(1 - smoothstep(0.01, 0.035, e)) * r for e, r in zip(major, ridge)]
    halo = [(1 - smoothstep(0.02, 0.09, e)) * r for e, r in zip(major, ridge)]
    rgb = mix(rgb, p["crack"], halo, 0.225)
    rgb = mix(rgb, p["white"], line, 0.765)
    rgb = mix(rgb, p["white"], bubbles(ctx), 0.7)
    # Frost and wind-blown snow dusting in patches.
    frost = layout_field(ctx, lambda rng: fbm(rng, 6, 3, size=size))
    grain = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))
    dust = [smoothstep(0.62, 0.8, f) * (0.6 + 0.4 * g) for f, g in zip(frost, grain)]
    rgb = mix(rgb, p["frost"], dust, 0.7)
    return rgb, None


@recipe(name="deep_snow", group="ice", label="Deep snow", profile="soft",
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


def wrap_blur(a, r):
    """Wrapping box blur of a square numpy field of any size (`box_blur` is
    fixed at N), from running sums along each axis."""
    if r <= 0:
        return a
    k = 2 * r + 1
    out = a.copy()
    for axis in (0, 1):
        padded = np.concatenate([np.take(out, range(-r - 1, 0), axis=axis), out,
                                 np.take(out, range(0, r), axis=axis)], axis=axis)
        sums = np.cumsum(padded, axis=axis)
        n = out.shape[axis]
        out = (np.take(sums, range(k, k + n), axis=axis) - np.take(sums, range(0, n), axis=axis)) / k
    return out


def as_list(a):
    """A numpy field as the flat list the field library works on."""
    return a.ravel().tolist()


def rock_layer(ctx, cells, metric, stretch, base, slope, cap, gap, keep, cap_spread=0.5):
    """One size class of angular rock fragments, in render-pixel heights.

    Each Worley cell, inset by its own gap, is a faceted roof: height grows
    linearly with F2-F1 toward the cell's middle, so every face toward an
    edge is planar and takes one light value, and is clipped at the cell's
    own flat top (`cap`). A cell is present with probability `keep`; absent
    cells leave the ground showing. Per-cell values are drawn by id from the
    shared stream, as in `plates`, so boundary fragments match every layout.

    Returns (height, common height, ids, common ids, mask, common mask) as
    size x size arrays; height is -1e3 where no fragment stands.
    """
    size = ctx.size
    f1, f2, ids, c1, c2, cids = block_worley(ctx, cells, metric, stretch)
    count = cells * ctx.area_scale
    r = ctx.shared_rng
    gaps = np.array([gap * (0.6 + 0.8 * r.random()) for _ in range(count)])
    caps = np.array([cap * (1 - cap_spread + 2 * cap_spread * r.random()) for _ in range(count)])
    lift = np.array([r.random() for _ in range(count)])
    present = np.array([r.random() < keep for _ in range(count)])
    cell_px = size / np.sqrt(count)

    def build(a, b, i):
        a, b, i = np.asarray(a), np.asarray(b), np.asarray(i)
        inside = (b - a - gaps[i]) * cell_px  # render px inside the fragment's edge
        roof = np.minimum(np.maximum(inside, 0) * slope, caps[i])
        h = base * (0.7 + 0.6 * lift[i]) + roof
        m = (inside > 0) & present[i]
        return np.where(m, h, -1e3).reshape(size, size), m.reshape(size, size), i.reshape(size, size)

    h, m, i = build(f1, f2, ids)
    ch, cm, ci = build(c1, c2, cids)
    return h, ch, i, ci, m, cm


def scree_fields(ctx, layers):
    """Heap the size classes in a z-buffer: at each pixel the highest fragment
    wins, so larger fragments rest on and lap over smaller ones.

    Returns (height, common height, layer index, fragment id); the layer is
    -1 on open ground. Painting layers in order instead tessellates them
    into crazy paving; the z-buffer lets a cobble lap over a block's low rim
    without piercing its middle, because each block's lowest top clears the
    highest cobble (see SCREE_LAYERS).
    """
    size = ctx.size
    top, common_top = np.zeros((size, size)), np.zeros((size, size))
    layer = np.full((size, size), -1)
    fragment = np.zeros((size, size), dtype=np.int64)
    for k, spec in enumerate(layers):
        h, ch, ids, _, m, cm = rock_layer(ctx, **spec)
        over = m & (h > top)
        top = np.where(over, h, top)
        layer = np.where(over, k, layer)
        fragment = np.where(over, ids, fragment)
        common_top = np.where(cm & (ch > common_top), ch, common_top)
    return top, common_top, layer, fragment


# Talus size classes, smallest first: chips between the stones, cobbles about
# half a tile across, and a few dominant blocks a tile or more across. A
# block's lowest top (base 10 x 0.7 + cap 18 x 0.7) clears the highest cobble
# (base 5 x 1.3 + cap 8 x 1.5), so cobbles never pierce a block.
SCREE_LAYERS = [
    dict(cells=24, metric="euclid", stretch=1.0, base=2.0, slope=0.4, cap=4.0, gap=0.10, keep=0.85, cap_spread=0.6),
    dict(cells=6, metric="euclid", stretch=1.0, base=5.0, slope=0.3, cap=8.0, gap=0.07, keep=0.8, cap_spread=0.5),
    dict(cells=1, metric="angular", stretch=1.5, base=10.0, slope=0.25, cap=18.0, gap=0.05, keep=0.55, cap_spread=0.3),
]
# Per-fragment tone and iron stain, looked up by (layer, id) in a fixed table
# drawn from the shared stream, so a boundary fragment keeps its tone.
SCREE_TONES = 4096


@recipe(name="scree", group="rough", label="Scree", profile="rock", seam={"height": 1},
        palette={"void": (30, 28, 27), "ground": (70, 64, 58), "dark": (82, 80, 77),
                 "mid": (112, 108, 102), "light": (142, 138, 130), "warm": (128, 110, 92)},
        style=Style(luma=86, std=20, grain_max=14, match=0.8), periodic=True, grid=8, blocks=4)
def render_scree(ctx):
    """Talus: angular, faceted fragments from chips to blocks a tile or more
    across, heaped so they rest on and lap over each other, with occluded
    voids between and shadows cast down-right: the rough group's heavy
    relief as broken rock."""
    p, size = RECIPES["scree"].palette, ctx.size
    top, common_top, layer, fragment = scree_fields(ctx, SCREE_LAYERS)
    rock = layer >= 0
    tones = np.array([ctx.shared_rng.random() for _ in range(SCREE_TONES)])
    stained = np.array([ctx.shared_rng.random() < 0.08 for _ in range(SCREE_TONES)], dtype=float)
    key = (fragment * 7 + layer * 1031 + 3) % SCREE_TONES
    tone = np.where(rock, 0.3 + 0.55 * tones[key], 0.0)
    rgb = ramp(as_list(tone), [(0.0, p["ground"]), (0.3, p["dark"]), (0.58, p["mid"]), (0.85, p["light"])])
    rgb = mix(rgb, p["warm"], as_list(stained[key] * rock), 0.3)
    # Coarse grit on the open ground between stones, faint texture on faces.
    grit = np.array(layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))).reshape(size, size)
    fine = as_list(np.where(rock, 0.06, 0.22) * (grit - 0.5) * 2)
    rgb = [[c * (1 + f) for c, f in zip(channel, fine)] for channel in rgb]
    # A gentle bend only, so fracture edges stay straight and angular.
    (*rgb, height), (common,) = jag(ctx, [*rgb, as_list(top)], [as_list(common_top)], 3, cells=6)
    # Planar roofs give each facet one tone, with crisp creases between.
    rgb = shade(rgb, height, 0.85, reference_height=common)
    # Voids: darken where neighbouring fragments stand above this pixel.
    h = np.array(height).reshape(size, size)
    occlusion = np.clip((wrap_blur(h, 6) - h) / 6.0, 0, 1)
    rgb = mix(rgb, p["void"], as_list(occlusion), 0.9)
    return cast(rgb, height, p["void"], reach=5, strength=0.55), None


# Paths: warm and light, faster movement.

NATIVE_PX = N // 32  # render pixels per native pixel

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


@recipe(name="brick_path", group="paths", label="Brick path", profile="crisp",
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


@recipe(name="boardwalk", group="paths", label="Boardwalk", profile="crisp",
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


@recipe(name="sandstone", group="paths", label="Sandstone tiles", profile="crisp",
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
        style=Style(luma=158, std=20, grain_max=26, match=0.9), periodic=True, grid=8, blocks=4)
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
    # Lit grains one or two native pixels across, half the size of the
    # original tiles' grains, rather than independent single-pixel noise.
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
# backdrop (content/data/gfx/water0.png averaged about (69, 52, 200), luma 74). Water
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
    rgb = shade(rgb, h, 0.2, light=(-1.0, -0.3), reference_height=common_h)
    rgb = mix(rgb, p["crack"], cracks, 0.3)
    return ridge_mark(ctx, rgb), None


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
    return ridge_mark(ctx, rgb, lit=0.4, shadow=0.45), None


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


# Legacy surfaces keep their original palette/style statistics; grass is the
# exception, redrawn as sunlit turf in natural greens (minimap and preview keep
# the classic colours).

@recipe(name='grass', group="legacy", label='Grass', profile='sand',
        seam={'height': 2, 'cast_q8': 56, 'cast_width_q8': 512},
        palette={'shadow': (30, 74, 38), 'dark': (44, 98, 40), 'mid': (70, 124, 46), 'light': (112, 150, 62),
                 'warm': (138, 150, 66), 'cool': (36, 92, 64)},
        style=Style(luma=88, std=8, grain_max=8),
        periodic=True, grid=8, blocks=4, preview=(30, 113, 30), minimap=(0, 90, 0))
def render_grass(ctx):
    """Fine tufts over soft growth: blue-green hollows, yellow-green crowns and
    broad sun/moisture drift, with gentle top-left relief."""
    p, size = RECIPES["grass"].palette, ctx.size

    def growth(rng):
        g = fbm(rng, 6, 4, size=size)
        g = domain_warp(g, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 1.5 * size / N)
        tufts = fbm(rng, 64, 2, cells_y=24, size=size)
        return [0.6 * a + 0.4 * b for a, b in zip(g, tufts)]

    common_raw = growth(ctx.shared_rng)
    common = normalize_like(common_raw, common_raw, 0.02, 0.98)
    height = normalize_like(blend_fields(common_raw, growth(ctx.rng), ctx.interior_mask), common_raw, 0.02, 0.98)
    rgb = ramp(height, [(0.0, p['shadow']), (0.35, p['dark']), (0.7, p['mid']), (1.0, p['light'])])
    rgb = shade(rgb, height, 0.16, reference_height=common)
    # Broad warm/cool drift, a cell or two across, like sun and soil moisture.
    common_drift = fbm(ctx.shared_rng, 1.5, 3, size=size)
    drift = normalize_like(blend_fields(common_drift, fbm(ctx.rng, 1.5, 3, size=size), ctx.interior_mask),
                           common_drift)
    rgb = mix(rgb, p['warm'], [smoothstep(0.55, 0.9, v) for v in drift], 0.55 * 0.35)
    rgb = mix(rgb, p['cool'], [smoothstep(0.45, 0.1, v) for v in drift], 0.55 * 0.3)
    fine = blend_fields(lattice_noise(ctx.shared_rng, 64, size=size), lattice_noise(ctx.rng, 64, size=size),
                        ctx.interior_mask)
    return [[c * (1 + 0.07 * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb], None


def grass_fields(rng, size):
    """Small tufts over broad growth patches, using only measured color targets."""
    growth = fbm(rng, 8, 3, size=size)
    growth = domain_warp(growth, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 6)
    blades = fbm(rng, 48, 2, cells_y=24, size=size)
    height = [0.82 * a + 0.18 * b for a, b in zip(growth, blades)]
    return height, lattice_noise(rng, 48, size=size)


SAND_GRAINS, SAND_DUNES, SAND_BEVEL = 640, 0.25, 0.15


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
        seam={'height': 3, 'cast_q8': 48, 'cast_width_q8': 512}, palette={'dark': (95, 84, 71), 'mid': (135, 122, 103), 'light': (165, 149, 128)}, style=Style(luma=121.2, std=20.0, grain_max=22.9),
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
        for ground_name in GRASS_LOOKS + SAND_LOOKS + ("dirt", "shale")
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
        "sprite": f"{SPRITE_PREFIX}{name}",
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


def committed_phases(name, root):
    """The committed native frames of a material's first phase, as rendered."""
    rec = RECIPES[name]
    return [[Image.open(root / f"{FRAME_PREFIX}{name}{i}.png").copy() for i in range(rec.cells * rec.blocks)]]


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
                        help="merge the material blocks, bindings and pair treatments into content/data/terrain/tileset.json "
                             "from the committed frames, without rendering (writing frames also does this)")
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

    # Review modes do not write frames unless --write is given; --hd alone is
    # complete after the renders above. The catalog needs only each material's
    # mean colour, so --write-catalog alone reads it from the committed frames.
    modes = args.contact_sheet is not None or args.emit_catalog or args.write_catalog or args.hd
    renders = args.contact_sheet is not None or args.emit_catalog or not modes or args.write
    if args.write_catalog and not renders:
        write_catalog({name: committed_phases(name, root) for name in names}, root / "content/data/terrain/tileset.json")
        print(f"Merged {len(names)} material blocks from committed frames into content/data/terrain/tileset.json")
        return
    if not renders:
        return
    results, hd_results = synthesize_both(names, jobs)
    if args.contact_sheet is not None:
        args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        contact_sheet(results, root).save(args.contact_sheet)
        print(f"Wrote contact sheet {args.contact_sheet}")
    if args.emit_catalog:
        print(json.dumps(catalog_fragment(results), indent=2))
        print(presentation_initialisers(results))
    if not modes or args.write:
        for name in names:
            hashes = write_material(name, results[name], hd_results[name], root)
            stats = style_stats(results[name][0])
            print(f"{name:<14} {len(hashes):3d} frames  luma {stats['luma']:5.1f} std {stats['std']:4.1f} "
                  f"grain {stats['grain']:4.1f} sat {stats['sat']:.2f}")
        print(f"Wrote {len(names)} materials under {root / 'content/data/gfx'} and {root / 'content-src/gfx'}")
    if args.write_catalog or not modes or args.write:
        # Written frames carry their catalog blocks from the same render.
        write_catalog(results, root / "content/data/terrain/tileset.json")
        print(f"Merged {len(results)} material blocks into content/data/terrain/tileset.json")


if __name__ == "__main__":
    main()
