# SPDX-License-Identifier: GPL-3.0-or-later
"""Field library and recipe registry for procedural terrain materials.

Each terrain material lives in its own module under terrain_packs/, which
registers its recipe here with `@recipe`; materials of one family share a
helper module there (`_waves`, `_sand`, ...). This module holds what every
material builds on: periodic noise and Worley fields, stamps, shading, the
shared group marks (cracks, lumps, ridges) and the per-render `Ctx`.
tools/artwork/terrain_synth.py renders, catalogs and records the materials.
"""
import math
import random
import sys
from dataclasses import dataclass, field, asdict
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from material_tiles import (  # noqa: E402
    SHEET,
    TILE,
    fnv1a32,
    luma,
)

N = SHEET  # render size; four render pixels per native pixel
VARIANTS = 16
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


# --------------------------------------------------------------------------
# Field library: flat fields of N*N floats, periodic on the 128x128 canvas.
#
# A field is either a flat list of floats or a flat NumPy array; an RGB triple
# is three such lists, or one (3, n) array. Every helper computes with NumPy
# and returns arrays when any field it is given is an array, lists otherwise,
# so recipes written over lists keep their exact output while recipes written
# over arrays stay vectorised end to end. Generators that take no field
# (noise, cells, flat colours) return lists unless called with `array=True`.
# Each helper repeats the list arithmetic operation for operation, and sums
# run in order (`np.cumsum`), so the two forms agree bit for bit.
# --------------------------------------------------------------------------


def _isarr(*values):
    return any(isinstance(v, np.ndarray) for v in values)


# Cached list fields (block masks, band weights) and their array twins, so an
# array recipe can pass `ctx.interior_mask` without converting it each time.
_ARRAY_TWINS = {}


def _twin(values, array):
    """Remember `array` as the NumPy form of the cached list `values`."""
    array.setflags(write=False)
    _ARRAY_TWINS[id(values)] = (values, array)
    return values


def as_field(values):
    """`values` as a flat float64 array (no copy when it is one already)."""
    if isinstance(values, np.ndarray):
        return values if values.dtype == np.float64 else values.astype(np.float64)
    twin = _ARRAY_TWINS.get(id(values))
    if twin is not None and twin[0] is values:
        return twin[1]
    return np.asarray(values, dtype=np.float64)


def rgb_field(rgb):
    """An RGB triple as a (3, n) float64 array."""
    if isinstance(rgb, np.ndarray) and rgb.ndim == 2:
        return rgb if rgb.dtype == np.float64 else rgb.astype(np.float64)
    return np.stack([as_field(channel) for channel in rgb])


def _out(values, array):
    return values if array else values.tolist()


def _out_rgb(values, array):
    return values if array else [channel.tolist() for channel in values]


def total(values):
    """Sum in order, as Python's `sum` does, so list and array forms agree."""
    values = np.asarray(values, dtype=np.float64).ravel()
    return float(np.cumsum(values)[-1]) if len(values) else 0.0


def hypot(dx, dy):
    """`math.hypot` over arrays; NumPy's hypot rounds differently in the last bit."""
    dx, dy = np.broadcast_arrays(np.asarray(dx, dtype=np.float64), np.asarray(dy, dtype=np.float64))
    return np.fromiter(map(math.hypot, dx.ravel().tolist(), dy.ravel().tolist()),
                       dtype=np.float64, count=dx.size).reshape(dx.shape)


def clamp01(v):
    if isinstance(v, np.ndarray):
        return np.clip(v, 0.0, 1.0)
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


def smoothstep(e0, e1, v):
    if _isarr(e0, e1, v):
        e0, e1, v = (np.asarray(x, dtype=np.float64) for x in (e0, e1, v))
        span = e1 - e0
        with np.errstate(divide="ignore", invalid="ignore"):
            t = np.clip((v - e0) / span, 0.0, 1.0)
        out = t * t * (3 - 2 * t)
        if np.any(span == 0):
            out = np.where(span == 0, np.where(v < e0, 0.0, 1.0), out)
        return out
    if e1 == e0:
        return 0.0 if v < e0 else 1.0
    t = clamp01((v - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def lattice_noise(rng, cells, cells_y=None, *, size=N, array=False):
    """Periodic value noise: a random lattice, tiled 3x3 and bicubically resized.

    Pillow does the interpolation in one C call; the centre crop of the 3x3
    tiling is periodic by construction. Values are clamped to [0, 1].
    """
    cells_y = cells_y or cells
    cells, cells_y = cells * size / N, cells_y * size / N
    cells = max(1, min(size, int(cells)))
    cells_y = max(1, min(size, int(cells_y or cells)))
    values = np.array([rng.random() for _ in range(cells * cells_y)], dtype=np.float32).reshape(cells_y, cells)
    image = Image.frombytes("F", (cells * 3, cells_y * 3), np.tile(values, (3, 3)).tobytes())
    big = image.resize((size * 3, size * 3), Image.Resampling.BICUBIC)
    crop = big.crop((size, size, 2 * size, 2 * size))
    data = np.asarray(crop, dtype=np.float32).astype(np.float64).ravel()
    return _out(np.clip(data, 0.0, 1.0), array)


def fbm(rng, cells, octaves=3, gain=0.5, lacunarity=2.0, cells_y=None, *, size=N, array=False):
    """Fractal sum of lattice noise, centred on 0.5 and roughly in [0.1, 0.9]."""
    out = np.zeros(size * size)
    amplitude, norm = 1.0, 0.0
    cy = cells_y or cells
    for _ in range(octaves):
        layer = lattice_noise(rng, cells, cy, size=size, array=True)
        out = out + amplitude * (layer - 0.5)
        norm += amplitude
        amplitude *= gain
        cells = min(N, cells * lacunarity)
        cy = min(N, cy * lacunarity)
    return _out(np.clip(0.5 + out / norm, 0.0, 1.0), array)


def normalize(values, low=0.01, high=0.99):
    """Stretch a field so its `low`/`high` quantiles map to 0 and 1."""
    array = _isarr(values)
    v = as_field(values)
    ordered = np.sort(v)
    a = ordered[int(low * (len(ordered) - 1))]
    b = ordered[int(high * (len(ordered) - 1))]
    if b - a < 1e-9:
        return _out(np.full(len(v), 0.5), array)
    scale = 1.0 / (b - a)
    return _out(np.clip((v - a) * scale, 0.0, 1.0), array)


def normalize_like(values, reference, low=0.01, high=0.99):
    """Normalize all interiors using one shared field's quantile range."""
    array = _isarr(values, reference)
    v = as_field(values)
    ordered = np.sort(as_field(reference))
    a, b = ordered[int(low * (len(ordered) - 1))], ordered[int(high * (len(ordered) - 1))]
    if b - a < 1e-9:
        return _out(np.full(len(v), 0.5), array)
    return _out(np.clip((v - a) / (b - a), 0.0, 1.0), array)


def blend_fields(common, own, mask):
    """Vary scalar geometry fields, preserving exact common boundary samples."""
    array = _isarr(common, own)
    a = as_field(common)
    return _out(a + (as_field(own) - a) * as_field(mask), array)


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


def sample_field(values, fx, fy, size=None):
    """`sample` at arrays of positions."""
    v = as_field(values)
    size = math.isqrt(len(v)) if size is None else size
    x0 = np.floor(fx)
    y0 = np.floor(fy)
    tx = fx - x0
    ty = fy - y0
    x0 = x0.astype(np.int64) % size
    y0 = y0.astype(np.int64) % size
    x1 = (x0 + 1) % size
    y1 = (y0 + 1) % size
    top = v[y0 * size + x0] * (1 - tx) + v[y0 * size + x1] * tx
    bottom = v[y1 * size + x0] * (1 - tx) + v[y1 * size + x1] * tx
    return top * (1 - ty) + bottom * ty


def _grid(size):
    i = np.arange(size * size)
    return (i % size).astype(np.float64), (i // size).astype(np.float64)


def domain_warp(values, warp_x, warp_y, amount, amount_y=None):
    """Re-sample `values` displaced by two noise fields (±`amount` pixels)."""
    array = _isarr(values, warp_x, warp_y)
    v = as_field(values)
    size = math.isqrt(len(v))
    amount_y = amount if amount_y is None else amount_y
    xs, ys = _grid(size)
    fx = xs + (as_field(warp_x) - 0.5) * 2 * amount
    fy = ys + (as_field(warp_y) - 0.5) * 2 * amount_y
    return _out(sample_field(v, fx, fy, size), array)


def box_blur(values, radius):
    """Separable wrapping box blur, O(N^2) through running sums."""
    array = _isarr(values)
    v = as_field(values).reshape(N, N)
    size = 2 * radius + 1

    def run(a):
        # Running sums along axis 1, every row at once, in the list order.
        out = np.empty_like(a)
        acc = np.zeros(a.shape[0])
        for x in range(-radius, radius + 1):
            acc = acc + a[:, x % N]
        for x in range(N):
            out[:, x] = acc / size
            acc = acc + (a[:, (x + radius + 1) % N] - a[:, (x - radius) % N])
        return out

    rows = run(v)
    return _out(run(rows.T).T.ravel(), array)


def _rotated_distance(dx, dy, rot, stretch):
    c, sn = rot
    u = dx * c + dy * sn
    v = (-dx * sn + dy * c) * stretch
    return max(abs(u), abs(v))


def _worley_from_points(points, metric, rotations=None, stretch=1.0, *, size=N, array=False):
    """Nearest and second-nearest wrapped distance (in pixels) and nearest id."""
    # NumPy keeps the full-block free-point search practical without changing
    # point order, distance arithmetic or the first/second-nearest tie rules.
    if size > 4 * N and len(points) > 1:
        return _worley_indexed(points, metric, rotations, stretch, size=size, array=array)
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
    if array:
        return best.ravel(), second.ravel(), ids.ravel()
    return best.ravel().tolist(), second.ravel().tolist(), ids.ravel().tolist()


def _worley_indexed(points, metric, rotations=None, stretch=1.0, *, size=N, array=False):
    """Exact periodic nearest-two search, accelerated by Euclidean candidates.

    The metric's Euclidean lower bound certifies when no omitted point can
    replace the second nearest. Uncertified samples expand their candidate
    list, and sorting IDs preserves the exhaustive search's tie order.
    """
    from scipy.spatial import cKDTree

    if len(points) < 2:
        return _worley_from_points(points, metric, rotations, stretch, size=size, array=array)
    positions = np.asarray(points, dtype=np.float64)
    turns = np.asarray(rotations, dtype=np.float64) if rotations is not None else None
    tree = cKDTree(positions, boxsize=size)
    factor = 1.0 if metric == "euclid" else (1 / math.sqrt(1 + 1 / stretch ** 2)
                                               if metric == "angular" else 1 / math.sqrt(2))
    best_out = np.empty(size * size)
    second_out = np.empty(size * size)
    ids_out = np.empty(size * size, dtype=np.int64)
    for start in range(0, size * size, 16384):
        linear = np.arange(start, min(start + 16384, size * size))
        samples = np.column_stack((linear % size, linear // size))
        best, second = np.empty(len(samples)), np.empty(len(samples))
        nearest = np.empty(len(samples), dtype=np.int64)
        # Euclidean candidates certify after a few neighbours; the rotated
        # metrics' looser bound needs more.
        pending, count = np.arange(len(samples)), min(6 if metric == "euclid" else 16, len(points))
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
        best_out[start:start + len(samples)] = best
        second_out[start:start + len(samples)] = second
        ids_out[start:start + len(samples)] = nearest
    if array:
        return best_out, second_out, ids_out
    return best_out.tolist(), second_out.tolist(), ids_out.tolist()


def _frozen(*arrays):
    for a in arrays:
        a.setflags(write=False)
    return arrays


def _guard(second, size, metric, stretch):
    band = 64  # Includes the shared strip, domain-warps and lighting samples.
    s = second.reshape(size, size)
    edge = np.arange(size)
    near = np.minimum(edge, size - 1 - edge)
    within = np.minimum(near[:, None], near[None, :]) < band
    reach = float(s[within].max())
    factor = (1.0 if metric == "euclid" else
              1 / math.sqrt(1 + 1 / stretch ** 2) if metric == "angular" else 1 / math.sqrt(2))
    return math.ceil(band + reach / factor) + 1


@lru_cache(maxsize=1)
def _block_worley_master(size, count, metric, stretch, state):
    rng = random.Random()
    rng.setstate(state)
    points = [(rng.random() * size, rng.random() * size) for _ in range(count)]
    angles = [rng.random() * TAU for _ in range(count)] if metric == "angular" else []
    turns = [(math.cos(a), math.sin(a)) for a in angles] if angles else None
    first, second, ids = _worley_from_points(points, metric, turns, stretch, size=size, array=True)
    guard = _guard(second, size, metric, stretch)
    cell = size / math.sqrt(count)
    return (points, turns, *_frozen(first / cell, second / cell, ids), guard, rng.getstate())


@lru_cache(maxsize=4)
def _block_worley_kept_master(size, count, metric, stretch, state, keep):
    """`_block_worley_master` over the kept points only. Every point is still
    drawn, so the stream advances as without `keep`; the guard is certified
    against the kept points, whose cells reach further than the full set's."""
    rng = random.Random()
    rng.setstate(state)
    points = [(rng.random() * size, rng.random() * size) for _ in range(count)]
    angles = [rng.random() * TAU for _ in range(count)] if metric == "angular" else []
    turns = [(math.cos(a), math.sin(a)) for a in angles] if angles else None
    first, second, ids = _worley_kept(points, turns, keep, metric, stretch, size)
    guard = _guard(second, size, metric, stretch)
    cell = size / math.sqrt(count)
    return (points, turns, *_frozen(first / cell, second / cell, ids), guard, rng.getstate())


def _worley_kept(points, turns, keep, metric, stretch, size):
    """Worley over the points whose `keep` is true; IDs index all points."""
    index = np.array([i for i, k in enumerate(keep) if k], dtype=np.int64)
    first, second, ids = _worley_from_points(
        [points[i] for i in index], metric,
        [turns[i] for i in index] if turns is not None else None, stretch, size=size, array=True)
    return first, second, index[ids]


def _worley_out(fields, array):
    return tuple(fields) if array else tuple(f.tolist() for f in fields)


def block_worley(ctx, count, metric="euclid", stretch=1.0, keep=None, *, array=False):
    """Shared boundary cells, independently placed interior cells, exact F1/F2.

    Returns own F1/F2/IDs followed by their common references. The certified
    geometry guard keeps every layout identical throughout the outer64 pixels.

    `keep(count)`, when given, draws one flag per cell from `ctx.shared_rng`
    just after the cell points, and returns them; the cells are then built
    from the kept points alone (distances still in units of the full cell
    spacing, IDs still indexing every cell), so a kept cell is never cut by
    a neighbour that was left out.

    With `array=True` the fields are NumPy arrays; the common references are
    shared, read-only arrays.
    """
    if keep is not None:
        return _block_worley_keep(ctx, round(count * ctx.area_scale), metric, stretch, keep, array)
    size, count = ctx.size, round(count * ctx.area_scale)
    points, turns, common1, common2, common_ids, guard, state = _block_worley_master(
        size, count, metric, stretch, ctx.shared_rng.getstate())
    ctx.shared_rng.setstate(state)
    if ctx.variant == 0:
        ctx.rng.setstate(state)
        common = _worley_out((common1, common2, common_ids), array)
        return common + common
    positions, rotations = list(points), list(turns) if turns is not None else None
    for i, (x, y) in enumerate(points):
        if min(x, y, size - x, size - y) <= guard:
            continue
        positions[i] = (ctx.rng.uniform(guard, size - guard), ctx.rng.uniform(guard, size - guard))
        if rotations is not None:
            a = ctx.rng.random() * TAU
            rotations[i] = (math.cos(a), math.sin(a))
    first, second, ids = _worley_from_points(positions, metric, rotations, stretch, size=size, array=True)
    cell = size / math.sqrt(count)
    return _worley_out((first / cell, second / cell, ids, common1, common2, common_ids), array)


def _block_worley_keep(ctx, count, metric, stretch, keep, array):
    """`block_worley` with a keep-mask drawn just after the cell points."""
    size = ctx.size
    start = ctx.shared_rng.getstate()
    for _ in range(count * (3 if metric == "angular" else 2)):
        ctx.shared_rng.random()
    after_points = ctx.shared_rng.getstate()
    flags = tuple(bool(k) for k in keep(count))
    points, turns, common1, common2, common_ids, guard, _ = _block_worley_kept_master(
        size, count, metric, stretch, start, flags)
    if ctx.variant == 0:
        ctx.rng.setstate(after_points)
        common = _worley_out((common1, common2, common_ids), array)
        return common + common
    positions, rotations = list(points), list(turns) if turns is not None else None
    for i, (x, y) in enumerate(points):
        if min(x, y, size - x, size - y) <= guard:
            continue
        positions[i] = (ctx.rng.uniform(guard, size - guard), ctx.rng.uniform(guard, size - guard))
        if rotations is not None:
            a = ctx.rng.random() * TAU
            rotations[i] = (math.cos(a), math.sin(a))
    first, second, ids = _worley_kept(positions, rotations, flags, metric, stretch, size)
    cell = size / math.sqrt(count)
    return _worley_out((first / cell, second / cell, ids, common1, common2, common_ids), array)


def worley_points(rng, count, metric="euclid", stretch=1.0, *, size=N, array=False):
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
    f1, f2, ids = _worley_from_points(points, metric, rotations, stretch, size=size, array=True)
    cell = size / math.sqrt(count)
    return (*_worley_out((f1 / cell, f2 / cell, ids), array), points)


def worley(rng, cells, metric="euclid", jitter=1.0, *, size=N, array=False):
    """Jittered-grid Worley cells; F1/F2 in units of the cell size.

    Each pixel examines the nine surrounding cells with wrapped indices, so the
    field is periodic and the cost stays linear in the canvas size. Metrics:
    `euclid` (round cells), `chebyshev` (axis-aligned squares) and `angular`
    (a Chebyshev metric rotated by a random angle per cell).
    """
    canvas = size
    cells = max(1, int(cells * canvas / N))
    size = canvas / cells
    px = np.zeros((cells, cells))
    py = np.zeros((cells, cells))
    rc = np.ones((cells, cells))
    rs = np.zeros((cells, cells))
    for cy in range(cells):
        for cx in range(cells):
            px[cy, cx] = (cx + 0.5 + (rng.random() - 0.5) * jitter) * size
            py[cy, cx] = (cy + 0.5 + (rng.random() - 0.5) * jitter) * size
            if metric == "angular":
                angle = rng.random() * TAU
                rc[cy, cx], rs[cy, cx] = math.cos(angle), math.sin(angle)
    i = np.arange(canvas * canvas)
    x, y = (i % canvas).astype(np.float64), (i // canvas).astype(np.float64)
    cx0 = (x / size).astype(np.int64)
    cy0 = (y / size).astype(np.int64)
    best = np.full(len(i), 1e9)
    second = np.full(len(i), 1e9)
    best_id = np.zeros(len(i), dtype=np.int64)
    for oy in (-1, 0, 1):
        cy = (cy0 + oy) % cells
        wrap_y = ((cy0 + oy) // cells) * canvas
        for ox in (-1, 0, 1):
            cx = (cx0 + ox) % cells
            wrap_x = ((cx0 + ox) // cells) * canvas
            dx = px[cy, cx] + wrap_x - x
            dy = py[cy, cx] + wrap_y - y
            if metric == "angular":
                c, sn = rc[cy, cx], rs[cy, cx]
                d = np.maximum(np.abs(dx * c + dy * sn), np.abs((-dx * sn + dy * c) * 1.0))
            elif metric == "chebyshev":
                d = np.maximum(np.abs(dx), np.abs(dy))
            else:
                d = np.sqrt(dx * dx + dy * dy)
            closer = d < best
            nearer = ~closer & (d < second)
            second = np.where(closer, best, np.where(nearer, d, second))
            best_id = np.where(closer, cy * cells + cx, best_id)
            best = np.where(closer, d, best)
    return _worley_out((best / size, second / size, best_id), array)


def _disc_box(size, cx, cy, radius):
    """Pixel indices and centre distances of a disc's wrapped bounding box."""
    r = int(math.ceil(radius)) + 1
    ux = np.arange(int(math.floor(cx)) - r, int(math.floor(cx)) + r + 1)
    uy = np.arange(int(math.floor(cy)) - r, int(math.floor(cy)) + r + 1)
    d = hypot((ux + 0.5 - cx)[None, :], (uy + 0.5 - cy)[:, None])
    index = (uy % size)[:, None] * size + (ux % size)[None, :]
    return index, d


def stamp_disc(mask, cx, cy, radius, value=1.0, softness=0.5, mode="max"):
    """Stamp a soft disc into `mask` with wrap-around; falloff over `softness`."""
    if not isinstance(mask, np.ndarray):
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
        return
    size = math.isqrt(len(mask))
    inner = radius * (1 - softness)
    index, d = _disc_box(size, cx, cy, radius)
    inside = d < radius
    index, d = index[inside], d[inside]
    weight = np.where(d <= inner, 1.0, 1 - smoothstep(inner, radius, d))
    if mode == "max":
        mask[index] = np.maximum(mask[index], weight * value)
    else:
        mask[index] = np.clip(mask[index] + weight * value, 0.0, 1.0)


def scatter(ctx, count, radius_min, radius_max, softness=0.5, *, array=False):
    """Random soft discs, with native density on either a tile or a full block.

    Returns the union mask and (x, y, r, u) tuples. Full-block stamps can
    cross all cell edges and wrap the block; independent tiles keep a margin.
    """
    margin = STAMP_MARGIN + radius_max
    mask = np.zeros(ctx.size * ctx.size)
    discs = []
    for _ in range(count * ctx.area_scale):
        cx, cy = ctx.interior_point(margin)
        radius = radius_min + ctx.rng.random() * (radius_max - radius_min)
        discs.append((cx, cy, radius, ctx.rng.random()))
    for cx, cy, radius, _ in discs:
        stamp_disc(mask, cx, cy, radius, 1.0, softness)
    return _out(mask, array), discs


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


def render_block_domes(objects, tone_of, lumps_of=None, texture=None, *, size, array=False):
    """Rasterize only each object's wrapped bounding box, not a full canvas.

    `texture(u, angle, h)` is called with arrays of the box's angles and
    heights, so it must use NumPy functions."""
    height = np.zeros(size * size)
    rgb = np.zeros((3, size * size))
    for cx, cy, radius, u in sorted(objects, key=lambda o: o[1]):
        lumps = lumps_of(u) if lumps_of else ()
        tone = tone_of(u)
        reach = int(math.ceil(radius * 1.4)) + 1
        uy = np.arange(math.floor(cy) - reach, math.floor(cy) + reach + 1)
        ux = np.arange(math.floor(cx) - reach, math.floor(cx) + reach + 1)
        dx = np.broadcast_to((ux + 0.5 - cx)[None, :], (len(uy), len(ux)))
        dy = np.broadcast_to((uy + 0.5 - cy)[:, None], (len(uy), len(ux)))
        angle = np.arctan2(dy, dx)
        bump = np.zeros(angle.shape)
        for h, a, ph in lumps:
            bump = bump + a * np.sin(h * angle + ph)
        local = radius * (1 + bump)
        d = hypot(dx, dy) / local
        inside = d < 1
        h = np.sqrt(1 - d[inside] * d[inside])
        index = ((uy % size)[:, None] * size + (ux % size)[None, :])[inside]
        height[index] = h
        value = texture(u, angle[inside], h) if texture else 1.0
        for k in range(3):
            rgb[k, index] = tone[k] * value
    if array:
        return height, rgb
    return height.tolist(), [channel.tolist() for channel in rgb]


def shifted(values, dx, dy):
    """The field moved by (dx, dy) whole render pixels, wrapped."""
    array = _isarr(values)
    v = as_field(values)
    size = math.isqrt(len(v))
    return _out(np.roll(v.reshape(size, size), (dy, dx), axis=(0, 1)).ravel(), array)


def interior_weight(margin, fade=8):
    """0 within `margin` render px of the edge, rising to 1 over `fade`."""
    w = band_weight(margin, margin + fade)
    return [1 - v for v in w]


def band(values, edge, width):
    """1 inside `values < edge`, falling to 0 over `width` beyond it."""
    array = _isarr(values)
    return _out(1 - smoothstep(edge, edge + width, as_field(values)), array)


@lru_cache(maxsize=64)
def _ramp_lut(stops):
    lut = []
    for i in range(256):
        t = i / 255
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1 or (t1, c1) == stops[-1]:
                f = 0.0 if t1 == t0 else clamp01((t - t0) / (t1 - t0))
                lut.append(tuple(a + (b - a) * f for a, b in zip(c0, c1)))
                break
    out = np.array(lut, dtype=np.float64).T.copy()
    out.setflags(write=False)
    return out


def ramp(values, stops):
    """Map a [0,1] field through colour stops via a 256-entry LUT."""
    array = _isarr(values)
    lut = _ramp_lut(tuple((float(t), tuple(c)) for t, c in stops))
    index = (np.clip(as_field(values), 0.0, 1.0) * 255 + 0.5).astype(np.int64)
    return _out_rgb(lut[:, index], array)


def flat(color, size=N, *, array=False):
    if array:
        return np.repeat(np.asarray(color, dtype=np.float64)[:, None], size * size, axis=1)
    return [float(color[0])] * (size * size), [float(color[1])] * (size * size), [float(color[2])] * (size * size)


def _is_colour(value):
    return (isinstance(value, (tuple, list)) and len(value) == 3
            and not isinstance(value[0], (list, np.ndarray)))


def mix(rgb, color_or_rgb, mask, strength=1.0):
    """Blend toward a colour (or another RGB triple of fields) by mask*strength."""
    colour = _is_colour(color_or_rgb)
    array = _isarr(rgb, mask) or (not colour and _isarr(color_or_rgb))
    a = rgb_field(rgb)
    if colour:
        target = np.asarray(color_or_rgb, dtype=np.float64)[:, None]
    else:
        target = rgb_field(color_or_rgb)
    m = np.clip(as_field(mask) * strength, 0.0, 1.0)
    return _out_rgb(a + (target - a) * m, array)


def luma_field(rgb):
    a = rgb_field(rgb)
    return 0.299 * a[0] + 0.587 * a[1] + 0.114 * a[2]


def desaturate(rgb, amount):
    """Pull colours toward their luma by `amount` (0..1)."""
    array = _isarr(rgb)
    a = rgb_field(rgb)
    grey = luma_field(a)
    return _out_rgb(a + (grey - a) * amount, array)


def _gradient(height, light):
    h = as_field(height)
    size = math.isqrt(len(h))
    h2 = h.reshape(size, size)
    gx = np.roll(h2, -1, axis=1) - np.roll(h2, 1, axis=1)
    gy = np.roll(h2, -1, axis=0) - np.roll(h2, 1, axis=0)
    return (gx * light[0] + gy * light[1]).ravel()


def gradient_light(height, light=(-0.7, -0.7), reference_height=None):
    """Normalised directional gradient of a height field in [-1, 1]."""
    array = _isarr(height, reference_height)
    g = _gradient(height, light)
    # A common lighting scale keeps interchangeable block boundaries identical.
    reference = g if reference_height is None else _gradient(reference_height, light)
    rms = math.sqrt(total(reference * reference) / len(reference)) or 1.0
    return _out(np.clip(g / (2.5 * rms), -1.0, 1.0), array)


def shade(rgb, height, strength, light=(-0.7, -0.7), reference_height=None):
    """Bevel: brighten slopes facing `light`, darken the others."""
    array = _isarr(rgb, height, reference_height)
    g = as_field(gradient_light(height, light, reference_height))
    return _out_rgb(rgb_field(rgb) * (1 - strength * g), array)


def grain(rgb, rng, amount=0.12, cells=48):
    """Fine per-pixel grain, as the native tiles have (about one native pixel)."""
    array = _isarr(rgb)
    a = rgb_field(rgb)
    fine = lattice_noise(rng, cells, size=math.isqrt(a.shape[1]), array=True)
    return _out_rgb(a * (1 + amount * (fine - 0.5) * 2), array)


def cell_jitter(rng, count, spread, hue=0.25):
    """Per-cell brightness jitter with a small, hue-neutral colour drift."""
    jitters = []
    for _ in range(count):
        value = 1 + (rng.random() - 0.5) * 2 * spread
        warm = (rng.random() - 0.5) * 2 * spread * hue
        jitters.append((value + warm, value, value - warm))
    return jitters


def apply_cell_jitter(rgb, ids, jitters):
    array = _isarr(rgb, ids)
    table = np.asarray(jitters, dtype=np.float64).T
    return _out_rgb(rgb_field(rgb) * table[:, np.asarray(ids, dtype=np.int64)], array)


def alpha_from(values, low, high):
    array = _isarr(values)
    return _out(low + (high - low) * np.clip(as_field(values), 0.0, 1.0), array)


# --------------------------------------------------------------------------
# Perimeter, downsampling and style
# --------------------------------------------------------------------------

BAND_WEIGHTS = {}


def band_weight(inner, outer):
    """1 at the tile edge, falling to 0 between `inner` and `outer` render px."""
    key = (inner, outer)
    if key not in BAND_WEIGHTS:
        xs, ys = _grid(N)
        d = np.minimum(np.minimum(xs + 0.5, ys + 0.5), np.minimum(N - xs - 0.5, N - ys - 0.5))
        weights = 1 - smoothstep(float(inner), float(outer), d)
        BAND_WEIGHTS[key] = _twin(weights.tolist(), weights)
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
    array = _isarr(rgb, alpha)
    weight = as_field(band_weight(0, BAND)) * strength
    a = rgb_field(rgb)
    count = a.shape[1]
    lum = luma_field(a)
    mean = total(lum) / count
    low = as_field(box_blur(lum, radius))
    shift = (mean - low) * weight
    a = a + shift
    if alpha is not None:
        al = as_field(alpha)
        mean_a = total(al) / count
        low_a = as_field(box_blur(al, radius))
        alpha = _out(al + (mean_a - low_a) * weight, array)
    return _out_rgb(a, array), alpha


def box_down_fields(rgb, alpha, factor=N // TILE):
    """Area-average render fields to native size on premultiplied colour."""
    array = _isarr(rgb, alpha)
    size = N // factor
    a = rgb_field(rgb).reshape(3, N, N)
    w = np.full((N, N), 255.0) if alpha is None else as_field(alpha).reshape(N, N)
    acc = np.zeros((3, size, size))
    acc_a = np.zeros((size, size))
    # Accumulate in the order the per-pixel loop did, so sums agree exactly.
    for dy in range(factor):
        for dx in range(factor):
            weight = w[dy::factor, dx::factor]
            acc_a = acc_a + weight
            acc = acc + a[:, dy::factor, dx::factor] * weight
    inv = 1.0 / (factor * factor)
    out_alpha = (acc_a * inv).ravel()
    with np.errstate(divide="ignore", invalid="ignore"):
        out_rgb = np.where(acc_a > 0, acc / acc_a, 0.0).reshape(3, size * size)
    return _out_rgb(out_rgb, array), _out(out_alpha, array)


def style_transform(rgb, target_luma, target_std, strength=1.0):
    """Affine luma correction pulling mean and spread toward the recipe targets.

    Returns (channel means, contrast factor, shift); `apply_style` applies the
    same transform to every variant of a material so they share one tone.
    Statistics only: nothing is read from existing art.
    """
    a = rgb_field(rgb)
    count = a.shape[1]
    lum = luma_field(a)
    mean = total(lum) / count
    std = math.sqrt(total((lum - mean) ** 2) / count)
    k = 1.0 if std < 1e-6 else max(0.5, min(2.0, target_std / std))
    k = 1 + (k - 1) * strength
    means = [total(channel) / count for channel in a]
    shift = (target_luma - mean) * strength
    return means, k, shift


def apply_style(rgb, transform):
    means, k, shift = transform
    array = _isarr(rgb)
    a = rgb_field(rgb)
    m = np.asarray(means, dtype=np.float64)[:, None]
    return _out_rgb(m + (a - m) * k + shift, array)


def fields_to_image(rgb, alpha, size):
    a = rgb_field(rgb).reshape(3, size, size)
    al = np.full((size, size), 255.0) if alpha is None else as_field(alpha).reshape(size, size)
    rgba = np.concatenate([a, al[None]])
    data = np.clip(np.round(rgba), 0, 255).astype(np.uint8)
    return Image.frombytes("RGBA", (size, size), np.ascontiguousarray(data.transpose(1, 2, 0)).tobytes())


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
    # Boundary identity written to the catalog: the material's edge profile
    # and its border class (see "Boundary rules" in docs/assets/terrain-rendering.md).
    profile: str
    border_class: str
    seam: dict
    palette: dict
    style: Style
    render: object = field(repr=False, compare=False)
    phases: int = 1
    animation_ticks: int = 8
    # Crossfade weights per phase ("animation_blend_steps"); 0 blends every
    # tick. Fewer steps recompose animated terrain pages less often.
    blend_steps: int = 0
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
    # Laid stone paths whose edges follow whole stones ("stones" in the
    # catalog): the runtime keeps or drops each stone as a unit, so a border
    # never cuts one. Sawn materials such as boardwalk leave this off.
    stone_edges: bool = False

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
        centre = np.arange(size) + 0.5
        edge = np.minimum(centre, size - centre)
        mask = smoothstep(32.0, 96.0, np.minimum(edge[None, :], edge[:, None])).ravel()
        BLOCK_INTERIOR_MASKS[size] = _twin(mask.tolist(), mask)
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
        # Laid-stone geometry recorded by record_stones, for stone maps.
        self.stones = None

    @property
    def interior_mask(self):
        return block_interior_mask(self.size)

    @property
    def interior(self):
        """`interior_mask` as a read-only NumPy array."""
        return as_field(block_interior_mask(self.size))

    def interior_point(self, margin):
        """A position at least `margin` render pixels from every edge."""
        if self.size > N:
            return self.rng.random() * self.size, self.rng.random() * self.size
        span = max(1.0, N - 2 * margin)
        return margin + self.rng.random() * span, margin + self.rng.random() * span


def record_stones(ctx, ids, edge, centres, inlays=()):
    """Keep a laid surface's stone geometry on the render context.

    `ids` gives each render pixel's stone, `edge` its distance from the stone
    face's edge in render pixels (negative inside the joint) and `centres`
    each stone's centre. `inlays` names small stones set between others, such
    as keystones, which follow the stones around them at a border rather than
    standing alone. The stone layout is common to every layout of the block,
    so any layout's record describes the world's stones.
    """
    ctx.stones = (ids, edge, centres, frozenset(inlays))


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


def soft_blobs(ctx, count, radius_min, radius_max, core=0.0, *, array=False):
    """Blooms that survive the 4x downsample: 2-3 native px with a soft halo.

    Returns (halo mask, core mask, discs); the core is a small darker centre.
    """
    halo, discs = scatter(ctx, count, radius_min, radius_max, softness=0.75, array=array)
    cores = np.zeros(ctx.size * ctx.size)
    if core:
        for cx, cy, _, _ in discs:
            stamp_disc(cores, cx, cy, core, 1.0, 0.5)
    return halo, _out(cores, array), discs


# Group marks: every material in a gameplay group shares one learnable mark,
# drawn by the same helper, while its colour and material stay free per biome.
# Each helper keeps the block contract: boundary geometry from the shared
# stream, varied interiors, and a common reference for any normalisation.


def layout_field(ctx, make):
    """`make(rng)` from the shared stream at the block edge, varied inside."""
    return blend_fields(make(ctx.shared_rng), make(ctx.rng), ctx.interior_mask)


def group_cracks(ctx, cells=7, width=0.065, gaps=0.30, warp=5, *, array=False):
    """Broken dry cracks, one to two native pixels wide at the default width.

    Warped Voronoi edges, gated by broad noise so the network breaks into
    separate fissures rather than closed plates (clay keeps its own plates).
    The barren mark draws them thin and faint; rock grounds use them as veins.
    """
    f1, f2, _, _, _, _ = block_worley(ctx, cells, array=True)
    size = ctx.size
    wx = layout_field(ctx, lambda rng: fbm(rng, 5, 2, size=size, array=True))
    wy = layout_field(ctx, lambda rng: fbm(rng, 5, 2, size=size, array=True))
    edge = domain_warp(f2 - f1, wx, wy, warp)
    gate = layout_field(ctx, lambda rng: fbm(rng, 6, 2, size=size, array=True))
    return _out((1 - smoothstep(width * 0.4, width, edge)) * smoothstep(gaps - 0.06, gaps + 0.06, gate), array)


def level_ground(rgb, keep=0.5):
    """Damp relief two to twelve native pixels across toward the broad tone,
    keeping the finest grain, so the ground reads level.

    The filters read inward past the band every layout shares, so the outer
    16 render px (the perimeter strip all layouts repeat byte for byte) keep
    the ground as it is, and the levelling fades in over the next 16 px."""
    from scipy.ndimage import uniform_filter

    array = _isarr(rgb)
    a = rgb_field(rgb)
    size = math.isqrt(a.shape[1])
    a = a.reshape(3, size, size)
    broad = np.stack([uniform_filter(ch, 49, mode="wrap") for ch in a])
    mid = np.stack([uniform_filter(ch, 7, mode="wrap") for ch in a])
    levelled = broad + keep * (mid - broad) + (a - mid)
    centre = np.arange(size) + 0.5
    edge = np.minimum(centre, size - centre)
    d = np.minimum(edge[:, None], edge[None, :])
    t = np.clip((d - 16) / 16, 0, 1)
    weight = t * t * (3 - 2 * t)
    return _out_rgb((a + (levelled - a) * weight).reshape(3, size * size), array)


def compaction_grain(ctx, rgb, amount=0.035):
    """Very fine, even grain of tamped earth."""
    array = _isarr(rgb)
    fine = layout_field(ctx, lambda rng: lattice_noise(rng, 192, size=ctx.size, array=True))
    return _out_rgb(rgb_field(rgb) * (1 + amount * (fine - 0.5) * 2), array)


def group_gravel(ctx, rgb, count, radius_min, radius_max, lift, shadow=0.0, offset=3):
    """Small flat stones strewn evenly: paler, greyer bodies of the ground's
    own colour, each casting a short shadow `offset` render px down-right."""
    array = _isarr(rgb)
    size = ctx.size

    def make(rng):
        mask = np.zeros(size * size)
        for _ in range(count * ctx.area_scale):
            stamp_disc(mask, rng.random() * size, rng.random() * size,
                       rng.uniform(radius_min, radius_max), 1.0, 0.45)
        return mask

    body = layout_field(ctx, make)
    cast = np.maximum(0.0, shifted(body, offset, offset) - body)
    tone = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size, array=True))
    a = rgb_field(rgb).reshape(3, size, size)
    b = body.reshape(size, size)
    s = cast.reshape(size, size)
    t = tone.reshape(size, size)
    grey = a.mean(axis=0, keepdims=True)
    stone = (a + (grey - a) * 0.35) * (1 + lift * (0.6 + 0.8 * t))
    a = (a + (stone - a) * b) * (1 - shadow * s)
    return _out_rgb(a.reshape(3, size * size), array)


def barren_mark(ctx, rgb, crack_colour, cells=7, strength=1.0, fade=0.22):
    """Barren mark: faded, level, packed ground strewn with fine grit and
    small flat stones, with a few faint hairline cracks.

    Firm and buildable, but dry: nothing grows. `crack_colour` may be a colour
    or an RGB triple of fields. The recipes' style targets keep the contrast
    low (about 0.7 of a cracked ground's), so the packing is not undone.
    """
    array = _isarr(rgb)
    rgb = desaturate(rgb_field(rgb), fade + 0.05)
    rgb = level_ground(rgb, 0.5)
    rgb = compaction_grain(ctx, rgb)
    cracks = group_cracks(ctx, cells, width=0.045, gaps=0.45, array=True)
    rgb = mix(rgb, crack_colour, cracks, strength * 0.45)
    rgb = group_gravel(ctx, rgb, 30, 2.2, 3.0, lift=0.28)
    return _out_rgb(group_gravel(ctx, rgb, 8, 5.0, 7.0, lift=0.38, shadow=0.35, offset=3), array)


def worn_edge(rgb, lines, light=(1, 1), strength=0.16):
    """Paths mark: a crisp, lighter worn lip beside every straight line.

    `lines` marks grooves (ruts, joints, plank gaps); the lip is the strip
    just past each groove toward the viewer's light."""
    array = _isarr(rgb, lines)
    dx, dy = light
    a = as_field(lines)
    lip = np.maximum(0.0, shifted(a, dx, dy) - a)
    return _out_rgb(rgb_field(rgb) * (1 + strength * lip), array)


def lumps(ctx, cells, pits=0.0, metric="euclid", stretch=1.0, rise=0.6, warp=0, *, array=False):
    """Rough mark: heavy lumps, with some cells sunk into wet pits.

    Returns (height, common height, pit mask, cell ids). Height rises from 0
    at every cell border over `rise` (in F2-F1 units: large for rounded
    lumps, small for flat-topped slabs) and is negative in pit cells. Pits are
    chosen per cell id from the shared stream, so boundary pits are the same
    pits in every layout. `warp` rounds soft lumps (render px); the signed
    height is warped, so pit rims stay continuous. Cell ids stay unwarped.
    """
    f1, f2, ids, common1, common2, common_ids = block_worley(ctx, cells, metric, stretch, array=True)
    count = cells * ctx.area_scale
    sunk = np.array([1.0 if ctx.shared_rng.random() < pits else 0.0 for _ in range(count)])

    def dome(a, b, i):
        h = smoothstep(0.0, rise, b - a)
        return np.where(sunk[i] != 0, -h * 0.8, h)

    height = dome(f1, f2, ids)
    common = dome(common1, common2, common_ids)
    if warp:
        common_x = fbm(ctx.shared_rng, 4, 2, size=ctx.size, array=True)
        common_y = fbm(ctx.shared_rng, 4, 2, size=ctx.size, array=True)
        wx = blend_fields(common_x, fbm(ctx.rng, 4, 2, size=ctx.size, array=True), ctx.interior_mask)
        wy = blend_fields(common_y, fbm(ctx.rng, 4, 2, size=ctx.size, array=True), ctx.interior_mask)
        height = domain_warp(height, wx, wy, warp)
        common = domain_warp(common, common_x, common_y, warp)
    pit = smoothstep(0.1, 0.4, -height)
    return _out(height, array), _out(common, array), _out(pit, array), _out(ids, array)


def plates(ctx, cells, metric="angular", stretch=1.0, step=0.3, rise=0.1, *, array=False):
    """Overlapping slabs: Worley cells raised to their own level.

    Returns (height, common height, cell ids). Each cell is a flat-topped
    slab at a level drawn per cell id from the shared stream, so boundary
    slabs stand at the same level in every layout and neighbours meet at
    steps that `shade` and `cast` pick out, as if one slab lay over the next.
    `rise` (in F2-F1 units) is the bevel width at each slab's rim.
    """
    f1, f2, ids, common1, common2, common_ids = block_worley(ctx, cells, metric, stretch, array=True)
    levels = np.array([ctx.shared_rng.random() for _ in range(cells * ctx.area_scale)])

    def slab(a, b, i):
        return step * levels[i] + smoothstep(0.0, rise, b - a)

    return _out(slab(f1, f2, ids), array), _out(slab(common1, common2, common_ids), array), _out(ids, array)


def cast(rgb, height, colour, reach=3, strength=0.5):
    """Shadow a higher neighbour casts down-right onto lower ground: wherever
    the height `reach` px up and to the left stands above this pixel, blend
    toward `colour` in proportion to the step."""
    array = _isarr(rgb, height)
    h = as_field(height)
    drop = np.clip((shifted(h, reach, reach) - h) * 4, 0.0, 1.0)
    return _out_rgb(mix(rgb_field(rgb), colour, drop, strength), array)


def jag(ctx, fields, common, amount, cells=16):
    """Break straight cell edges into jagged ones. `fields` (lists) take one
    warp that is common at the block edge and varied inside; their `common`
    references take the common warp alone, so every layout lights against
    the same reference."""
    array = _isarr(*fields, *common)
    size = ctx.size
    cx = fbm(ctx.shared_rng, cells, 2, size=size, array=True)
    cy = fbm(ctx.shared_rng, cells, 2, size=size, array=True)
    wx = blend_fields(cx, fbm(ctx.rng, cells, 2, size=size, array=True), ctx.interior_mask)
    wy = blend_fields(cy, fbm(ctx.rng, cells, 2, size=size, array=True), ctx.interior_mask)
    return ([_out(domain_warp(as_field(f), wx, wy, amount), array) for f in fields],
            [_out(domain_warp(as_field(f), cx, cy, amount), array) for f in common])


def wet_pits(rgb, height, common, pit, water, sheen, depth=0.75):
    """Rough mark: standing water in the pits, a bright sheen on the lit rim."""
    array = _isarr(rgb, height, common, pit)
    p = as_field(pit)
    rgb = mix(rgb_field(rgb), water, p, depth)
    glint = np.maximum(0.0, -gradient_light(as_field(height), reference_height=as_field(common))) * p
    return _out_rgb(mix(rgb, sheen, smoothstep(0.15, 0.6, glint), 0.85), array)


def ridge_mark(ctx, rgb, spacing=64, wobble=0.5, riser=0.14, lip=0.05, lit=0.3, shadow=0.35):
    """Ridges mark: long, roughly level ledges every `spacing` render px.

    Down the screen each ledge top ends in a narrow lit lip, then a short
    shaded riser drops to the next ledge. Ledge k is where
    `y / spacing + wobble * noise` crosses k; only its fractional part is
    used, so the ledges wrap with the block."""
    array = _isarr(rgb)
    size = ctx.size
    noise = layout_field(ctx, lambda rng: fbm(rng, 3, 2, cells_y=2, size=size, array=True))
    top, fall = 1 - riser - lip, 1 - riser
    rows = np.arange(size * size) // size
    f = (rows / spacing + wobble * (noise - 0.5)) % 1
    glint = smoothstep(top - 0.02, top + 0.01, f) * (1 - smoothstep(fall - 0.01, fall + 0.01, f))
    face = smoothstep(fall - 0.01, fall + 0.01, f) * (1 - 0.6 * smoothstep(fall, 1.0, f))
    scale = 1 + lit * glint - shadow * face
    return _out_rgb(rgb_field(rgb) * scale, array)


def as_list(a):
    """A numpy field as the flat list the field library works on."""
    return a.ravel().tolist()


NATIVE_PX = N // 32  # render pixels per native pixel
