# SPDX-License-Identifier: GPL-3.0-or-later
"""Surfaces of the connected Barriers masses drawn by terrain_decor.py.

Each mass is a raised surface over its painted footprint, drawn with the same
corner-mask construction as cliffs (see terrain_decor.render_mass). A frame may
depend only on its corner mask, its four corner colours and its interior
variant, so every surface here is built from parts that neighbouring frames
share exactly:

- per-vertex-colour noise patches, blended with variance-preserving bilinear
  weights, so along a shared edge only that edge's two vertices count;
- an interior patch for filled cells whose weight is zero at the cell edge;
- vertex-disc features (shards, blocks) that never reach a farther cell, drawn
  whole by every frame that sees them;
- edge features keyed by the colours of the edge's two ends.

A surface returns (top colour, outline heights, wall columns, wall grit) as
images over the cell plus its halo, and each mass names a wall colour ramp.
"""
import math
import random
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).resolve().parent))
from material_tiles import fnv1a32  # noqa: E402

CELL, HALO = 128, 22
SIZE = CELL + 2 * HALO
PATCH = 2 * (CELL + HALO)
DISC = CELL - HALO - 8          # vertex features stay inside the four cells around their vertex
LIGHT = np.array([-0.50, -0.62, 0.60]) / np.linalg.norm([-0.50, -0.62, 0.60])
VIEW = np.array([0.0, 0.7071, 0.7071])


def rng_for(*parts):
    return np.random.default_rng(fnv1a32(":".join(str(p) for p in parts)))


def noise(rng, cells_x, cells_y=None, size=PATCH):
    """Smooth value noise in 0..1 over a size x size patch."""
    cells_y = cells_y or cells_x
    grid = rng.random((cells_y + 4, cells_x + 4))
    ys = 1 + np.arange(size) * cells_y / size
    xs = 1 + np.arange(size) * cells_x / size
    out = ndimage.map_coordinates(grid, np.meshgrid(ys, xs, indexing="ij"), order=3, mode="nearest")
    return np.clip(out, 0, 1)


def octaves(rng, cells, count, size=PATCH, stretch=1.0):
    total, norm = np.zeros((size, size)), 0.0
    for o in range(count):
        c = cells * 2 ** o
        total += 0.5 ** o * noise(rng, max(1, int(c)), max(1, int(c * stretch)), size)
        norm += 0.5 ** o
    return total / norm


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def mix(c0, c1, t):
    t = np.asarray(t, float)[..., None]
    return np.asarray(c0, float) * (1 - t) + np.asarray(c1, float) * t


def ramp(t, stops):
    t = np.clip(np.asarray(t, float), 0, 1)
    out = np.zeros(t.shape + (3,))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        m = (t >= t0) & (t <= t1)
        f = ((t[m] - t0) / max(1e-9, t1 - t0))[:, None]
        out[m] = np.asarray(c0) * (1 - f) + np.asarray(c1) * f
    return out


GRID_U = np.clip((np.arange(SIZE) - HALO + 0.5) / CELL, 0, 1)
U, V = np.meshgrid(GRID_U, GRID_U)
WEIGHTS = [(1 - U) * (1 - V), U * (1 - V), (1 - U) * V, U * V]
INSIDE = np.zeros((SIZE, SIZE), bool)
INSIDE[HALO:HALO + CELL, HALO:HALO + CELL] = True
INTERIOR = 0.35 * smooth(0.0, 0.32, np.minimum.reduce([U, 1 - U, V, 1 - V])) * INSIDE


def corner_view(patch, k):
    """The part of a vertex-centred patch that covers this cell (plus halo) for corner k."""
    oy = PATCH // 2 - HALO - (CELL if k & 2 else 0)
    ox = PATCH // 2 - HALO - (CELL if k & 1 else 0)
    return patch[oy:oy + SIZE, ox:ox + SIZE]


def blend(colours, patches, detail=None):
    """Variance-preserving bilinear blend of the four corners' patches, plus a
    filled cell's interior patch whose weight is zero at the cell edge."""
    weights = list(WEIGHTS)
    inside = INTERIOR if detail is not None else np.zeros((SIZE, SIZE))
    weights = [w * (1 - inside) for w in weights]
    norm = np.sqrt(sum(w * w for w in weights) + inside * inside)
    total = sum(w * (corner_view(patches[(colours >> k) & 1], k) - 0.5) for k, w in enumerate(weights))
    if detail is not None:
        d = np.full((SIZE, SIZE), 0.5)
        d[HALO:HALO + CELL, HALO:HALO + CELL] = detail
        total = total + inside * (d - 0.5)
    return np.clip(0.5 + total / norm, 0, 1)


def edge_cracks(colours, shape, horizontal=True, vertical=False):
    """(depth 0..1, near-lip side 0..1) of cracks that follow cell edges.

    shape(first, second, t) gives the (offset, half width) a fraction t along
    an edge from colour `first` to colour `second`; it must depend on those
    alone, so the two cells sharing the edge draw the same crack."""
    bit = lambda k: (colours >> k) & 1
    crack, near = np.zeros((SIZE, SIZE)), np.zeros((SIZE, SIZE))
    edges = []
    if horizontal:
        edges += [(False, HALO, bit(0), bit(1)), (False, HALO + CELL, bit(2), bit(3))]
    if vertical:
        edges += [(True, HALO, bit(0), bit(2)), (True, HALO + CELL, bit(1), bit(3))]
    along = np.arange(SIZE)
    for vert, line, first, second in edges:
        t = np.clip((along - HALO + 0.5) / CELL, 0, 1)
        offset, width = np.vectorize(lambda tt: shape(first, second, tt, vert))(t)
        centre = line + offset
        across = np.arange(SIZE)[:, None]
        rel = (across + 0.5 - centre[None, :]) / np.maximum(width[None, :], 1e-6)
        core = np.clip(1 - np.abs(rel), 0, 1) ** 0.6 * (width[None, :] > 0)
        side = smooth(-0.3, 0.6, rel)
        if vert:
            core, side = core.T, side.T
        take = core > crack
        crack = np.where(take, core, crack)
        near = np.where(take, side, near)
    return crack, near


def crack_shape(name, amplitude, widths, offsets):
    """An edge crack's course: vertex values plus harmonics that vanish at both ends."""
    cache = {}

    def shape(first, second, t, vertical):
        key = (first, second, vertical)
        if key not in cache:
            r = random.Random(fnv1a32(f"{name}-crack:{int(vertical)}:{first}:{second}"))
            cache[key] = ([(k, r.uniform(-1, 1) * amplitude / k ** 1.3) for k in range(1, 8)], r.uniform(-4, 8))
        harmonics, swell = cache[key]
        ease = t * t * (3 - 2 * t)
        hump = math.sin(math.pi * t)
        offset = offsets[first] + (offsets[second] - offsets[first]) * ease + sum(
            a * math.sin(k * math.pi * t) for k, a in harmonics)
        width = widths[first] + (widths[second] - widths[first]) * ease + swell * hump
        return offset, (width if width > 2.4 else 0.0)
    return shape


EDGE_SPAN = CELL // 2 - 12      # edge features keep this close to the edge's midpoint along it
EDGE_DEPTH = CELL - 34         # and this close to the edge across it


def _feature_layer(name, kind, key, size, draw, region):
    """A feature patch: draw(rng, layer, centre_y, centre_x, region) fills it."""
    cache = _feature_layer.cache
    ident = (name, kind, key)
    if ident not in cache:
        layer = {"key": np.full((size, size), -1.0)}
        draw(rng_for(name, kind, key), layer, size / 2, size / 2, region)
        cache[ident] = layer
    return cache[ident]


_feature_layer.cache = {}


def _place(fields, best, layer, top, left):
    """Copy a layer's features over the frame where they outrank what is there."""
    size = layer["key"].shape[0]
    y0, x0 = max(0, top), max(0, left)
    y1, x1 = min(SIZE, top + size), min(SIZE, left + size)
    if y1 <= y0 or x1 <= x0:
        return
    src = (slice(y0 - top, y1 - top), slice(x0 - left, x1 - left))
    dst = (slice(y0, y1), slice(x0, x1))
    take = layer["key"][src] > best[dst]
    best[dst] = np.where(take, layer["key"][src], best[dst])
    for n, v in layer.items():
        if n == "key":
            continue
        if n not in fields:
            fields[n] = np.zeros((SIZE, SIZE) + v.shape[2:])
        fields[n][dst] = np.where(take[(...,) + (None,) * (v.ndim - 2)], v[src], fields[n][dst])


def features(name, colours, interior, filled, draw):
    """Features from the cell's four vertex discs, its four edges (keyed by the
    colours of each edge's ends) and, in a filled cell, its interior, so each
    feature is drawn whole by every frame that sees it and the arrangement
    varies with the corner colours rather than repeating per vertex."""
    best = np.full((SIZE, SIZE), -1.0)
    fields = {}
    bit = lambda k: (colours >> k) & 1
    for k in range(4):
        layer = _feature_layer(name, "vertex", bit(k), PATCH, draw, ("disc", DISC - 6))
        _place(fields, best, layer, HALO + (CELL if k & 2 else 0) - PATCH // 2,
               HALO + (CELL if k & 1 else 0) - PATCH // 2)
    for vertical, first, second, cy, cx in ((False, bit(0), bit(1), HALO, HALO + CELL // 2),
                                            (False, bit(2), bit(3), HALO + CELL, HALO + CELL // 2),
                                            (True, bit(0), bit(2), HALO + CELL // 2, HALO),
                                            (True, bit(1), bit(3), HALO + CELL // 2, HALO + CELL)):
        region = ("box", EDGE_DEPTH, EDGE_SPAN) if vertical else ("box", EDGE_SPAN, EDGE_DEPTH)
        layer = _feature_layer(name, f"edge{int(vertical)}", (first, second), 2 * CELL, draw, region)
        _place(fields, best, layer, cy - CELL, cx - CELL)
    if filled:
        layer = _feature_layer(name, "interior", interior, CELL, draw, ("box", CELL // 2 - 10, CELL // 2 - 10))
        _place(fields, best, layer, HALO, HALO)
    fields["key"] = best
    return fields


def region_point(rng, cy, cx, region, margin):
    """A random centre inside a feature region, keeping `margin` from its edge."""
    if region[0] == "disc":
        a, r = rng.uniform(0, math.tau), math.sqrt(rng.random()) * max(1, region[1] - margin)
        return cy + math.sin(a) * r, cx + math.cos(a) * r
    return cy + rng.uniform(-1, 1) * max(1, region[1] - margin), cx + rng.uniform(-1, 1) * max(1, region[2] - margin)


def region_mask(shape, cy, cx, region):
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]]
    if region[0] == "disc":
        return np.hypot(yy - cy, xx - cx) < region[1]
    return (np.abs(yy - cy) < region[1]) & (np.abs(xx - cx) < region[2])


def light(relief, scale=12.0):
    gy, gx = np.gradient(relief * scale)
    n = np.stack([-gx, -gy, np.ones_like(relief)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n, n @ LIGHT


def images(rgb, heights, columns, grit):
    """Surface colour, outline heights and wall marks as the images render_mass reads."""
    image = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")
    h = Image.fromarray((np.clip(heights, 0, 1) * 255).astype(np.uint8), "L")
    marks = Image.fromarray((np.clip(columns, 0, 1) * 255).astype(np.uint8), "L")
    rough = Image.fromarray((np.clip(grit, 0, 1) * 255).astype(np.uint8), "L")
    return image, h, marks.filter(ImageFilter.GaussianBlur((0.4, 30))), rough.filter(ImageFilter.GaussianBlur(1.0))


# --------------------------------------------------------------------------
# Sandstone benches: a pale caprock split by joints into benches, standing on
# thick beds that each step back a little from the one below.
# --------------------------------------------------------------------------

def _patches(name, specs, colour):
    rng = rng_for(name, "patch", colour)
    return [spec(rng) for spec in specs]


def _details(name, specs, interior):
    rng = rng_for(name, "interior", interior)
    return [spec(rng) for spec in specs]


SANDSTONE_PATCH = [lambda r: octaves(r, 2, 3), lambda r: octaves(r, 9, 2), lambda r: octaves(r, 3, 2)]
SANDSTONE_DETAIL = [lambda r: octaves(r, 1, 3, CELL), lambda r: octaves(r, 4, 2, CELL), lambda r: octaves(r, 1, 2, CELL)]
SANDSTONE_JOINT = crack_shape("sandstone", 10, (0.0, 5.5), (-34, 26))
# Vertical joints close at vertices of the first colour, so blocks vary in length.
SANDSTONE_CROSS_JOINT = crack_shape("sandstone-cross", 8, (0.0, 5.0), (-30, 24))


def sandstone_surface(colours, interior, filled):
    patches = [_patches("sandstone", SANDSTONE_PATCH, c) for c in (0, 1)]
    detail = _details("sandstone", SANDSTONE_DETAIL, interior) if filled else None
    tone, pits, step = (blend(colours, [patches[0][i], patches[1][i]], detail[i] if detail else None) for i in range(3))
    joints, near = edge_cracks(colours, SANDSTONE_JOINT, horizontal=True)
    cross, cross_near = edge_cracks(colours, SANDSTONE_CROSS_JOINT, horizontal=False, vertical=True)
    near = np.where(cross > joints, cross_near, near)
    joints = np.maximum(joints, cross)
    # Benches: the step field cut into two levels with a short rounded riser.
    level = step * 3.0
    bench = np.floor(level) + smooth(0.0, 0.25, level - np.floor(level))
    relief = 0.5 + 0.22 * bench + 0.04 * (tone - 0.5) - 0.30 * joints
    n, lam = light(relief, 10)
    rgb = ramp(0.5 + 0.8 * (tone - 0.5), [(0, (212, 178, 134)), (0.5, (228, 200, 158)), (1, (242, 222, 184))])
    rgb = rgb * (0.94 + 0.10 * pits)[..., None]
    inner = mix((150, 98, 66), (96, 60, 42), np.clip(joints * (0.5 + 0.6 * near), 0, 1))
    rgb = mix(rgb, inner, smooth(0.0, 0.2, joints))
    rgb = rgb * np.clip(0.9 + 0.85 * (lam - LIGHT[2]), 0.5, 1.15)[..., None]
    return images(rgb, tone, 0.5 + 0.9 * (pits - 0.5) - 0.6 * joints, step)


def sandstone_wall(t, c, g):
    """Thick beds by height, each recessed at its foot; wind hollows from the grit."""
    beds = ((198, 130, 86), (218, 156, 106), (230, 178, 128))
    level = t * 3.0
    k = min(2, int(level))
    colour = beds[k]
    shade = 0.82 + 0.30 * (c - 0.5)
    if level - k < 0.12 and k > 0:
        shade *= 0.72                      # the recess under the bed above
    if g > 0.68:
        shade *= 0.8                       # a wind hollow
    if t > 0.95:
        colour, shade = (236, 210, 168), max(shade, 0.98)
    return tuple(int(min(255, ch * shade)) for ch in colour)


# --------------------------------------------------------------------------
# Obsidian ridge: a heap of glassy black shards, each flat facet catching the
# sky or a sharp glint, with thin mahogany bands.
# --------------------------------------------------------------------------

def _shards(rng, layer, centre_y, centre_x, region):
    size_px = layer["key"].shape[0]
    key = layer["key"]
    normal = np.zeros((size_px, size_px, 3))
    gloss = np.zeros((size_px, size_px))
    yy, xx = np.mgrid[0:size_px, 0:size_px]
    allowed = region_mask(key.shape, centre_y, centre_x, region)
    count = 26 if region[0] == "disc" else 12
    for i in range(count):
        cy, cx = region_point(rng, centre_y, centre_x, region, 22)
        size = rng.uniform(12, 24)
        sides = int(rng.integers(4, 7))
        ang0 = rng.uniform(0, math.tau)
        inside = np.ones((size_px, size_px), bool)
        for s in range(sides):
            t = ang0 + math.tau * (s + rng.uniform(-0.2, 0.2)) / sides
            rr = size * rng.uniform(0.7, 1.0)
            inside &= (xx - cx) * math.cos(t) + (yy - cy) * math.sin(t) < rr
        inside &= allowed
        tilt = rng.normal(0, 0.55, 2)
        nrm = np.array([tilt[0], tilt[1], 1.0])
        nrm /= np.linalg.norm(nrm)
        prio = rng.random() + 1
        take = inside & (prio > key)
        key[take] = prio
        normal[take] = nrm
        gloss[take] = rng.uniform(0.2, 1.0)
    layer.update(normal=normal, gloss=gloss)


OBSIDIAN_PATCH = [lambda r: octaves(r, 3, 2), lambda r: octaves(r, 2, 2, stretch=4)]
OBSIDIAN_DETAIL = [lambda r: octaves(r, 1, 2, CELL), lambda r: octaves(r, 1, 2, CELL, stretch=4)]


def obsidian_surface(colours, interior, filled):
    patches = [_patches("obsidian", OBSIDIAN_PATCH, c) for c in (0, 1)]
    detail = _details("obsidian", OBSIDIAN_DETAIL, interior) if filled else None
    tone, band = (blend(colours, [patches[0][i], patches[1][i]], detail[i] if detail else None) for i in range(2))
    shards = features("obsidian", colours, interior, filled, _shards)
    has = shards["key"] > 0
    normal = np.where(has[..., None], shards["normal"], np.array([0.0, 0.0, 1.0]))
    lam = normal @ LIGHT
    base = mix((38, 38, 46), (84, 46, 36), smooth(0.80, 0.92, 0.5 + 0.5 * np.sin(band * 40)) * 0.35)
    sky = smooth(0.55, 0.98, normal[..., 2])
    rgb = mix(base, (132, 140, 162), sky * 0.7 * has * (0.5 + 0.5 * shards["gloss"]))
    rgb = rgb * np.clip(0.75 + 0.6 * lam, 0.45, 1.25)[..., None]
    refl = 2 * (normal @ LIGHT)[..., None] * normal - LIGHT
    spec = np.clip(refl @ VIEW, 0, 1) ** 24 * has * shards["gloss"]
    rgb = rgb + spec[..., None] * np.array([170, 176, 196])
    # Bright crests where shards meet.
    edge = (ndimage.maximum_filter(shards["key"], 3) != ndimage.minimum_filter(shards["key"], 3)) & has
    rgb = np.where(edge[..., None], rgb * 0.55 + np.array([110, 116, 132]) * 0.6, rgb)
    return images(rgb, tone, 0.5 + 0.8 * (tone - 0.5), band)


def obsidian_wall(t, c, g):
    colour = tuple(int(a + (b - a) * t) for a, b in zip((22, 22, 28), (64, 66, 80)))
    shade = 0.85 + 0.35 * (c - 0.5)
    sheen = max(0.0, (c - 0.72) * 3.0)     # vertical glints down the glass
    return tuple(int(min(255, ch * shade + 120 * sheen)) for ch in colour)


# --------------------------------------------------------------------------
# Granite tors: rounded, pillowy blocks with sheeting joints, moss gathered in
# the hollows between them, speckled feldspar and biotite.
# --------------------------------------------------------------------------

def _blocks(rng, layer, centre_y, centre_x, region):
    size_px = layer["key"].shape[0]
    key = layer["key"]
    height = np.zeros((size_px, size_px))
    yy, xx = np.mgrid[0:size_px, 0:size_px]
    allowed = region_mask(key.shape, centre_y, centre_x, region)
    count = 5 if region[0] == "disc" else 2
    for i in range(count):
        cy, cx = region_point(rng, centre_y, centre_x, region, 40)
        ra, rb = rng.uniform(30, 46), rng.uniform(24, 36)
        ang = rng.uniform(0, math.pi)
        u = ((xx - cx) * math.cos(ang) + (yy - cy) * math.sin(ang)) / ra
        v = (-(xx - cx) * math.sin(ang) + (yy - cy) * math.cos(ang)) / rb
        d = (np.abs(u) ** 2.5 + np.abs(v) ** 2.5) ** (1 / 2.5)
        h = np.clip(1 - d ** 2.5, 0, 1) ** 0.4 * rng.uniform(0.6, 1.0)
        h = np.where(allowed, h, 0)
        take = (h > height) & (h > 0)
        height = np.where(take, h, height)
        key[take] = height[take] + rng.random() * 1e-3
    layer.update(height=height)


GRANITE_PATCH = [lambda r: octaves(r, 2, 2), lambda r: octaves(r, 4, 3), lambda r: noise(r, 60), lambda r: noise(r, 56)]
GRANITE_DETAIL = [lambda r: octaves(r, 1, 2, CELL), lambda r: octaves(r, 2, 3, CELL), lambda r: noise(r, 26, size=CELL),
                  lambda r: noise(r, 24, size=CELL)]


def granite_surface(colours, interior, filled):
    patches = [_patches("granite", GRANITE_PATCH, c) for c in (0, 1)]
    detail = _details("granite", GRANITE_DETAIL, interior) if filled else None
    tone, moss, speck, speck2 = (blend(colours, [patches[0][i], patches[1][i]], detail[i] if detail else None)
                                 for i in range(4))
    blocks = features("granite", colours, interior, filled, _blocks)
    height = np.where(blocks["key"] > 0, blocks["height"], 0.0)
    # Sheeting joints: the domes break into stacked slabs.
    sheets = height * 3.0
    slab = (np.floor(sheets) + smooth(0.0, 0.3, sheets - np.floor(sheets))) / 3.0
    relief = 0.25 + 0.75 * slab + 0.05 * (tone - 0.5)
    n, lam = light(relief, 22)
    rgb = mix((132, 126, 116), (172, 164, 150), tone)
    rgb = mix(rgb, (86, 82, 78), smooth(0.80, 0.88, speck) * 0.35)
    rgb = mix(rgb, (204, 198, 184), smooth(0.82, 0.90, speck2) * 0.35)
    low = smooth(0.18, 0.0, height)
    rgb = mix(rgb, (70, 68, 62), low * 0.55)                     # gaps between the blocks fall into shade
    m = smooth(0.55, 0.75, moss + 0.35 * low)
    rgb = mix(rgb, mix((66, 90, 44), (98, 120, 58), moss), m * 0.75)
    rgb = rgb * np.clip(0.9 + 0.8 * (lam - LIGHT[2]), 0.5, 1.15)[..., None]
    return images(rgb, tone, 0.5 + 0.8 * (tone - 0.5), moss)


def granite_wall(t, c, g):
    colour = (142, 136, 124) if t > 0.15 else (110, 118, 80)       # moss at the foot
    shade = 0.80 + 0.3 * (c - 0.5)
    if abs(t * 3 - round(t * 3)) < 0.05 and 0.1 < t < 0.95:
        shade *= 0.7                                                # a sheeting joint
    if t > 0.93:
        shade = max(shade, 0.98)
    return tuple(int(min(255, ch * shade)) for ch in colour)


# --------------------------------------------------------------------------
# Travertine terraces: matte calcite steps holding shallow, milky pools, each
# spilling over a rounded drape to the step below.
# --------------------------------------------------------------------------

TRAVERTINE_PATCH = [lambda r: octaves(r, 2, 3), lambda r: octaves(r, 2, 2), lambda r: octaves(r, 6, 2)]
TRAVERTINE_DETAIL = [lambda r: octaves(r, 1, 3, CELL), lambda r: octaves(r, 1, 2, CELL), lambda r: octaves(r, 3, 2, CELL)]


def travertine_surface(colours, interior, filled):
    patches = [_patches("travertine", TRAVERTINE_PATCH, c) for c in (0, 1)]
    detail = _details("travertine", TRAVERTINE_DETAIL, interior) if filled else None
    level_field, wet, tone = (blend(colours, [patches[0][i], patches[1][i]], detail[i] if detail else None)
                              for i in range(3))
    t = level_field * 3.2
    lv = np.floor(t)
    f = t - lv
    drape = smooth(0.70, 1.0, f) ** 0.8
    lip = smooth(0.0, 0.04, f) * smooth(0.14, 0.06, f)
    relief = 0.12 * (lv + drape) + 0.03 * lip
    n, lam = light(relief, 9)
    rgb = mix((218, 210, 192), (232, 226, 212), tone)
    rgb = np.where((f > 0.68)[..., None], mix(rgb, (204, 180, 146), 0.25), rgb)
    rgb = mix(rgb, (244, 240, 230), lip * 0.6)
    pool = (f > 0.12) & (f < 0.6) & (wet > 0.55)
    depth = np.clip((f - 0.12) / 0.5, 0, 1)
    water = mix((196, 214, 206), (164, 194, 192), depth)        # matte, milky: no glint
    rgb = np.where(pool[..., None], water, rgb)
    rgb = rgb * np.clip(0.94 + 0.6 * (lam - LIGHT[2]), 0.6, 1.08)[..., None]
    return images(rgb, 0.4 + 0.5 * tone, 0.5 + 0.8 * (tone - 0.5), wet)


def travertine_wall(t, c, g):
    colour = tuple(int(a + (b - a) * t) for a, b in zip((188, 156, 116), (226, 214, 190)))
    shade = 0.84 + 0.28 * (c - 0.5)
    if g > 0.62:
        colour = tuple(int(a * 0.7 + b * 0.3) for a, b in zip(colour, (176, 122, 70)))   # ochre staining
    return tuple(int(min(255, ch * shade)) for ch in colour)


SURFACES = {
    "sandstone_benches": (sandstone_surface, sandstone_wall),
    "obsidian_ridge": (obsidian_surface, obsidian_wall),
    "granite_tors": (granite_surface, granite_wall),
    "travertine_terraces": (travertine_surface, travertine_wall),
}
