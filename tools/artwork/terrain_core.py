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
import struct
import sys
from dataclasses import dataclass, field, asdict
from functools import lru_cache
from pathlib import Path

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
    size, count = ctx.size, round(count * ctx.area_scale)
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


def as_list(a):
    """A numpy field as the flat list the field library works on."""
    return a.ravel().tolist()


NATIVE_PX = N // 32  # render pixels per native pixel
