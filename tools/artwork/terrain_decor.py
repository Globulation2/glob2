#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise the raised decor sprites drawn over obstacle terrain.

Obstacle terrain (boulders, hedge, thicket, cliffs, ridge rock, outcrop) is drawn in
two layers, like forest: the ground material in the terrain compositor, then a
decor sprite per cell drawn with the resources in screen-row order, so objects
overlap their neighbours and occlude each other. This tool renders those
sprites from scratch.

Object sprites are 64x64 native (256x256 HD), centred on a 32x32 cell, except
the tall boulders, which are 128x128 native (512x512 HD). The runtime keeps a
tall frame's bottom edge where a 64x64 frame's would be, sixteen native pixels
below the cell, so a tall frame grows upward: its cell is the 32x32 square 48
pixels from either side and 16 from the bottom. Boulders rise well over a cell
into the rows above and cast long shadows to the lower right. Connected
foliage uses cropped 32x64 native frames (128x256 HD): a raised canopy whose
long shadow falls on the open ground south of it; cliffs and the other rock
masses use the same layout in 32x68 frames, raised twenty native pixels above
walls of rock. Smaller objects stand on the
lower part of their cell and rise up to sixteen native pixels into the cell
above, as trees do; drawn back to front, interior cells overlap into one
continuous mass. A frame
holds a small cluster of objects in the game's soft three-quarter view: a lit
top, a darker front face below it, top-left light and a soft contact shadow to
the lower right, drawn back to front. Frames come in two sizes: `full` for
cells inside an obstacle region and `edge` (smaller, pulled toward the cell
centre) for cells with open neighbours; the runtime picks by corner count.
Hedge and thicket additionally have connected corner-mask surfaces: their
interiors have no per-cell front face or ground shadow. Only the exposed
contour is shaded, and leaf strokes inherit soft overlapping crown lighting.
Cliffs share that corner-mask layout as a raised rock plateau whose exposed
fronts drop as fractured walls.

Usage (pinned encoder interpreter, as for terrain_synth.py):

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/terrain_decor.py                  # write all frames + HD
    "$ENC" tools/artwork/terrain_decor.py --sheet artifacts/terrain/decor.png

All sets share one sprite, `content/data/gfx/terrain-decorN.png`, with set k at
frames 12k..12k+11 for the first five sets in `ORDER`, followed by connected
hedge/thicket frames (16 corner masks, a 4x4 continuous texture block each);
later sets append their object and connected frames after those, so the runtime batches every decor draw like
resources. HD frames are registered through tools/artwork/highres_pack.py and
provenance is written to `content-src/gfx/terrain-decor/provenance.json`.
`--write-catalog` adds the `decor` block to each obstacle material file in
content/data/terrain/materials/.
Python 3 with Pillow and NumPy (development-only dependencies); the canopy and
connected-mass surfaces, their walls and the object canvas (domes, faceted
rocks, shadows) are computed as arrays, operation for operation as the
per-pixel loops they replace (`math.hypot` and Python's float power where
NumPy rounds differently), so frames stay byte-identical.
"""
import argparse
import concurrent.futures
import hashlib
import functools
import json
import math
import os
import random
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import PIL
from PIL import Image, ImageDraw, ImageFilter, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parent))
from material_tiles import ROOT, fnv1a32, pixel_sha256  # noqa: E402

NATIVE = 64          # native frame size; the cell is the central 32x32
SCALE = 4
S = NATIVE * SCALE   # render size
CELL = 32 * SCALE
PAD = (S - CELL) // 2
CY = S - CELL        # render row of the cell centre (S / 2 for 64x64 frames)
# Sets whose full and edge frames are taller than 64x64 native.
TALL = {"boulders": 128}


def native_size(name):
    return TALL.get(name, 64)


def use_frame(native):
    """Select the frame geometry the object primitives render into."""
    global NATIVE, S, PAD, CY
    NATIVE, S = native, native * SCALE
    PAD = (S - CELL) // 2
    CY = S - CELL


def offset_x(width):
    """TerrainVisual::decorOffsetX: native pixels a frame starts left of its cell."""
    return (width - 32) // 2


def offset_y(height):
    """TerrainVisual::decorOffsetY: frames up to 64 tall are centred; taller
    frames keep a 64x64 frame's bottom edge and grow upward."""
    return max((height - 32) // 2, height - 48)
FULL, EDGE = 8, 4    # frames per size class: full 0..7, edge 8..11
FRAMES = FULL + EDGE
PREFIX = "content/data/gfx/terrain-decor"
# tileset.json names the sprite by its runtime path, under the data search root.
SPRITE = "data/gfx/terrain-decor"
HD_PREFIX = "content/data/highres/v1/terrain-decor"
HD_CATEGORY = "procedural-materials"
HD_RECIPE = "procedural terrain decor v1"
LIGHT = (-0.62, -0.78)  # toward the upper left, screen space
SHADOW = (0.55, 0.42)   # contact shadow offset direction (lower right)
# The same light in 3D for faceted rock: x right, y south, z up; unit length.
LIGHT3 = tuple(c / math.hypot(LIGHT[0], LIGHT[1], 0.9) for c in (LIGHT[0], LIGHT[1], 0.9))


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def smoothstep(a, b, v):
    t = clamp((v - a) / (b - a)) if b != a else (1.0 if v >= b else 0.0)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def mix3(a, b, t):
    return tuple(lerp(x, y, t) for x, y in zip(a, b))


# Array forms of the helpers above, for the connected masses: each repeats the
# scalar arithmetic in the same order, so results match bit for bit.


def smooth_field(a, b, v):
    """`smoothstep` over an array."""
    if b == a:
        return np.where(v >= b, 1.0, 0.0)
    t = np.clip((v - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def mix_field(a, b, t):
    """`mix3` over (..., 3) colour arrays (or colours) and a weight array."""
    a, b = np.asarray(a, dtype=np.float64), np.asarray(b, dtype=np.float64)
    return a + (b - a) * np.asarray(t, dtype=np.float64)[..., None]


def ramp_field(stops, t):
    """`ramp` over an array: (..., 3) colours."""
    out = np.empty(t.shape + (3,))
    done = np.zeros(t.shape, dtype=bool)
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        take = ~done & (t <= t1)
        colour = mix_field(c0, c1, np.clip((t - t0) / (t1 - t0), 0.0, 1.0))
        out[take] = colour[take]
        done |= take
    out[~done] = stops[-1][1]
    return out


def image_data(image):
    """An L or RGB image as an array indexed [y, x] (or [y, x, channel])."""
    return np.asarray(image).astype(np.int64)


def from_data(values):
    """Clamp to 0..255, truncate as `int` does and make an L or RGB image."""
    return Image.fromarray(np.clip(values, 0, 255).astype(np.uint8))


class Noise:
    """Small value-noise field over the render canvas (not periodic)."""

    def __init__(self, rng, cells):
        self.cells = cells
        self.grid = [[rng.random() for _ in range(cells + 2)] for _ in range(cells + 2)]
        self.table = np.array(self.grid)

    def __call__(self, x, y):
        """The noise at a point, or at arrays of points."""
        fx, fy = x / S * self.cells, y / S * self.cells
        if isinstance(fx, np.ndarray) or isinstance(fy, np.ndarray):
            fx, fy = np.broadcast_arrays(np.asarray(fx, dtype=np.float64), np.asarray(fy, dtype=np.float64))
            ix, iy = fx.astype(np.int64), fy.astype(np.int64)
            tx, ty = fx - ix, fy - iy
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            g = self.table
            a = lerp(g[iy, ix], g[iy, ix + 1], tx)
            b = lerp(g[iy + 1, ix], g[iy + 1, ix + 1], tx)
            return lerp(a, b, ty)
        ix, iy = int(fx), int(fy)
        tx, ty = fx - ix, fy - iy
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        g = self.grid
        a = lerp(g[iy][ix], g[iy][ix + 1], tx)
        b = lerp(g[iy + 1][ix], g[iy + 1][ix + 1], tx)
        return lerp(a, b, ty)


def fbm(rng, base, octaves=3):
    layers = [Noise(rng, base * (2 ** o)) for o in range(octaves)]
    weights = [0.5 ** o for o in range(octaves)]
    total = sum(weights)
    return lambda x, y: sum(w * n(x, y) for w, n in zip(weights, layers)) / total


class Canvas:
    """Straight RGBA float canvas composited back to front.

    `over` and `darken` take one pixel index or an array of distinct indices
    (with per-pixel colours and amounts), each pixel computed as alone."""

    def __init__(self):
        self.rgb = np.zeros((S * S, 3))
        self.a = np.zeros(S * S)
        # Coverage by object bodies alone, so a shadow can tell an object it
        # darkens from earlier shadow on the ground it should deepen.
        self.body = np.zeros(S * S)

    def over(self, i, color, alpha, body=True):
        i = np.atleast_1d(np.asarray(i, dtype=np.int64))
        alpha = np.broadcast_to(np.asarray(alpha, dtype=np.float64), i.shape)
        color = np.broadcast_to(np.asarray(color, dtype=np.float64), i.shape + (3,))
        keep = alpha > 0
        if not keep.all():
            i, alpha, color = i[keep], alpha[keep], color[keep]
        if not len(i):
            return
        if body:
            self.body[i] = alpha + self.body[i] * (1 - alpha)
        a0 = self.a[i]
        out = alpha + a0 * (1 - alpha)
        keep = out > 0
        if not keep.all():
            i, alpha, color, a0, out = i[keep], alpha[keep], color[keep], a0[keep], out[keep]
        c0 = self.rgb[i]
        self.rgb[i] = (color * alpha[:, None] + c0 * a0[:, None] * (1 - alpha)[:, None]) / out[:, None]
        self.a[i] = out

    def darken(self, i, amount):
        """Multiply what is already there (shadow cast onto earlier objects)."""
        i = np.atleast_1d(np.asarray(i, dtype=np.int64))
        amount = np.broadcast_to(np.asarray(amount, dtype=np.float64), i.shape)
        keep = (amount > 0) & (self.a[i] > 0)
        i, amount = i[keep], amount[keep]
        self.rgb[i] = self.rgb[i] * (1 - amount)[:, None]

    def image(self):
        # Fade the outer few render pixels so soft shadows never end in a
        # hard line at the sprite frame.
        index = np.arange(S * S)
        x, y = index % S, index // S
        edge = np.minimum(np.minimum(x, y), np.minimum(S - 1 - x, S - 1 - y))
        near = edge < 12
        self.a[near] = self.a[near] * smooth_field(0, 12, edge[near].astype(np.float64))
        rgba = np.concatenate([np.clip(self.rgb / 255, 0.0, 1.0), np.clip(self.a, 0.0, 1.0)[:, None]], axis=1)
        data = np.floor(rgba * 255 + 0.5).astype(np.uint8).reshape(S, S, 4)
        return Image.fromarray(data)


# --------------------------------------------------------------------------
# Object primitives. Each returns a list of (index, height in 0..1) for its
# silhouette; shading is shared.
# --------------------------------------------------------------------------


def lumpy_outline(rng, harmonics=4, amount=0.12):
    """The outline radius at an angle (a float or an array of angles)."""
    lumps = [(h, amount * rng.uniform(0.3, 1.0) / h ** 0.6, rng.uniform(0, math.tau))
             for h in range(2, 2 + harmonics)]

    def outline(angle):
        if isinstance(angle, np.ndarray):
            total = 0
            for h, a, p in lumps:
                total = total + a * np.sin(h * angle + p)
            return 1 + total
        return 1 + sum(a * math.sin(h * angle + p) for h, a, p in lumps)
    return outline


def hypot_field(dx, dy):
    """`math.hypot` over arrays; NumPy's hypot rounds differently in the last bit."""
    dx, dy = np.broadcast_arrays(np.asarray(dx, dtype=np.float64), np.asarray(dy, dtype=np.float64))
    return np.fromiter(map(math.hypot, dx.ravel().tolist(), dy.ravel().tolist()),
                       dtype=np.float64, count=dx.size).reshape(dx.shape)


def pow_field(values, exponent):
    """Python's float power over an array: NumPy squares rather than calling pow."""
    values = np.asarray(values, dtype=np.float64)
    return np.fromiter(map(math.pow, values.ravel().tolist(), [exponent] * values.size),
                       dtype=np.float64, count=values.size).reshape(values.shape)


def dome(cx, cy, rx, ry, outline, power=0.6):
    """Height field of a lumpy elliptical dome: arrays (i, h, dx, dy, d) of
    its pixels in row order."""
    reach = 1.35
    ys = np.arange(max(0, int(cy - ry * reach)), min(S, int(cy + ry * reach) + 1))
    xs = np.arange(max(0, int(cx - rx * reach)), min(S, int(cx + rx * reach) + 1))
    dx, dy = np.broadcast_arrays((xs + 0.5 - cx)[None, :] / rx, (ys + 0.5 - cy)[:, None] / ry)
    d = hypot_field(dx, dy) / outline(np.arctan2(dy, dx))
    inside = d < 1
    d = d[inside]
    h = pow_field(1 - d * d, power)
    index = (ys[:, None] * S + xs[None, :])[inside]
    return index, h, dx[inside], dy[inside], d


def polygon_mask(points, blur=0.0):
    """Supersampled polygon coverage as {index: coverage}."""
    img = Image.new("L", (S * 2, S * 2), 0)
    ImageDraw.Draw(img).polygon([(x * 2, y * 2) for x, y in points], fill=255)
    if blur:
        img = img.filter(ImageFilter.GaussianBlur(blur * 2))
    img = img.resize((S, S), Image.Resampling.BOX)
    data = img.getdata()
    return {i: v / 255 for i, v in enumerate(data) if v}


def shadow_on(canvas, image, strength, darken, cover, colour):
    """Lay a blurred shadow image on the canvas: darken the objects it falls on
    by `darken` of its strength and cover open ground with `colour` by `cover`."""
    values = image_data(image).ravel()
    i = np.flatnonzero(values)
    a = strength * values[i] / 255
    b = canvas.body[i]
    canvas.darken(i, a * darken * b)
    canvas.over(i, colour, a * cover * (1 - b), body=False)


def cast_shadow(canvas, footprint, strength, spread, offset):
    """Soft contact shadow of a footprint (indices, coverage), offset to the lower right."""
    index, coverage = footprint
    values = np.zeros(S * S, dtype=np.uint8)
    values[index] = (coverage * 255).astype(np.uint8)
    img = Image.fromarray(values.reshape(S, S))
    img = img.transform(img.size, Image.Transform.AFFINE,
                        (1, 0, -offset * SHADOW[0], 0, 1, -offset * SHADOW[1]))
    img = img.filter(ImageFilter.GaussianBlur(spread))
    shadow_on(canvas, img, strength, 0.8, 0.85, (12, 14, 10))


def ground_shadow(canvas, bx, by, rx, ry, strength=0.45):
    """Soft elliptical shadow on the ground at an object's base, thrown to the
    lower right (light from the upper left)."""
    img = Image.new("L", (S, S), 0)
    cx, cy = bx + rx * 0.35, by + ry * 0.25
    ImageDraw.Draw(img).ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=255)
    img = img.filter(ImageFilter.GaussianBlur(max(2.0, ry * 0.35)))
    # Darken the objects it falls on; deepen shadow already on the ground
    # rather than stopping at its faint rim.
    shadow_on(canvas, img, strength, 0.6, 0.8, (14, 16, 12))


def shade_dome(canvas, pixels, color_at, *, light=0.55, rim=0.35, front=0.0, face_color=None,
               edge_soft=0.08):
    """Lambert-ish shading of a dome from its implicit normal, painted over."""
    lx, ly = LIGHT
    i, h, dx, dy, d = pixels
    nz = np.maximum(h, 0.05)
    nx, ny = dx * (1 - h) * 1.4, dy * (1 - h) * 1.4
    norm = np.sqrt(nx * nx + ny * ny + nz * nz)
    lam = (-(nx * lx + ny * ly) + 0.55 * nz) / norm
    x, y = i % S, i // S
    base = np.stack([np.broadcast_to(np.asarray(c, dtype=np.float64), i.shape) for c in color_at(x, y, h)], -1)
    value = 0.62 + light * lam
    if front:
        # Three-quarter view: the lower face turns toward the viewer and
        # away from the light, reading as the object's side.
        lower = dy > 0
        t = smooth_field(0.15, 0.85, dy) * front
        face = base if face_color is None else face_color
        base = np.where(lower[:, None], mix_field(base, face, t), base)
        value = np.where(lower, value * (1 - 0.35 * t), value)
    # Edge darkening only on the side turned away from the light, so an
    # object never sits in a dark ring.
    away = np.maximum(0.0, (dx * -lx + dy * -ly) / np.maximum(1e-6, hypot_field(dx, dy)))
    value = value * (1 - rim * smooth_field(0.8, 1.0, d) * away)
    alpha = 1 - smooth_field(1 - edge_soft, 1.0, d)
    canvas.over(i, base * value[:, None], alpha)


# --------------------------------------------------------------------------
# Materials
# --------------------------------------------------------------------------


@dataclass
class Spec:
    name: str
    render: object = field(repr=False)
    note: str = ""


SPECS = {}


def spec(name, note=""):
    def register(fn):
        SPECS[name] = Spec(name, fn, note)
        return fn
    return register


def layout(rng, count, radius_range, edge, spread=None, extent=(1.35, 1.6)):
    """Object centres and radii inside the cell, sorted back to front.

    `extent` is the object's reach (incl. shadow) in radii horizontally and
    vertically; centres are clamped so nothing is cut by the sprite frame.
    """
    spread = spread if spread is not None else (0.22 if edge else 0.36)
    items = []
    for k in range(count):
        r = rng.uniform(*radius_range) * (0.9 if edge else 1.0)
        lo_x, hi_x = extent[0] * r + 4, S - extent[0] * r - 4
        lo_y, hi_y = extent[1] * r * 0.8 + 4, S - extent[1] * r - 4
        for _ in range(40):
            cx = clamp(S / 2 + rng.uniform(-1, 1) * CELL * spread, lo_x, max(lo_x, hi_x))
            # Bases sit a little low in the cell so the raised bodies rise into it.
            cy = clamp(S / 2 + CELL * 0.22 + rng.uniform(-1, 1) * CELL * spread * 0.6, lo_y, max(lo_y, hi_y))
            # Keep clusters overlapping but not stacked exactly.
            if all(math.hypot(cx - x, cy - y) > 0.55 * (r + rr) for x, y, rr in items):
                break
        items.append((cx, cy, r))
    return sorted(items, key=lambda t: t[1] + t[2] * 0.6)


def stone_palette(rng):
    tones = [(150, 146, 138), (132, 128, 122), (158, 148, 128), (120, 118, 116), (146, 136, 120)]
    base = rng.choice(tones)
    j = rng.uniform(-10, 10)
    return tuple(c + j + rng.uniform(-5, 5) for c in base)


def facet_rock(rng, cx, cy, r, squash=0.8, height=0.95):
    """A convex faceted rock: the soft minimum of jittered sloped planes and a
    tilted top, so the outline is angular and each facet catches the light on
    its own. Planes facing the viewer (down-screen) are steeper, giving the
    rock a narrow front face in the three-quarter view.

    Returns arrays (i, alpha, nx, ny, nz, rim) of its silhouette, where rim is
    how far toward the outline the pixel lies (0 inside, 1 at the edge)."""
    base = rng.uniform(0, math.tau)
    facets = rng.randint(6, 9)
    planes = []
    for k in range(facets):
        a = base + k * math.tau / facets + rng.gauss(0, 0.22)
        reach = rng.uniform(0.8, 1.12)
        steep = 1 + 1.8 * max(0.0, math.sin(a)) ** 2
        planes.append((steep, math.cos(a) / reach, math.sin(a) / reach))
    top = rng.uniform(0.6, 0.78)
    tilt = (rng.uniform(-0.2, -0.04), rng.uniform(-0.18, -0.02))
    soft = 0.05
    ry = r * squash
    ys = np.arange(max(0, int(cy - ry * 1.4)), min(S, int(cy + ry * 1.4) + 1))
    xs = np.arange(max(0, int(cx - r * 1.4)), min(S, int(cx + r * 1.4) + 1))
    u, v = np.broadcast_arrays(((xs + 0.5 - cx) / r)[None, :], ((ys + 0.5 - cy) / ry)[:, None])
    index = ys[:, None] * S + xs[None, :]
    hs = [(s * (1 - (u * pu + v * pv)), -s * pu, -s * pv) for s, pu, pv in planes]
    hs.append((top + tilt[0] * u + tilt[1] * v, tilt[0], tilt[1]))
    m = np.minimum.reduce([h for h, _, _ in hs])
    keep = m > 0
    m, index = m[keep], index[keep]
    hs = [(h[keep], gu, gv) for h, gu, gv in hs]
    ws = [np.exp(-(h - m) / soft) for h, _, _ in hs]
    total = 0
    for w in ws:
        total = total + w
    h = m - soft * np.log(total)
    keep = h > 0
    h, index, total = h[keep], index[keep], total[keep]
    ws = [w[keep] for w in ws]
    gu = gv = 0
    for w, (_, pu, pv) in zip(ws, hs):
        gu = gu + w * pu
        gv = gv + w * pv
    gu, gv = gu / total, gv / total
    nx, ny, nz = -height * gu, -height * gv / squash, 1.0
    n = np.sqrt(nx * nx + ny * ny + nz * nz)
    return index, np.clip(h / 0.03, 0.0, 1.0), nx / n, ny / n, nz / n, 1 - smooth_field(0, 0.14, h)


def place_rocks(rng, sizes, edge):
    """Base points for rocks of the given radii, largest first, around a
    cluster centre that wanders off the cell centre so neighbouring cells do
    not line up in rows. Sorted back to front by base."""
    spread = 0.24 if edge else 0.42
    wander = 0.12 if edge else 0.2
    ox = S / 2 + rng.uniform(-1, 1) * CELL * wander
    oy = CY + CELL * 0.04 + rng.uniform(-1, 1) * CELL * wander
    items = []
    for r, kind in sorted(sizes, key=lambda t: -t[0]):
        ry = r * 0.8
        lo_x, hi_x = r * 1.45 + 6, S - r * 1.45 - 6
        # Bases stay on the cell: the bodies rise from it and the long
        # shadows need the frame's room to the lower right.
        lo_y, hi_y = ry * 2.0 + 8, min(S - ry * 0.9 - 16, CY + CELL * 0.3)
        for _ in range(60):
            cx = clamp(ox + rng.uniform(-1, 1) * CELL * spread, lo_x, hi_x)
            cy = clamp(oy + rng.uniform(-1, 1) * CELL * spread * 0.9, lo_y, hi_y)
            # Rocks may lean on each other but not sink into one another.
            if all(math.hypot(cx - x, (cy - y) * 1.2) > 0.72 * (r + rr) for x, y, rr, _ in items):
                break
        items.append((cx, cy, r, kind))
    return sorted(items, key=lambda t: t[1])


def boulder_sizes(rng, edge):
    """A size hierarchy: one dominant rock or a few medium ones, plus rubble."""
    def rubble(n, lo=10, hi=18):
        return [(rng.uniform(lo, hi), "rubble") for _ in range(n)]
    if edge:
        rocks = [(rng.uniform(30, 40), "rock")] + [(rng.uniform(18, 26), "rock")] * rng.randint(0, 1)
        return rocks + rubble(rng.randint(1, 2), 9, 15)
    kind = rng.choice(("anchor", "anchor", "pair", "cluster", "heap"))
    if kind == "anchor":
        return [(rng.uniform(56, 66), "rock"), (rng.uniform(28, 36), "rock")] + rubble(rng.randint(2, 3))
    if kind == "pair":
        return [(rng.uniform(42, 52), "rock") for _ in range(2)] + rubble(rng.randint(1, 3))
    if kind == "cluster":
        return [(rng.uniform(34, 42), "rock") for _ in range(3)] + rubble(rng.randint(1, 2))
    return [(rng.uniform(26, 36), "rock") for _ in range(4)] + rubble(rng.randint(1, 2))


def boulder_palette(rng):
    """Warm greys that sit with the dusty ground rather than turning blue
    against it."""
    tones = [(142, 136, 126), (126, 121, 114), (152, 142, 124), (116, 112, 106), (138, 130, 118)]
    return rng.choice(tones)


# Long cast shadows fall to the lower right from a low sun; a little flatter
# than the contact shadows so they reach across the open ground.
LONG = tuple(c / math.hypot(0.9, 0.43) for c in (0.9, 0.43))


def mask_image(pixels, threshold=0.0):
    """An "L" image of silhouette pixels: arrays (i, alpha, ...)."""
    index, alpha = pixels[0], pixels[1]
    keep = alpha > threshold
    values = np.zeros(S * S, dtype=np.uint8)
    values[index[keep]] = (alpha[keep] * 255 + 0.5).astype(np.uint8)
    return Image.fromarray(values.reshape(S, S))


def translate(image, dx, dy):
    return image.transform(image.size, Image.Transform.AFFINE, (1, 0, -dx, 0, 1, -dy))


def sweep(mask, length, direction, taper):
    """The ground shadow of an upright prism: its footprint swept along the
    light, fading with distance from the object."""
    out = Image.new("L", mask.size)
    steps = max(1, int(length / 3))
    for k in range(steps + 1):
        t = k / steps
        moved = translate(mask, t * length * direction[0], t * length * direction[1])
        out = ImageChops.lighter(out, moved.point(lambda v, f=1 - taper * t: int(v * f)))
    return out


def boulder_height(rng, r, kind, edge):
    """How far a boulder's top stands above its base, in render pixels."""
    if kind == "rubble":
        lo, hi = 0.3, 0.45
    elif r > 50:
        lo, hi = 1.9, 2.6    # the anchor stands well over a cell above its base
    elif r > 40:
        lo, hi = 1.7, 2.3
    elif r > 32:
        lo, hi = 1.6, 2.2
    else:
        lo, hi = 1.5, 2.1
    return r * rng.uniform(lo, hi) * (0.75 if edge else 1.0)


@spec("boulders", "tall faceted boulders casting long shadows, three-quarter view")
def render_boulders(rng, edge):
    """Each boulder is a faceted, lit top on a tall front face: the top's
    outline extruded down to the ground. Its footprint, swept along the light,
    gives the long shadow, drawn on the ground before any stone."""
    canvas = Canvas()
    k = S // 256
    grit = fbm(rng, 12 * k, 3)
    lichen = fbm(rng, 18 * k, 2)
    cracks = fbm(rng, 10 * k, 2)
    family = boulder_palette(rng)  # one rock type per frame, varied per stone
    owner = np.full(S * S, -1, dtype=np.int64)
    rocks = []
    for bx, by, r, kind in place_rocks(rng, boulder_sizes(rng, edge), edge):
        squash = rng.uniform(0.55, 0.7)  # the top, seen obliquely
        h = min(boulder_height(rng, r, kind, edge), by - r * squash * 1.4 - 16)
        top = facet_rock(rng, bx, by - h, r, squash, 0.7 if kind == "rubble" else 0.95)
        rocks.append((bx, by, r, kind, squash, h, top))
    # Long shadows first: they lie on the ground under every stone.
    shadows = Image.new("L", (S, S))
    for bx, by, r, kind, squash, h, top in rocks:
        footprint = translate(mask_image(top), 0, h)
        blur = max(3.0, r * 0.08)
        length = min(h * 1.15, (S - 16 - blur * 2 - (by + r * squash)) / LONG[1],
                     (S - 16 - blur * 2 - (bx + r)) / LONG[0])
        shade = sweep(footprint, max(0.0, length), LONG, 0.55).filter(ImageFilter.GaussianBlur(blur))
        shadows = ImageChops.lighter(shadows, shade)
    values = image_data(shadows).ravel()
    lying = np.flatnonzero(values)
    canvas.over(lying, (12, 14, 10), 0.6 * values[lying] / 255, body=False)
    for n, (bx, by, r, kind, squash, h, top) in enumerate(rocks):
        ground_shadow(canvas, bx + r * 0.1, by, r * 1.05, r * squash * 0.42, 0.55)
        j = rng.uniform(-9, 9)
        tone = tuple(c + j + rng.uniform(-2, 2) for c in family)
        face_colour = (tone[0] * 0.5, tone[1] * 0.5, tone[2] * 0.52)
        speck = random.Random(rng.random())
        # Front face: below each column of the top, down to the ground. Each
        # stretch of the top's front outline is one flat face, lit by the
        # light's horizontal part as the outline turns, so the facets carry
        # down the body.
        columns = {}
        for i, alpha in zip(top[0].tolist(), top[1].tolist()):
            if alpha > 0.02:
                x, y = i % S, i // S
                lo, hi, a = columns.get(x, (y, y, 0.0))
                columns[x] = (min(lo, y), max(hi, y) if alpha > 0.5 or x not in columns else hi, max(a, alpha))
        foot = fbm(random.Random(rng.random()), 6 * k, 2)
        faces = {}
        for x, (lo, hi, coverage) in columns.items():
            before = columns.get(x - 2, columns.get(x - 1, (lo, hi, 0)))[1]
            after = columns.get(x + 2, columns.get(x + 1, (lo, hi, 0)))[1]
            slope = (after - before) / 4 / squash
            norm = math.hypot(slope, 1)
            fx, fy = -slope / norm, 1 / norm  # outward horizontal normal
            lam = max(0.0, fx * LIGHT3[0] + fy * LIGHT3[1]) / math.hypot(LIGHT3[0], LIGHT3[1])
            base = h + (foot(x, by) - 0.5) * r * 0.25  # an uneven foot on the ground
            faces[x] = (lo, hi, coverage, lam, base)
        if faces:
            middle = (min(faces) + max(faces) + 1) / 2
            flare = 0.0 if kind == "rubble" else 0.14  # bodies spread toward the ground
            reach = int((max(faces) - min(faces)) * flare / 2) + 2
            bottom = min(S, int(max(f[1] + f[4] for f in faces.values())) + 2)
            # Every face pixel at once, as the per-column loop found them:
            # columns of the top as arrays over their x range.
            first, last = min(faces), max(faces)
            found = np.zeros(last - first + 1, dtype=bool)
            f_lo, f_hi = np.zeros(len(found), dtype=np.int64), np.zeros(len(found), dtype=np.int64)
            f_cov, f_lam, f_base = np.zeros(len(found)), np.zeros(len(found)), np.zeros(len(found))
            for x, (lo, hi, coverage, lam, base) in faces.items():
                col = x - first
                found[col], f_lo[col], f_hi[col], f_cov[col], f_lam[col], f_base[col] = True, lo, hi, coverage, lam, base

            def face_at(source):
                col = source.astype(np.int64) - first
                inside = (col >= 0) & (col < len(found))
                col = np.where(inside, col, 0)
                return inside & found[col], col

            # Pixels in the loop's order: column by column, down each column.
            xs = np.arange(max(0, first - reach), min(S, last + reach + 1))[:, None]
            ys = np.arange(min(f[0] for f in faces.values()), bottom)[None, :]
            xs, ys = np.broadcast_arrays(xs, ys)
            # The source column under the top, found through the flare.
            source = middle + (xs + 0.5 - middle)
            active = np.ones(xs.shape, dtype=bool)
            for _ in range(2):
                hit, col = face_at(source)
                active &= hit
                t = np.clip((ys - f_hi[col]) / np.maximum(1.0, f_base[col]), 0.0, 1.0)
                source = np.where(active, middle + (xs + 0.5 - middle) / (1 + flare * t), source)
            hit, col = face_at(source)
            hit &= ys >= f_lo[col]
            hi, base = f_hi[col], f_base[col]
            depth = (ys + 0.5 - hi) / np.maximum(1.0, base)  # 0 under the top, 1 at the ground
            alpha = f_cov[col] * np.clip(1 + base - (ys - hi), 0.0, 1.0)
            hit &= alpha > 0
            x, y, col, depth, alpha = xs[hit], ys[hit], col[hit], depth[hit], alpha[hit]
            lam = f_lam[col]
            g = grit(x, y)
            streak = cracks(x, y * 0.2)
            c = mix_field(face_colour, tone, 0.4 + 0.5 * lam)
            value = (0.68 + 0.55 * lam) * (0.88 + 0.24 * g) * (0.82 + 0.3 * streak)
            value = value * (1 + 0.22 * (1 - smooth_field(0.0, 0.14, depth)))  # a rounded shoulder
            value = value * (1 - 0.32 * smooth_field(0.3, 1.0, depth))  # darker toward the ground
            value = value * np.array([speck.uniform(0.97, 1.03) for _ in range(len(value))])
            i = y * S + x
            canvas.over(i, c * value[:, None], alpha)
            owner[i[alpha > 0.5]] = n
        i, alpha, nx, ny, nz, rim = top
        x, y = i % S, i // S
        lam = np.maximum(0.0, nx * LIGHT3[0] + ny * LIGHT3[1] + nz * LIGHT3[2])
        g = grit(x, y)
        c = mix_field(tone, face_colour, smooth_field(0.25, 0.75, ny) * 0.6)
        moss = lichen(x, y)
        mossy = (nz > 0.8) & (moss > 0.72)
        c = np.where(mossy[:, None], mix_field(c, (128, 128, 92), smooth_field(0.72, 0.82, moss) * 0.3), c)
        value = (0.45 + 0.8 * lam) * (0.86 + 0.28 * g) * np.array([speck.uniform(0.96, 1.04) for _ in range(len(i))])
        # A dark line on the outline turned from the light separates stones.
        away = np.clip(0.5 + 0.5 * (nx * -LIGHT[0] + ny * -LIGHT[1]) / np.maximum(1e-6, hypot_field(nx, ny)),
                       0.0, 1.0)
        value = value * (1 - 0.35 * rim * away)
        canvas.over(i, c * value[:, None], alpha)
        owner[i[alpha > 0.5]] = n
    # Crevices where stones meet, and each stone's shade on the others.
    grid = owner.reshape(S, S)
    right = np.zeros((S, S), dtype=bool)
    right[:, :-1] = (grid[:, 1:] != -1) & (grid[:, 1:] != grid[:, :-1])
    down = np.zeros((S, S), dtype=bool)
    down[:-1] = (grid[1:] != -1) & (grid[1:] != grid[:-1])
    seam = Image.fromarray(np.where((grid >= 0) & (right | down), 255, 0).astype(np.uint8))
    seam = image_data(seam.filter(ImageFilter.GaussianBlur(3.5))).ravel()
    shade = np.zeros(S * S)
    for n, (bx, by, r, *_) in enumerate(rocks):
        mask = Image.fromarray(np.where(grid == n, 255, 0).astype(np.uint8))
        d = r * 0.3
        mask = translate(mask, d * SHADOW[0], d * SHADOW[1])
        v = image_data(mask.filter(ImageFilter.GaussianBlur(r * 0.12))).ravel()
        on = (v != 0) & (owner != -1) & (owner != n)
        shade[on] = np.maximum(shade[on], v[on] / 255)
    stone = np.flatnonzero(owner >= 0)
    canvas.darken(stone, np.minimum(0.7, seam[stone] / 255 * 2.2 * 0.6 + shade[stone] * 0.35))
    return canvas


def foliage(canvas, rng, cx, cy, rx, ry, *, base, light, dark, clump=(7, 11), density=55,
            lumps=0.08, shadow=0.6, rim=0.4):
    """A leafy mass: a shaded body covered in small lit leaf clumps."""
    # (cx, cy) is the base; the leafy body rises above it.
    ground_shadow(canvas, cx, cy, rx * 1.02, ry * 0.42, shadow * 0.8)
    cy = cy - ry * 0.75
    body = dome(cx, cy, rx, ry, lumpy_outline(rng, 6, lumps), power=0.5)
    leaf = fbm(rng, 12, 2)
    shade_dome(canvas, body, lambda x, y, h: tuple(c * (0.7 + 0.4 * leaf(x, y)) for c in base),
               light=0.55, rim=rim, front=0.65, face_color=dark, edge_soft=0.12)
    spots = []
    for _ in range(int(rx * ry / density)):
        a, d = rng.uniform(0, math.tau), math.sqrt(rng.random()) * 0.97
        # Clumps crowd the lit top; the front face shows fewer.
        y0 = cy + math.sin(a) * d * ry
        if y0 > cy + ry * 0.35 and rng.random() < 0.6:
            continue
        spots.append((cx + math.cos(a) * d * rx, y0, rng.uniform(*clump)))
    for x0, y0, rr in sorted(spots, key=lambda t: t[1]):
        # Clumps facing the light are lighter; those low on the front face darker.
        facing = -((x0 - cx) / rx * LIGHT[0] + (y0 - cy) / ry * LIGHT[1])
        low = smoothstep(0.2, 0.9, (y0 - cy) / ry)
        t = clamp(0.5 + 0.5 * facing)
        tint = mix3(base, light, t)
        tint = mix3(tint, dark, low * 0.6)
        tint = tuple(c * rng.uniform(0.92, 1.08) for c in tint)
        pix = dome(x0, y0, rr, rr * 0.88, lumpy_outline(rng, 3, 0.18), power=0.6)
        shade_dome(canvas, pix, lambda x, y, h, c=tint: c, light=0.75, rim=0.45, edge_soft=0.25)


@spec("hedge", "a continuous clipped hedge mass that joins its neighbours")
def render_hedge(rng, edge):
    canvas = Canvas()
    g = rng.uniform(-6, 6)
    # One wide, tall mass reaching past the cell sides and into the cell above,
    # so neighbouring hedge cells merge into one canopy when drawn back to front.
    rx = CELL * (0.62 if edge else 0.74) * rng.uniform(0.96, 1.04)
    ry = CELL * 0.5 * rng.uniform(0.96, 1.04)
    bx = S / 2 + rng.uniform(-6, 6)
    by = S / 2 + CELL * 0.3
    foliage(canvas, rng, bx, by, rx, ry,
            base=(62 + g, 120 + g, 48), light=(112 + g, 172 + g, 76), dark=(36, 74, 32),
            clump=(11, 16), density=48, lumps=0.03, rim=0.15)
    return canvas


@spec("thicket", "scrubby bushes with twigs poking through")
def render_thicket(rng, edge):
    canvas = Canvas()
    count = rng.randint(2, 3) if edge else rng.randint(3, 4)
    items = layout(rng, count, (30, 42), edge, extent=(1.4, 1.0))
    for cx, cy, r in items:
        cy = cy - r * 0.45  # twigs grow from the raised crown
        twigs = Image.new("L", (S, S), 0)
        draw = ImageDraw.Draw(twigs)
        for _ in range(rng.randint(3, 5)):
            a = rng.uniform(math.pi * 0.9, math.pi * 2.1)  # mostly upward and sideways
            length = r * rng.uniform(1.05, 1.35)
            x1, y1 = cx + math.cos(a) * length, cy + math.sin(a) * length * 0.85
            draw.line([(cx, cy), (x1, y1)], fill=255, width=3)
            bx, by = lerp(cx, x1, 0.65), lerp(cy, y1, 0.65)
            b = a + rng.choice((-1, 1)) * rng.uniform(0.5, 0.9)
            draw.line([(bx, by), (bx + math.cos(b) * length * 0.3, by + math.sin(b) * length * 0.3)],
                      fill=255, width=2)
        values = image_data(twigs).ravel()
        twig = np.flatnonzero(values)

        bark = (58 + rng.uniform(-6, 6), 44 + rng.uniform(-5, 5), 32)
        canvas.over(twig, bark, values[twig] / 255)
        o = rng.uniform(-8, 8)
        foliage(canvas, rng, cx, cy + r * 0.45, r, r * 1.05, base=(104 + o, 122 + o, 52), light=(176 + o, 182 + o, 92),
                dark=(42, 48, 22), clump=(6, 9), density=48, lumps=0.2, shadow=0.5)
    return canvas


VIEW = (0.0, 0.7071, 0.7071)  # toward the viewer in the three-quarter view


def face_mask(points, inset=0.0):
    """Supersampled coverage of a polygon as arrays (indices, coverage) of its
    covered pixels, in row order; `inset` shrinks it toward its centroid by
    that many pixels."""
    if inset:
        mx = sum(p[0] for p in points) / len(points)
        my = sum(p[1] for p in points) / len(points)
        points = [(x + (mx - x) * min(0.9, inset / max(1e-6, math.hypot(mx - x, my - y))),
                   y + (my - y) * min(0.9, inset / max(1e-6, math.hypot(mx - x, my - y)))) for x, y in points]
    x0 = max(0, int(min(p[0] for p in points)) - 1)
    y0 = max(0, int(min(p[1] for p in points)) - 1)
    x1 = min(S, int(max(p[0] for p in points)) + 2)
    y1 = min(S, int(max(p[1] for p in points)) + 2)
    if x1 <= x0 or y1 <= y0:
        return np.zeros(0, dtype=np.int64), np.zeros(0)
    w, h = x1 - x0, y1 - y0
    img = Image.new("L", (w * 2, h * 2), 0)
    ImageDraw.Draw(img).polygon([((x - x0) * 2, (y - y0) * 2) for x, y in points], fill=255)
    data = image_data(img.resize((w, h), Image.Resampling.BOX)).ravel()
    k = np.flatnonzero(data)
    return (y0 + k // w) * S + x0 + k % w, data[k] / 255


def convex_hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def normalize(v):
    n = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / n for c in v)


def facet_value(normal):
    n = normalize(normal)
    lam = sum(a * b for a, b in zip(n, normalize(LIGHT3)))
    return 0.62 + 0.42 * lam


@dataclass
class Rock:
    """A faceted rock: a convex ground ring raised into walls, each ring vertex
    lifted a little more, and a fan of sloping top facets meeting at a peak.
    Coordinates are ground positions in render pixels; z is height."""
    ring: list    # [(x, y, z)] around the top of the walls
    peak: tuple   # (x, y, z)
    tone: tuple

    def faces(self):
        """Visible faces as (screen polygon, normal, kind), back to front."""
        ring, (px, py, pz) = self.ring, self.peak
        n = len(ring)
        cx = sum(p[0] for p in ring) / n
        cy = sum(p[1] for p in ring) / n
        tops, sides = [], []
        for k in range(n):
            a, b = ring[k], ring[(k + 1) % n]
            ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
            vx, vy, vz = px - a[0], py - a[1], pz - a[2]
            normal = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
            if normal[2] < 0:
                normal = tuple(-c for c in normal)
            screen = [(a[0], a[1] - a[2]), (b[0], b[1] - b[2]), (px, py - pz)]
            tops.append(((a[1] + b[1] + py) / 3, screen, normal))
            # Outward wall normal in the ground plane.
            ox, oy = uy, -ux
            if ox * ((a[0] + b[0]) / 2 - cx) + oy * ((a[1] + b[1]) / 2 - cy) < 0:
                ox, oy = -ox, -oy
            if oy > 0.02 * math.hypot(ox, oy):
                wall = [(a[0], a[1] - a[2]), (b[0], b[1] - b[2]), (b[0], b[1]), (a[0], a[1])]
                sides.append(((a[1] + b[1]) / 2, wall, (ox, oy, 0.0)))
        out = [(s, nrm, "top") for _, s, nrm in sorted(tops)
               if sum(x * y for x, y in zip(normalize(nrm), VIEW)) > 0]
        out += [(s, nrm, "side") for _, s, nrm in sorted(sides)]
        return out

    def silhouette(self):
        pts = [(x, y - z) for x, y, z in self.ring] + [(x, y) for x, y, _ in self.ring]
        pts.append((self.peak[0], self.peak[1] - self.peak[2]))
        return convex_hull(pts)


def make_rock(rng, gx, gy, rx, ry, angle, wall, lift, peak, tone, *, points=8, jag=0.22, peak_offset=0.3):
    ring = []
    c, s = math.cos(angle), math.sin(angle)
    for k in range(points):
        t = math.tau * (k + rng.uniform(-0.3, 0.3)) / points
        r = rng.uniform(1 - jag, 1.0)
        lx, ly = math.cos(t) * rx * r, math.sin(t) * ry * r
        ring.append((gx + lx * c - ly * s, gy + lx * s * 0.6 + ly * c))
    ring = convex_hull(ring)
    ring = [(x, y, wall + rng.uniform(0, lift)) for x, y in ring]
    px = gx + rng.uniform(-1, 1) * rx * peak_offset
    py = gy + rng.uniform(-1, 1) * ry * peak_offset
    return Rock(ring, (px, py, wall + peak), tone)


def draw_rocks(canvas, rng, rocks, *, lichen=None, strata=0.0, shadow=0.45):
    """Shadows first, then every rock back to front: a dark body fill that
    closes any gaps between facets, flat-shaded facets with grit, wall
    strata and a darker foot, and thin seams where facets meet."""
    grit = fbm(rng, 18, 3)
    fine = Noise(rng, 64)
    moss = fbm(rng, 10, 2)
    rocks = sorted(rocks, key=lambda r: sum(p[1] for p in r.ring) / len(r.ring))
    for rock in rocks:
        footprint = face_mask([(x, y) for x, y, _ in rock.ring])
        height = max(z for _, _, z in rock.ring + [rock.peak])
        cast_shadow(canvas, footprint, shadow, 3 + height * 0.04, 4 + height * 0.25)
    for rock in rocks:
        ground = max(y for _, y, _ in rock.ring)
        height = max(z for _, _, z in rock.ring + [rock.peak])
        i, v = face_mask(rock.silhouette())
        canvas.over(i, tuple(c * 0.3 for c in rock.tone), v)
        xs = [x for x, _, _ in rock.ring]
        fissures = [rng.uniform(min(xs), max(xs)) for _ in range(rng.randint(1, 3))]
        inner = np.zeros(S * S)
        for poly, normal, kind in rock.faces():
            value = facet_value(normal) * rng.uniform(0.94, 1.06)
            inner_i, inner_v = face_mask(poly, inset=1.6)
            inner[inner_i] = inner_v
            i, v = face_mask(poly)
            x, y = i % S, i // S
            g = grit(x, y)
            shade = value * (0.84 + 0.3 * g + 0.08 * (fine(x, y) - 0.5))
            if kind == "side":
                # Darker toward the foot, faint bedding lines across the wall.
                foot = smooth_field(ground - height, ground + 4, y.astype(np.float64))
                shade = shade * (1 - 0.28 * foot * foot)
                if strata:
                    shade = shade * (1 - strata * smooth_field(0.75, 0.95, np.sin((y - 0.35 * x) / 4.5 + g * 4)))
                # Vertical joints that wander a little down the face.
                if len(i):
                    gap = np.minimum.reduce([np.abs(x - f - (g - 0.5) * 10) for f in fissures])
                    shade = np.where(gap < 1.4, shade * 0.72, shade)
            color = np.asarray(rock.tone, dtype=np.float64)[None, :] * shade[:, None]
            if lichen and kind == "top":
                m = moss(x, y)
                color = mix_field(color, lichen, smooth_field(0.68, 0.82, m) * 0.35 * (0.6 + 0.4 * value))
            seam = (1 - inner[i]) * (0.22 if kind == "side" else 0.05)
            color = np.where((seam > 0)[:, None], color * (1 - seam)[:, None], color)
            canvas.over(i, color, v)
            inner[inner_i] = 0.0
    return canvas


def outcrop_tone(rng):
    base = mix3((146, 138, 124), (164, 156, 140), rng.random())
    j = rng.uniform(-5, 5)
    return tuple(c + j for c in base)


@spec("ridge_rock", "chunky faceted crags clustered along a shared grain")
def render_ridge_rock(rng, edge):
    canvas = Canvas()
    grain = -0.35 + rng.uniform(-0.25, 0.25)   # the shared strike every crag follows
    gx, gy = S / 2 + rng.uniform(-6, 6), S / 2 + CELL * 0.2
    rocks = []
    count = rng.randint(1, 2) if edge else rng.randint(3, 4)
    for k in range(count):
        t = (k - (count - 1) / 2) / max(1, count - 1)
        ox = t * (44 if edge else 96) + rng.uniform(-8, 8)
        oy = t * (44 if edge else 96) * math.sin(grain) + rng.uniform(-12, 12)
        scale = rng.uniform(0.7, 0.85) if edge else rng.uniform(0.85, 1.1)
        tone = mix3((134, 134, 136), (156, 153, 148), rng.random())
        peak = rng.uniform(56, 80) * scale
        rocks.append(make_rock(rng, gx + ox, gy + oy, 44 * scale, 27 * scale, grain, rng.uniform(4, 8) * scale,
                               peak * 0.4, peak, tone, points=7, jag=0.3, peak_offset=0.45))
    return draw_rocks(canvas, rng, rocks, strata=0.12, shadow=0.42)


@spec("outcrop", "broad stepped bedrock blocks with faceted tops and sparse lichen")
def render_outcrop(rng, edge):
    canvas = Canvas()
    tone = outcrop_tone(rng)
    gx, gy = S / 2 + rng.uniform(-8, 8), S / 2 + CELL * 0.15
    rocks = []
    if edge:
        rx = rng.uniform(44, 54)
        rocks.append(make_rock(rng, gx, gy, rx, rx * 0.72, rng.uniform(-0.5, 0.5), rng.uniform(24, 32), 8,
                               rng.uniform(10, 16), tone, points=8, jag=0.3, peak_offset=0.5))
    elif rng.random() < 0.3:
        # Two blocks split by a joint, the rear one taller.
        rx = rng.uniform(48, 56)
        for k, side in enumerate((-1, 1)):
            rocks.append(make_rock(rng, gx + side * rx * 0.75, gy - (6 if k == 0 else -6) + rng.uniform(-6, 6), rx,
                                   rx * 0.8, rng.uniform(-0.4, 0.4), rng.uniform(30, 44), 8, rng.uniform(10, 16),
                                   outcrop_tone(rng), points=7, jag=0.3, peak_offset=0.5))
    else:
        rx = rng.uniform(66, 86)
        gx += rng.uniform(-10, 10)
        wall = rng.uniform(30, 42)
        rocks.append(make_rock(rng, gx, gy, rx, rx * 0.74, rng.uniform(-0.4, 0.4), wall, 10, rng.uniform(12, 20),
                               tone, points=8, jag=0.3, peak_offset=0.5))
        if rng.random() < 0.6:
            # A smaller block stepped on top of the first, set back from its face.
            rx2 = rx * rng.uniform(0.4, 0.55)
            sx, sy = gx + rng.uniform(-0.3, 0.3) * rx, gy - rng.uniform(0.1, 0.3) * rx
            rocks.append(make_rock(rng, sx, sy - wall, rx2, rx2 * 0.7, rng.uniform(-0.6, 0.6),
                                   rng.uniform(14, 20), 5, rng.uniform(8, 12), outcrop_tone(rng), points=6, jag=0.3,
                                   peak_offset=0.5))
    for _ in range(rng.randint(0, 1) if edge else rng.randint(1, 2)):
        # Fallen rubble at the foot of the face.
        a = rng.uniform(-0.8, 0.8) * math.pi / 2 + math.pi / 2
        d = rx * rng.uniform(0.85, 1.0)
        r = rng.uniform(8, 13)
        rocks.append(make_rock(rng, gx + math.cos(a) * d, gy + math.sin(a) * d * 0.62, r, r * 0.7,
                               rng.uniform(0, math.tau), r * 0.5, 2, r * 0.45, outcrop_tone(rng), points=6))
    return draw_rocks(canvas, rng, rocks, lichen=(118, 128, 76), strata=0.0, shadow=0.45)


@spec("cliffs", "a raised rock plateau whose exposed fronts drop as fractured walls")
def render_cliffs(rng, edge):
    # The decor schema needs object frames for every set; a material with
    # connected frames never draws them (Catalog::decorFrame), so these are
    # the outcrop blocks in the warmer cliff-face stone.
    canvas = Canvas()
    tone = mix3((128, 110, 88), (150, 130, 102), rng.random())
    rx = rng.uniform(40, 50) if edge else rng.uniform(66, 82)
    rocks = [make_rock(rng, S / 2 + rng.uniform(-8, 8), S / 2 + CELL * 0.15, rx, rx * 0.74,
                       rng.uniform(-0.4, 0.4), rng.uniform(36, 48), 8, rng.uniform(10, 16), tone,
                       points=8, jag=0.3, peak_offset=0.5)]
    return draw_rocks(canvas, rng, rocks, strata=0.12, shadow=0.5)


@spec("glacier", "a raised glacier whose exposed fronts drop as blue ice walls")
def render_glacier(rng, edge):
    # Object frames the runtime never draws for a connected set (see
    # render_cliffs): blocks of blue serac ice.
    canvas = Canvas()
    tone = mix3((150, 190, 220), (196, 222, 240), rng.random())
    rx = rng.uniform(40, 50) if edge else rng.uniform(66, 82)
    rocks = [make_rock(rng, S / 2 + rng.uniform(-8, 8), S / 2 + CELL * 0.15, rx, rx * 0.74,
                       rng.uniform(-0.4, 0.4), rng.uniform(36, 48), 8, rng.uniform(10, 16), tone,
                       points=8, jag=0.2, peak_offset=0.5)]
    return draw_rocks(canvas, rng, rocks, strata=0.0, shadow=0.5)


def mass_blocks(tone_lo, tone_hi, wall):
    """Object frames the decor schema requires of a connected mass; connected
    frames replace them in play (Catalog::decorFrame)."""
    def render(rng, edge):
        canvas = Canvas()
        tone = mix3(tone_lo, tone_hi, rng.random())
        rx = rng.uniform(40, 50) if edge else rng.uniform(66, 82)
        rocks = [make_rock(rng, S / 2 + rng.uniform(-8, 8), S / 2 + CELL * 0.15, rx, rx * 0.74,
                           rng.uniform(-0.4, 0.4), wall, 10, rng.uniform(8, 14), tone,
                           points=8, jag=0.25, peak_offset=0.5)]
        return draw_rocks(canvas, rng, rocks, strata=0.1, shadow=0.45)
    return render


spec("sandstone_benches", "flat-topped sandstone benches: a pale caprock on thick, stepped beds")(
    mass_blocks((210, 160, 112), (232, 196, 150), 44))
spec("obsidian_ridge", "a heap of glassy black obsidian shards with sharp glints")(
    mass_blocks((50, 50, 60), (78, 76, 90), 40))
spec("granite_tors", "rounded, sheeted granite blocks with moss in the hollows")(
    mass_blocks((140, 134, 122), (168, 160, 146), 48))
spec("travertine_terraces", "matte calcite terraces holding shallow milky pools")(
    mass_blocks((214, 204, 184), (232, 224, 208), 34))


class _Rendered:
    """A finished HD frame where a spec renderer returns a Canvas."""

    def __init__(self, image):
        self._image = image

    def image(self):
        return self._image


def _register_outcrops():
    import terrain_outcrops
    for name, (_, note) in terrain_outcrops.SETS.items():
        spec(name, note)(lambda rng, edge, name=name: _Rendered(
            terrain_outcrops.render_object(name, rng.getrandbits(64), edge)))


_register_outcrops()


@spec("icefall", "a glacier icefall: crevassed snow standing on sheer, fluted blue ice walls")
def render_icefall_blocks(rng, edge):
    # Object frames the decor schema requires; connected frames replace them in
    # play (Catalog::decorFrame). Tilted blocks of the same blue ice.
    canvas = Canvas()
    tone = mix3((150, 196, 226), (182, 216, 238), rng.random())
    rx = rng.uniform(40, 50) if edge else rng.uniform(66, 82)
    rocks = [make_rock(rng, S / 2 + rng.uniform(-8, 8), S / 2 + CELL * 0.15, rx, rx * 0.74,
                       rng.uniform(-0.4, 0.4), rng.uniform(40, 52), 10, rng.uniform(8, 14), tone,
                       points=8, jag=0.25, peak_offset=0.5)]
    return draw_rocks(canvas, rng, rocks, strata=0.0, shadow=0.45)


# Connected foliage is a raised surface over its painted footprint, not a set of
# individually lit domes. Crowns are anchored on map vertices: every vertex has
# one of two seeded colours (cornerColours in TerrainMaterials.cpp), and each
# colour owns a cluster of crowns that spreads into the four cells around that
# vertex. A cell's frame therefore depends only on its corner mask and its four
# corner colours, and neighbouring frames agree wherever they meet, so large
# patches continue without seams and without a periodic repeat.
MASS_SETS = ("icefall", "sandstone_benches", "obsidian_ridge", "granite_tors", "travertine_terraces")
CONNECTED = ("hedge", "thicket", "cliffs", "glacier") + MASS_SETS
# Connected sets whose edges stand on open ground ("connected_open"): a cliff's
# rim and walls belong on the grass beside it, where foliage keeps a clear border.
OPEN_EDGED = ("cliffs", "glacier") + MASS_SETS
# Free-standing obstacles that keep clear of open-edged masses ("clear_of_masses"):
# no spire or pillar is drawn on a cell with a mass vertex within this many
# vertices of its corners. 0 skips only cells with a mass corner of their own, so
# spires run up to a cliff's foot; 1 (until 2026-11-11) left a bare strip two
# tiles wide beside every mass.
CLEAR_OF_MASSES = {name: 0 for name in ("boulders", "ridge_rock", "outcrop", "penitentes",
                                         "termite_mounds", "hoodoos", "petrified_stumps")}
CANOPY_VARIANTS = 16          # one per combination of the four corner colours
CANOPY_INTERIORS = 8          # interior crown arrangements of a filled cell
BAND = 20                     # vertex crowns centre this close to a cell edge
HALO = 22                     # height field kept around the cell
REACH = CELL - HALO - 4       # a cluster never reaches another cell's halo
CANOPY_TOP = 64               # frame rows above the cell (16 native)
CANOPY_BELOW = 64             # frame rows below it, for the long shadow (16 native)
CANOPY_LIGHT = (-0.50, -0.62, 0.60)
CANOPY_STYLE = {
    # Shadow, mid and highlight ramps keep the vegetation brighter and more
    # saturated than the flat grass at its lit edges, and cooler in crevices.
    "hedge": dict(
        ramp=((0.0, (8, 34, 26)), (0.28, (18, 72, 38)), (0.52, (36, 116, 46)),
              (0.76, (84, 162, 58)), (1.0, (168, 212, 98))),
        big=(3, 21, 25), medium=(4, 12, 17), sprigs=(3, 8, 11),
        tint=((0.88, (0, 0, 4)), (1.08, (8, 6, -6))),
        lumps=(7, 0.10), dab=(4, 7, 0.05, 40), amplitude=0.30, wood=0,
        # HD pixels the clipped top stands above its footprint (16 native).
        raise_=64),
    "thicket": dict(
        ramp=((0.0, (30, 34, 16)), (0.28, (66, 76, 26)), (0.52, (112, 128, 38)),
              (0.76, (164, 172, 58)), (1.0, (222, 214, 118))),
        big=(3, 19, 22), medium=(4, 11, 16), sprigs=(4, 7, 10),
        tint=((0.88, (-4, 0, 2)), (1.08, (10, 4, -8))),
        lumps=(5, 0.22), dab=(4, 8, 0.07, 50), amplitude=0.32, wood=14,
        raise_=56),
}


CONNECTED_FRAMES = 16 * CANOPY_VARIANTS + CANOPY_VARIANTS * (CANOPY_INTERIORS - 1)
# Where an open-edged mass meets a different connected mass, each corner of a
# cell is the mass's own (1), open ground (0) or the other mass (2), and the
# edge pulls back from the other mass so the two stand apart across a valley
# half a cell wide. Codes are base-3 corner states (TL, TR, BL, BR); only codes
# with both an own and a foreign corner need frames ("connected_foreign").
FOREIGN_CODES = [code for code in range(81)
                 if any(code // 3 ** k % 3 == 1 for k in range(4)) and any(code // 3 ** k % 3 == 2 for k in range(4))]
FOREIGN_FRAMES = len(FOREIGN_CODES) * CANOPY_VARIANTS


def foreign_masks(code):
    """(own mask, foreign mask) of a base-3 corner code."""
    own = sum(1 << k for k in range(4) if code // 3 ** k % 3 == 1)
    foreign = sum(1 << k for k in range(4) if code // 3 ** k % 3 == 2)
    return own, foreign


def connected_first(name):
    if ORDER.index(name) >= LEGACY_SETS:
        return first_frame(name) + FRAMES
    return LEGACY_SETS * FRAMES + CONNECTED.index(name) * CONNECTED_FRAMES


def connected_rise(name):
    """(HD rows above the cell, HD rise of the raised top) of a connected frame:
    cliffs stand taller than foliage."""
    if name in CLIFF_STYLES:
        return CLIFF_TOP, CLIFF_RAISE
    if name in MASS_SETS:
        return CLIFF_TOP, MASSES[name]["lift"]
    return CANOPY_TOP, raise_of(name)


def connected_size(name):
    """HD size of a connected frame: rows above the cell, the cell, and rows
    below it for the long shadow (canopies) or the cast shadow (rock masses).
    Rock frames are taller than 64 native, so the runtime keeps their bottom
    16 native pixels below the cell and they grow upward (decorOffsetY)."""
    if name in CLIFF_STYLES or name in MASS_SETS:
        return CELL, CLIFF_TOP + CELL + CLIFF_BELOW
    return CELL, CELL + 2 * connected_rise(name)[0]


def frame_count(name):
    """Frames a set renders: objects, connected tiles, and valley tiles for open-edged sets."""
    return FRAMES + (CONNECTED_FRAMES if name in CONNECTED else 0) + (FOREIGN_FRAMES if name in OPEN_EDGED else 0)


# Sets numbered before valley tiles existed keep their frames; their valley
# tiles sit in one block after FOREIGN_RETROFIT_AFTER instead, and later sets
# follow that block, so adding a set never renumbers an existing frame.
RETROFIT_FOREIGN = ("cliffs", "glacier")
FOREIGN_RETROFIT_AFTER = "petrified_stumps"


def block_size(name):
    """Frames a set occupies in its own numbered block."""
    retrofit = name in RETROFIT_FOREIGN
    return frame_count(name) - (FOREIGN_FRAMES if retrofit else 0)


def foreign_first(name):
    if name in RETROFIT_FOREIGN:
        return (first_frame(FOREIGN_RETROFIT_AFTER) + block_size(FOREIGN_RETROFIT_AFTER) +
                RETROFIT_FOREIGN.index(name) * FOREIGN_FRAMES)
    return connected_first(name) + CONNECTED_FRAMES


def connected_frame(index):
    """(mask, colours, interior, foreign mask) of a connected frame index past FRAMES.

    Mask-major frames with interior variant 0 come first, then the extra
    interior variants of the filled mask, colour-major, then (open-edged sets)
    the frames against another mass, code-major in FOREIGN_CODES order."""
    if index < 16 * CANOPY_VARIANTS:
        return index // CANOPY_VARIANTS, index % CANOPY_VARIANTS, 0, 0
    index -= 16 * CANOPY_VARIANTS
    if index < CANOPY_VARIANTS * (CANOPY_INTERIORS - 1):
        return 15, index // (CANOPY_INTERIORS - 1), index % (CANOPY_INTERIORS - 1) + 1, 0
    index -= CANOPY_VARIANTS * (CANOPY_INTERIORS - 1)
    own, foreign = foreign_masks(FOREIGN_CODES[index // CANOPY_VARIANTS])
    return own, index % CANOPY_VARIANTS, 0, foreign


def frame_number(name, index):
    if index < FRAMES:
        return first_frame(name) + index
    if index < FRAMES + CONNECTED_FRAMES:
        return connected_first(name) + index - FRAMES
    return foreign_first(name) + index - FRAMES - CONNECTED_FRAMES


def runtime_hash(x, y, salt):
    """TerrainVisual::hash() in TerrainMaterials.cpp."""
    h = (x * 73856093 ^ y * 19349663 ^ salt) & 0xffffffff
    h ^= h >> 13
    h = (h * 0x5bd1e995) & 0xffffffff
    return h ^ h >> 15


def vertex_colour(x, y):
    """One bit of cornerColours() in TerrainMaterials.cpp, terrain seed zero."""
    return runtime_hash(x, y, 0xca70b1) >> 16 & 1


def connected_index(x, y, mask, colours):
    """Connected frame index past FRAMES that the runtime draws for a cell."""
    interior = runtime_hash(x, y, 0x1e7e10) % CANOPY_INTERIORS if mask == 15 else 0
    if not interior:
        return mask * CANOPY_VARIANTS + colours
    return 16 * CANOPY_VARIANTS + colours * (CANOPY_INTERIORS - 1) + interior - 1


def ramp(stops, t):
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            return mix3(c0, c1, clamp((t - t0) / (t1 - t0)))
    return stops[-1][1]


def crown_kernel(rng, style, radius, base, top):
    """One crown: a lumpy dome covered with small leaf clusters.

    Heights are absolute (0..1) so crowns combine by maximum into tiers. Leaf
    clusters near the rim bulge past the outline and scallop the silhouette.
    """
    harmonics, amount = style["lumps"]
    lumps = lumpy_outline(rng, harmonics, amount)
    # Summed harmonics can exceed `amount`; rescale so the crown keeps within
    # its kernel and its cluster's reach.
    peak = float(np.max(lumps(np.arange(720) * math.tau / 720))) - 1
    scale = min(1.0, amount / peak) if peak > 0 else 1.0
    outline = lambda angle: 1 + (lumps(angle) - 1) * scale
    dab_lo, dab_hi, dab_amp, dab_area = style["dab"]
    half = int(radius * (1 + amount)) + dab_hi + 2
    n = half * 2

    def surface(dx, dy):
        d = math.hypot(dx, dy) / radius / outline(math.atan2(dy, dx))
        return base + (top - base) * (1 - d * d) ** 0.55 if d < 1 else None

    offsets = np.arange(n) + 0.5 - half
    dx, dy = np.broadcast_arrays(offsets[None, :], offsets[:, None])
    d = hypot_field(dx, dy) / radius / outline(np.arctan2(dy, dx))
    inside = d < 1
    values = np.zeros((n, n))
    values[inside] = base + (top - base) * (1 - d[inside] * d[inside]) ** 0.55
    for _ in range(int(radius * radius / dab_area) + 3):
        angle, rho = rng.uniform(0, math.tau), math.sqrt(rng.random()) * radius * 1.02
        cx, cy = math.cos(angle) * rho, math.sin(angle) * rho
        under = surface(cx * 0.9, cy * 0.9)
        if under is None:
            continue
        r, amp = rng.uniform(dab_lo, dab_hi), dab_amp * rng.uniform(0.7, 1.2)
        ys = np.arange(max(0, int(cy + half - r)), min(n, int(cy + half + r) + 2))
        xs = np.arange(max(0, int(cx + half - r)), min(n, int(cx + half + r) + 2))
        if not len(xs) or not len(ys):
            continue
        q = 1 - (pow_field(xs[None, :] + 0.5 - half - cx, 2) + pow_field(ys[:, None] + 0.5 - half - cy, 2)) / (r * r)
        box = values[ys[0]:ys[-1] + 1, xs[0]:xs[-1] + 1]
        hit = q > 0
        box[hit] = np.maximum(box[hit], under + amp * q[hit] ** 0.6)
    image = Image.fromarray((np.clip(values, 0.0, 1.0) * 255).astype(np.uint8))
    return image, half


LAYERS = (("big", (0.42, 0.52), (0.88, 0.98)),
          ("medium", (0.30, 0.48), (0.66, 0.86)),
          ("sprigs", (0.22, 0.36), (0.50, 0.66)))


def paste_crown(heights, kernel, half, cx, cy):
    layer = Image.new("L", heights.size)
    layer.paste(kernel, (int(cx) - half, int(cy) - half))
    return ImageChops.lighter(heights, layer)


def draw_wood(rng, wood, count, lo, hi):
    draw = ImageDraw.Draw(wood)
    for _ in range(count):
        x0, y0 = rng.uniform(lo, hi), rng.uniform(lo, hi)
        angle, length = rng.uniform(0, math.tau), rng.uniform(16, 40)
        x1 = clamp(x0 + math.cos(angle) * length, lo, hi)
        y1 = clamp(y0 + math.sin(angle) * length, lo, hi)
        draw.line([(x0, y0), (x1, y1)], fill=255, width=5)
        mx, my = lerp(x0, x1, 0.55), lerp(y0, y1, 0.55)
        twig = angle + rng.choice((-1, 1)) * rng.uniform(0.5, 1.0)
        draw.line([(mx, my), (clamp(mx + math.cos(twig) * length * 0.45, lo, hi),
                              clamp(my + math.sin(twig) * length * 0.45, lo, hi))], fill=170, width=3)


@functools.lru_cache(maxsize=8)
def canopy_cluster(name, colour):
    """Crowns, and thicket branches, along the two cell edges through a vertex.

    Returns height and wood images of 2*REACH pixels centred on the vertex. Crown
    centres lie within BAND of the edge lines, half an edge each way, so
    neighbouring cells agree everywhere near their shared edges.
    """
    style = CANOPY_STYLE[name]
    rng = random.Random(fnv1a32(f"canopy-cluster:{name}:{colour}"))
    size = REACH * 2
    heights, wood = Image.new("L", (size, size)), Image.new("L", (size, size))
    for key, base, top in LAYERS:
        count, lo, hi = style[key]
        # Jittered strata along each arm keep the band evenly covered; random
        # offsets stop their creases lining up from cluster to cluster.
        for arm in range(2):
            offset = rng.uniform(0, CELL)
            for i in range(count):
                radius = rng.uniform(lo, hi)
                kernel, half = crown_kernel(rng, style, radius, rng.uniform(*base), rng.uniform(*top))
                assert half <= REACH - CELL // 2, (name, radius, half)
                along = -CELL / 2 + ((i + rng.random()) * CELL / count + offset) % CELL
                across = rng.uniform(-BAND, BAND)
                cx, cy = (along, across) if arm == 0 else (across, along)
                heights = paste_crown(heights, kernel, half, REACH + cx, REACH + cy)
    if style["wood"]:
        draw_wood(rng, wood, style["wood"] // 2, REACH - BAND, REACH + BAND)
    return heights, wood


@functools.lru_cache(maxsize=16)
def canopy_interior(name, interior):
    """Crowns wholly inside one cell: a seeded arrangement per interior variant.

    They never reach a cell edge, so each filled cell can pick its own variant
    and break the repetition of the two vertex clusters. One to three larger
    crowns give the cell a dominant mass at a random place.
    """
    style = CANOPY_STYLE[name]
    rng = random.Random(fnv1a32(f"canopy-interior:{name}:{interior}"))
    heights, wood = Image.new("L", (CELL, CELL)), Image.new("L", (CELL, CELL))
    for key, base, top in LAYERS:
        count, lo, hi = style[key]
        # Larger crowns land anywhere they fit; smaller ones fill jittered strata
        # so the interior is as evenly covered as the edge bands.
        strata = 1 if key == "big" else 3
        for i in range(rng.randint(1, 3) if key == "big" else strata * strata):
            radius = rng.uniform(lo, hi) * (0.85 if key == "big" else 1)
            kernel, half = crown_kernel(rng, style, radius, rng.uniform(*base), rng.uniform(*top))
            # Stay clear of the edge by more than the coverage blur, so the
            # outline meets the neighbouring frame at the same place.
            margin = half + 12
            span = (CELL - 2 * margin) / strata
            heights = paste_crown(heights, kernel, half,
                                  margin + (i % strata + rng.random()) * span,
                                  margin + (i // strata % strata + rng.random()) * span)
    if style["wood"]:
        draw_wood(rng, wood, style["wood"] // 2, 10, CELL - 10)
    return heights, wood


@functools.lru_cache(maxsize=48)
def canopy_surface(name, colours, interior):
    """Lit surface colour and heights of the cell plus its halo, before masking."""
    style = CANOPY_STYLE[name]
    size = CELL + 2 * HALO
    heights, wood = Image.new("L", (size, size)), Image.new("L", (size, size))
    for k, (vx, vy) in enumerate(((0, 0), (CELL, 0), (0, CELL), (CELL, CELL))):
        cluster_heights, cluster_wood = canopy_cluster(name, (colours >> k) & 1)
        for source, target in ((cluster_heights, 0), (cluster_wood, 1)):
            layer = Image.new("L", (size, size))
            layer.paste(source, (vx + HALO - REACH, vy + HALO - REACH))
            if target:
                wood = ImageChops.lighter(wood, layer)
            else:
                heights = ImageChops.lighter(heights, layer)
    for source, target in zip(canopy_interior(name, interior), (0, 1)):
        layer = Image.new("L", (size, size))
        layer.paste(source, (HALO, HALO))
        if target:
            wood = ImageChops.lighter(wood, layer)
        else:
            heights = ImageChops.lighter(heights, layer)
    hp, wp = image_data(heights), image_data(wood)
    tints = [style["tint"][(colours >> k) & 1] for k in range(4)]
    broad = image_data(heights.filter(ImageFilter.GaussianBlur(8)))
    lx, ly, lz = CANOPY_LIGHT
    norm_l = math.sqrt(lx * lx + ly * ly + lz * lz)
    # Every pixel at once, operation for operation as the per-pixel loop did.
    index = np.arange(size)
    right, left = np.minimum(index + 2, size - 1), np.maximum(index - 2, 0)
    h = hp / 255
    gx = (hp[:, right] - hp[:, left]) / 255 * 18
    gy = (hp[right, :] - hp[left, :]) / 255 * 18
    lam = (-gx * lx - gy * ly + lz) / (np.sqrt(gx * gx + gy * gy + 1) * norm_l)
    # Crevices between crowns, and the low gaps, fall into shade.
    occlusion = np.clip(1 + (h - broad / 255) * 3.2, 0.0, 1.0) * (0.45 + 0.55 * smooth_field(0.05, 0.5, h))
    t = np.clip((0.10 + 0.95 * lam) * occlusion + 0.18 * (h - 0.6), 0.0, 1.0)
    colour = ramp_field(style["ramp"], t)
    if style["wood"]:
        low = h < 0.28
        gap = 1 - smooth_field(0.10, 0.28, h)
        colour = np.where(low[..., None], mix_field(colour, (50, 40, 24), gap * 0.75), colour)
        bark = np.where((wp > 200)[..., None], np.array((104, 78, 46), dtype=np.float64),
                        np.array((78, 60, 36), dtype=np.float64))
        colour = np.where((low & (wp != 0))[..., None], mix_field(colour, bark, gap * wp / 255), colour)
    # Each vertex colour also tints its corner; the bilinear blend gives
    # mottling larger than a cell without a repeated feature.
    u = np.clip((index - HALO + 0.5) / CELL, 0.0, 1.0)[None, :]
    v = np.clip((index - HALO + 0.5) / CELL, 0.0, 1.0)[:, None]
    weights = ((1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v)
    scale = 0
    for k, w in enumerate(weights):
        scale = scale + w * tints[k][0]
    out = np.empty((size, size, 3))
    for c in range(3):
        shift = 0
        for k, w in enumerate(weights):
            shift = shift + w * tints[k][1][c]
        out[..., c] = colour[..., c] * scale + shift
    image = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    return image, heights


@functools.lru_cache(maxsize=96)
def canopy_field(mask, foreign=0):
    """Bilinear corner field over the cell and halo, clamped to the cell edge.

    A foreign corner (another mass) counts as -1, so along an edge to it the
    field crosses one half a quarter of the way from the own corner: the two
    masses' rims stand half a cell apart. Along an edge between own and open
    corners only those two values count, as before, so frames still agree."""
    bits = [1 if (mask >> k) & 1 else -1 if (foreign >> k) & 1 else 0 for k in range(4)]
    size = CELL + 2 * HALO
    u = np.clip((np.arange(size) - HALO + 0.5) / CELL, 0.0, 1.0)[None, :]
    v = u.T
    value = lerp(lerp(bits[0], bits[1], u), lerp(bits[2], bits[3], u), v)
    if mask in (6, 9) and not foreign:
        # Substantial joins avoid hourglass pinholes at diagonal corners.
        # Python's float power, not NumPy's square, so the frames match.
        sin_u = np.array([math.sin(t * math.pi) ** 2 for t in u.ravel().tolist()])[None, :]
        sin_v = sin_u.T
        value = value + 0.24 * sin_u * sin_v
    return Image.fromarray((np.clip(value, 0.0, 1.0) * 255).astype(np.uint8))


def shifted(image, dx, dy):
    """The image moved by (dx, dy), with uncovered pixels left empty."""
    out = Image.new(image.mode, image.size)
    out.paste(image, (dx, dy))
    return out


def raise_of(name):
    """HD pixels a connected canopy's top stands above its footprint."""
    return CANOPY_STYLE[name]["raise_"]


def render_connected(name, mask, colours, interior=0):
    """A 32x64 native frame: ground shadows, front wall, then the raised top.

    The frame owns the canopy whose footprint lies inside its cell. The top is
    drawn raise_of(name) pixels up and the front wall fills down to the
    footprint, so the next row's frame covers this row's wall wherever the
    canopy continues. Below the cell, the canopy's long shadow falls on the open
    ground south of it; where the canopy continues, the next row covers it too.
    """
    frame = Image.new("RGBA", (CELL, CANOPY_TOP + CELL + CANOPY_BELOW))
    if not mask:
        return frame
    style = CANOPY_STYLE[name]
    lift = raise_of(name)
    # Interior crowns never cross the cell edge, so every frame may choose its
    # own: spreading choices over masks and colours keeps edge cells varied too.
    surface, heights = canopy_surface(name, colours, (interior + colours * 3 + mask * 5) % CANOPY_INTERIORS)
    # Crowns push the outline out and the creases between them pull it in.
    bumps = heights.point(lambda v: int(smoothstep(0.40 * 255, 0.92 * 255, v) * 255 * style["amplitude"]))
    raw = ImageChops.add(canopy_field(mask), bumps)
    # A light blur removes one-pixel protrusions that would hang thin wall
    # columns below the outline.
    raw = raw.filter(ImageFilter.GaussianBlur(2.5))
    coverage = raw.point(lambda v: int(smoothstep(0.47 * 255, 0.53 * 255, v) * 255))
    falloff = raw.point(lambda v: int(smoothstep(0.50 * 255, 0.70 * 255, v) * 255))

    # Work on canvases with room above for the raised top and below for the
    # long shadow; the cell and its halo sit between.
    def pad(image):
        out = Image.new(image.mode, (image.width, CANOPY_TOP + image.height + CANOPY_BELOW))
        out.paste(image, (0, CANOPY_TOP))
        return out

    coverage, falloff, surface = pad(coverage), pad(falloff), pad(surface)
    cell = (HALO, CANOPY_TOP + HALO, HALO + CELL, CANOPY_TOP + HALO + CELL)
    below = (HALO, cell[3], HALO + CELL, cell[3] + CANOPY_BELOW)
    own = Image.new("L", coverage.size)
    own.paste(coverage.crop(cell), cell[:2])

    # Ground shadows: a soft contact shadow from all canopy nearby, and the
    # long shadow of the raised mass, fading with distance down the screen.
    contact = shifted(coverage, 8, 10).filter(ImageFilter.GaussianBlur(4)).point(lambda v: int(v * 0.42))
    # It comes from the canopy down to this cell's bottom edge, with the side
    # halos, so the blur matches across neighbouring columns.
    caster = Image.new("L", coverage.size)
    caster.paste(coverage.crop((0, 0, coverage.width, cell[3])), (0, 0))
    length = int(lift * 0.95)
    long_shadow = Image.new("L", coverage.size)
    for d in range(0, length + 1, 2):
        long_shadow = ImageChops.lighter(
            long_shadow, shifted(caster, 0, d).point(lambda v, f=1 - d / length: int(v * f)))
    long_shadow = long_shadow.filter(ImageFilter.GaussianBlur(6)).point(lambda v: int(v * 0.62))
    for box, row, shadow in ((cell, CANOPY_TOP, ImageChops.lighter(contact, long_shadow)),
                             (below, CANOPY_TOP + CELL, long_shadow)):
        layer = Image.new("RGBA", (CELL, box[3] - box[1]), (4, 18, 8, 255))
        layer.putalpha(shadow.crop(box))
        frame.alpha_composite(layer, (0, row))

    # Front wall from the footprint up to the raised top: brightest just under
    # the top edge, darkening to the ground. Each wall pixel shows the foliage
    # a little behind the rim above it, so the face reads as leaves, not as
    # the rim's colour smeared down.
    wall_alpha, wall_height = Image.new("L", own.size), Image.new("L", own.size)
    texture = Image.new("RGB", own.size)
    solid = own.point(lambda v: 255 if v > 127 else 0)
    for d in range(lift + 1):
        cover = shifted(solid, 0, -d)
        wall_alpha = ImageChops.lighter(wall_alpha, shifted(own, 0, -d))
        wall_height = ImageChops.lighter(wall_height, cover.point(lambda v, d=d: v and int(d * 255 / lift)))
        texture.paste(shifted(surface, 0, -(d - (lift - d) // 2)), (0, 0), cover)
    wall = Image.composite(texture.point(lambda v: int(v * 0.66)), texture.point(lambda v: int(v * 0.30)), wall_height)
    region = (HALO, cell[1] - lift, HALO + CELL, cell[3])
    layer = wall.crop(region).convert("RGBA")
    layer.putalpha(wall_alpha.crop(region))
    frame.alpha_composite(layer, (0, CANOPY_TOP - lift))

    # Raised top. Its silhouette is lit where it faces the light and shaded on
    # the far side, so the edge reads as a rounded rim rather than a cut-out.
    lit = ImageChops.subtract(falloff, shifted(falloff, 4, 4)).point(lambda v: min(255, v * 2))
    dark = ImageChops.subtract(falloff, shifted(falloff, -4, -4)).point(lambda v: min(255, v * 2))
    top = Image.composite(surface.point(lambda v: min(255, int(v * 1.28 + 14))), surface, lit)
    top = Image.composite(surface.point(lambda v: int(v * 0.62)), top, dark)
    layer = top.crop(cell).convert("RGBA")
    layer.putalpha(own.crop(cell))
    frame.alpha_composite(layer, (0, CANOPY_TOP - lift))
    return frame


# Cliffs use the connected corner-mask layout of hedge and thicket, but they are
# a raised block of rock rather than foliage: a craggy plateau split by joints,
# standing well above foliage, whose exposed fronts drop as fractured walls into
# a long shadow. Frames are 32x68 native: CLIFF_TOP rows above the cell hold the
# raised plateau and CLIFF_BELOW rows below it the shadow.
#
# The plateau has no discrete objects to repeat. Each vertex colour owns a patch
# of noise fields, and a cell blends the patches of its four corners with
# bilinear weights, so along a shared edge only the edge's two vertices count and
# neighbouring frames agree exactly. Filled cells add a seeded interior patch
# whose weight vanishes at the cell edge.
CLIFF_TOP = 80               # frame rows above the cell: the raised top (20 native)
CLIFF_BELOW = 64             # frame rows below it, for the contact shadow (16 native)
# Every rock-like mass stands this many HD rows above its footprint (20 native),
# which is also the runtime's terrace rise (TerrainVisual::DecorTierRise), so a
# terrace's wall reaches the terrace below.
CLIFF_RAISE = 80
CLIFF_PATCH = 2 * (CELL + HALO)
CLIFF_FACETS = 9              # wall facets across the range of the column field
CLIFF_SHELVES = 3             # weathered beds across the range of the crag field
CLIFF_STYLE = dict(
    # Weathered ochre stone, unlike the grey of boulders and ridges: a dusty
    # sunlit plateau over deeper, redder walls of the same rock.
    ramp=((0.0, (36, 31, 28)), (0.28, (80, 68, 58)), (0.52, (122, 106, 88)),
          (0.78, (160, 144, 120)), (1.0, (198, 186, 160))),
    # The face is the plateau's stone in its own shade, warmed a little by
    # light thrown back from the ground.
    wall=((0.0, (20, 17, 15)), (0.35, (60, 51, 43)), (0.70, (106, 92, 76)), (1.0, (150, 134, 112))),
    crack=(30, 24, 20), scrub=((48, 60, 30), (90, 100, 48)), lichen=(112, 114, 86),
    amplitude=0.22,
    # Seed prefix of the vertex, edge and interior fields.
    seed="cliff",
    # Fractures per edge (choices) and the joint drawer.
    edge_fractures=(0, 1), fracture="joint", interior_fractures=1,
    # Plateau relief: base + shelves + crags + grain + pits - joints + rocks.
    relief=(0.22, 0.30, 0.22, 0.03, 0.015, 0.03, 0.04),
    shadow=(16, 14, 18),
)
# A glacier is the same connected plateau in ice: a snowfield split by
# crevasses whose exposed fronts drop as blue ice walls under a snow cornice.
GLACIER_STYLE = dict(
    # Firn and wind-packed snow over blue ice, cooler and bluer in shade.
    ramp=((0.0, (40, 70, 112)), (0.28, (118, 152, 192)), (0.52, (190, 210, 230)),
          (0.78, (228, 237, 248)), (1.0, (248, 251, 255))),
    # The face is ice, not stone: deep blue at the foot, paler and greener
    # toward the lip where light passes through it.
    wall=((0.0, (8, 22, 50)), (0.30, (22, 64, 118)), (0.62, (62, 132, 190)), (0.86, (132, 194, 228)),
          (1.0, (196, 230, 246))),
    # Crevasses open on blue depths; hollows show bare blue ice; drifts of
    # fresh snow lie on the high ground where cliffs carry lichen.
    crack=(26, 74, 136), scrub=((70, 128, 182), (128, 188, 226)), lichen=(250, 252, 255),
    # Cornice: snow overhanging the lip, and the debris bands of old seasons.
    cornice=(238, 244, 252), debris=(74, 82, 96),
    amplitude=0.18,
    seed="glacier",
    edge_fractures=(0,), fracture="crevasse", interior_fractures=0, interior_crevasses=(0, 1, 1, 2),
    # Smoother than rock: low shelves, deep crevasses, no pitting to speak of.
    relief=(0.24, 0.12, 0.34, 0.02, 0.006, 0.10, 0.03),
    shadow=(20, 34, 60),
)
CLIFF_STYLES = {"cliffs": CLIFF_STYLE, "glacier": GLACIER_STYLE}
# Fractures and loose rocks belong to vertex discs no wider than REACH: a disc
# never reaches the far edges of the cells around its vertex, nor the halo of a
# farther cell, so every frame that sees a feature draws all of it.
CLIFF_DISC = REACH - 4


def disc_coverage(dx, dy):
    """How many vertex discs cover a point at (dx, dy) from a vertex."""
    return sum(math.hypot(dx - i * CELL, dy - j * CELL) < CLIFF_DISC for i in (-1, 0, 1) for j in (-1, 0, 1))


def disc_spots(rng, count, extent):
    """Feature centres in a vertex disc, thinned where discs overlap so the
    density is the same everywhere on the plateau."""
    spots = []
    while len(spots) < count:
        angle, rho = rng.uniform(0, math.tau), math.sqrt(rng.random()) * (CLIFF_DISC - extent)
        x, y = math.cos(angle) * rho, math.sin(angle) * rho
        if rng.random() * disc_coverage(x, y) < 1:
            spots.append((x, y))
    return spots


def draw_fracture(draw, rng, x, y, length):
    """A rock joint: straight segments with sharp turns and the odd short branch."""
    angle = rng.uniform(0, math.tau)
    points, left = [(x, y)], length
    while left > 0:
        step = min(left, rng.uniform(8, 20))
        ex, ey = x + math.cos(angle) * step, y + math.sin(angle) * step
        # Short wandering steps, so a joint is a broken line, not a clean stroke.
        for t in (0.35, 0.7):
            points.append((lerp(x, ex, t) + rng.uniform(-1.5, 1.5), lerp(y, ey, t) + rng.uniform(-1.5, 1.5)))
        x, y = ex, ey
        points.append((x, y))
        left -= step
        if rng.random() < 0.25:
            twig = angle + rng.choice((-1, 1)) * rng.uniform(0.8, 1.4)
            twig_length = rng.uniform(5, 10)
            draw.line([(x, y), (x + math.cos(twig) * twig_length, y + math.sin(twig) * twig_length)],
                      fill=200, width=2)
        angle += rng.choice((-1, 1)) * rng.uniform(0.3, 1.1)
    draw.line(points, fill=255, width=2)


def draw_crevasse(draw, rng, x, y, length):
    """A crevasse centred on (x, y): a gently curving slot across the ice,
    widest in the middle, running roughly east-west as transverse crevasses do
    across a flow. Its walls fade from the lip into the full depth of the slot."""
    angle = rng.uniform(-0.22, 0.22)
    bend = rng.uniform(-0.012, 0.012)
    steps = max(3, int(length / 4))
    x, y = x - math.cos(angle) * length / 2, y - math.sin(angle) * length / 2
    points = [(x, y)]
    for k in range(steps):
        angle += bend + rng.uniform(-0.03, 0.03)
        x, y = x + math.cos(angle) * length / steps, y + math.sin(angle) * length / steps
        points.append((x, y))
    for fill, extra in ((120, 3), (255, 0)):
        for k, (a, b) in enumerate(zip(points, points[1:])):
            # Widest at the middle, pinched shut at both ends.
            t = (k + 0.5) / steps
            width = 1 + int(round(6 * math.sin(math.pi * t))) + extra
            draw.line([a, b], fill=fill, width=width)
            draw.ellipse((b[0] - width / 2, b[1] - width / 2, b[0] + width / 2, b[1] + width / 2), fill=fill)


FRACTURES = {"joint": draw_fracture, "crevasse": draw_crevasse}


def draw_rock(draw, rng, x, y, radius):
    """A loose rock: a small angular polygon raised above the plateau."""
    angles = sorted(rng.uniform(0, math.tau) for _ in range(rng.randint(5, 7)))
    hull = convex_hull([(x + math.cos(a) * radius * rng.uniform(0.6, 1.0),
                         y + math.sin(a) * radius * rng.uniform(0.6, 1.0)) for a in angles])
    draw.polygon(hull, fill=rng.randint(170, 255))


def noise_patch(rng, cells, size=CLIFF_PATCH):
    """Smooth value noise in 0..1 over a size x size patch, `cells` lattice cells across."""
    lattice = Image.new("L", (cells + 3, cells + 3))
    lattice.putdata([rng.randrange(256) for _ in range((cells + 3) ** 2)])
    step = size / cells
    big = lattice.resize((int(step * (cells + 3)),) * 2, Image.Resampling.BICUBIC)
    data = big.crop((int(step), int(step), int(step) + size, int(step) + size))
    return np.asarray(data).astype(np.float64).ravel() / 255


def octaves(rng, cells, count, size=CLIFF_PATCH):
    fields = [noise_patch(rng, cells * 2 ** o, size) for o in range(count)]
    weights = [0.5 ** o for o in range(count)]
    total = sum(weights)
    out = np.zeros(size * size)
    for w, f in zip(weights, fields):
        out = out + w * f
    return out / total


@functools.lru_cache(maxsize=8)
def cliff_patch(name, colour):
    """(relief, joints, tone, grain) noise of one vertex colour, centred on the vertex."""
    rng = random.Random(fnv1a32(f"{CLIFF_STYLES[name]['seed']}-patch:{colour}"))
    fields = (octaves(rng, 3, 3), octaves(rng, 2, 2), octaves(rng, 10, 3))
    return fields + cliff_features(name, rng, CLIFF_PATCH, CLIFF_PATCH // 2, 0, (30, 46), 3)


# Edge features: most fractures and rocks belong to a cell edge, keyed by the
# colours of its two ends, which gives four variants per orientation against
# the two of a vertex. They keep within CLIFF_EDGE_SPAN of the edge's midpoint
# along it, short of the halos of the cells beside the two that share it.
CLIFF_EDGE_SPAN = HALO + 16
CLIFF_EDGE_DEPTH = 80


@functools.lru_cache(maxsize=16)
def cliff_edge(name, vertical, first, second):
    """(joints, stones) images of one edge, centred on its midpoint."""
    style = CLIFF_STYLES[name]
    rng = random.Random(fnv1a32(f"{style['seed']}-edge:{int(vertical)}:{first}:{second}"))
    size = (2 * CLIFF_EDGE_SPAN, 2 * CLIFF_EDGE_DEPTH)
    joints, stones = Image.new("L", size), Image.new("L", size)
    # Glacier draws no edge crevasses: an edge has only four variants, which
    # would line them up along rows, so they belong to the interior variants.
    for _ in range(rng.choice(style["edge_fractures"])):
        # Fractures may run past the box; the box clips them to a broken end.
        draw_fracture(ImageDraw.Draw(joints), rng, CLIFF_EDGE_SPAN + rng.uniform(-14, 14),
                      CLIFF_EDGE_DEPTH + rng.uniform(-48, 48), rng.uniform(22, 36))
    for _ in range(2):
        draw_rock(ImageDraw.Draw(stones), rng, CLIFF_EDGE_SPAN + rng.uniform(-20, 20),
                  CLIFF_EDGE_DEPTH + rng.uniform(-60, 60), rng.uniform(5, 11))
    # Fade the box's own edges so a clipped fracture tapers off.
    fade = Image.new("L", size)
    ImageDraw.Draw(fade).rectangle((6, 6, size[0] - 7, size[1] - 7), fill=255)
    fade = fade.filter(ImageFilter.GaussianBlur(3))
    joints = ImageChops.multiply(joints.filter(ImageFilter.GaussianBlur(0.6)), fade)
    if vertical:
        return joints.transpose(Image.Transpose.TRANSPOSE), stones.transpose(Image.Transpose.TRANSPOSE)
    return joints, stones


def cliff_features(name, rng, size, centre, fractures, lengths, rocks, spots=None):
    """Fracture and loose-rock coverage as flat lists over a size x size patch.

    A fracture never reaches farther from its start than its length (plus a
    short branch), which bounds its extent in the disc."""
    joints, stones = Image.new("L", (size, size)), Image.new("L", (size, size))
    for (x, y) in (spots or disc_spots)(rng, fractures, lengths[1] + 12):
        FRACTURES[CLIFF_STYLES[name]["fracture"]](ImageDraw.Draw(joints), rng, centre + x, centre + y,
                                                  rng.uniform(*lengths))
    for (x, y) in (spots or disc_spots)(rng, rocks, 14):
        draw_rock(ImageDraw.Draw(stones), rng, centre + x, centre + y, rng.uniform(5, 11))
    joints = joints.filter(ImageFilter.GaussianBlur(0.6))
    return (np.asarray(joints).astype(np.float64).ravel() / 255,
            np.asarray(stones).astype(np.float64).ravel() / 255)


@functools.lru_cache(maxsize=16)
def cliff_detail(name, interior):
    """The same fields wholly inside a filled cell, one set per interior variant."""
    rng = random.Random(fnv1a32(f"{CLIFF_STYLES[name]['seed']}-interior:{interior}"))
    fields = (octaves(rng, 2, 3, CELL), octaves(rng, 1, 2, CELL), octaves(rng, 5, 3, CELL))
    inside = lambda rng, count, extent: [(rng.uniform(-CELL / 2 + extent + 14, CELL / 2 - extent - 14),
                                          rng.uniform(-CELL / 2 + extent + 14, CELL / 2 - extent - 14))
                                         for _ in range(count)]
    # Features of an interior stay clear of the cell edge, whose fades they
    # would otherwise cut.
    style = CLIFF_STYLES[name]
    features = cliff_features(name, rng, CELL, CELL // 2, style["interior_fractures"], (18, 26), 1, spots=inside)
    if style["fracture"] == "crevasse":
        # Crevasses picked by the cell hash, wherever they fit clear of the edge.
        joints = Image.new("L", (CELL, CELL))
        count = rng.choice(style["interior_crevasses"])
        for k in range(count):
            length = rng.uniform(48, 94)
            reach = CELL / 2 - 16 - length / 2
            # Two crevasses keep to their own halves, so they never converge.
            rows = (26, CELL - 26) if count == 1 else ((26, CELL / 2 - 8), (CELL / 2 + 8, CELL - 26))[k]
            draw_crevasse(ImageDraw.Draw(joints), rng, CELL / 2 + rng.uniform(-reach, reach),
                          rng.uniform(*rows), length)
        # A crevasse that wanders toward the edge tapers off well short of it.
        fade = Image.new("L", (CELL, CELL))
        ImageDraw.Draw(fade).rectangle((18, 18, CELL - 19, CELL - 19), fill=255)
        joints = ImageChops.multiply(joints.filter(ImageFilter.GaussianBlur(0.6)),
                                     fade.filter(ImageFilter.GaussianBlur(3)))
        features = (np.maximum(features[0], np.asarray(joints).astype(np.float64).ravel() / 255), features[1])
    return fields + features


def corner_blend(patches, detail, count):
    """Blend each vertex colour's patch fields over the cell and halo.

    `patches` holds the four corners' field tuples (flat CLIFF_PATCH-square
    arrays centred on the vertex), `detail` a filled cell's interior fields
    (flat CELL-square) or None. The first `count` fields take the
    variance-preserving bilinear blend; the rest, features drawn whole, take
    the maximum of every patch that reaches the pixel. Every step repeats the
    per-pixel arithmetic in order, so the frames are unchanged."""
    size = CELL + 2 * HALO
    grid = np.clip((np.arange(size) - HALO + 0.5) / CELL, 0.0, 1.0)
    u, v = grid[None, :], grid[:, None]
    weights = [(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v]
    centre = CLIFF_PATCH // 2

    def view(k, f):
        oy = centre - HALO - (CELL if k & 2 else 0)
        ox = centre - HALO - (CELL if k & 1 else 0)
        return np.asarray(patches[k][f]).reshape(CLIFF_PATCH, CLIFF_PATCH)[oy:oy + size, ox:ox + size]

    region = np.zeros((size, size), dtype=bool)
    region[HALO:HALO + CELL, HALO:HALO + CELL] = True
    inside = np.zeros((size, size))
    if detail:
        # Zero at the cell edge, so neighbours never see it.
        edge = np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v))
        inside = np.where(region, 0.35 * smooth_field(0.0, 0.32, edge), 0.0)
        weights = [w * (1 - inside) for w in weights]

    def own(f):
        return np.pad(np.asarray(detail[f]).reshape(CELL, CELL), HALO)

    norm = np.sqrt(weights[0] * weights[0] + weights[1] * weights[1] + weights[2] * weights[2]
                   + weights[3] * weights[3] + inside * inside)
    fields = []
    for f in range(len(patches[0])):
        if f < count:
            # Variance-preserving blend: the mix is as rough at the middle of
            # a cell as next to a vertex, so no grid shows through.
            total = 0.0
            for k, w in enumerate(weights):
                total = total + w * (view(k, f) - 0.5)
            if detail:
                total = total + inside * (own(f) - 0.5)
            fields.append(np.clip(0.5 + total / norm, 0.0, 1.0))
        else:
            # Fractures and rocks are drawn whole, never faded: every disc
            # that reaches the pixel contributes.
            value = np.maximum(np.maximum(view(0, f), view(1, f)), np.maximum(view(2, f), view(3, f)))
            if detail:
                value = np.where(region, np.maximum(value, own(f)), value)
            fields.append(value)
    return fields


def surface_light(relief, scale):
    """Lambert term of a relief field lit by CANOPY_LIGHT, with clamped edges,
    and the term of flat ground."""
    size = relief.shape[0]
    after = np.minimum(np.arange(size) + 1, size - 1)
    before = np.maximum(np.arange(size) - 1, 0)
    gx = (relief[:, after] - relief[:, before]) * scale
    gy = (relief[after, :] - relief[before, :]) * scale
    lx, ly, lz = CANOPY_LIGHT
    norm_l = math.sqrt(lx * lx + ly * ly + lz * lz)
    return (-gx * lx - gy * ly + lz) / (np.sqrt(gx * gx + gy * gy + 1) * norm_l), lz / norm_l


@functools.lru_cache(maxsize=48)
def cliff_surface(name, colours, interior, filled):
    """Lit plateau colour, heights and wall streaks of the cell plus its halo."""
    style = CLIFF_STYLES[name]
    size = CELL + 2 * HALO
    patches = [cliff_patch(name, (colours >> k) & 1) for k in range(4)]
    detail = cliff_detail(name, interior) if filled else None
    fields = corner_blend(patches, detail, 3)
    # Add the features of the cell's four edges.
    features = [Image.fromarray((fields[f] * 255).astype(np.uint8)) for f in (3, 4)]
    bit = lambda k: (colours >> k) & 1
    for vertical, first, second, cx, cy in ((False, bit(0), bit(1), HALO + CELL // 2, HALO),
                                            (False, bit(2), bit(3), HALO + CELL // 2, HALO + CELL),
                                            (True, bit(0), bit(2), HALO, HALO + CELL // 2),
                                            (True, bit(1), bit(3), HALO + CELL, HALO + CELL // 2)):
        for c, source in enumerate(cliff_edge(name, vertical, first, second)):
            layer = Image.new("L", (size, size))
            layer.paste(source, (cx - source.width // 2, cy - source.height // 2))
            features[c] = ImageChops.lighter(features[c], layer)
    fields[3], fields[4] = (np.asarray(image).astype(np.float64) / 255 for image in features)
    height, tone, grain, joints, stones = fields
    # Fine pitting: independent per pixel, so it needs no agreement at edges.
    rng = random.Random(fnv1a32(f"{style['seed']}-pits:{colours}:{interior}:{int(filled)}"))
    pits = np.array([rng.random() for _ in range(size * size)]).reshape(size, size)
    # Heights: the crags weather into shelves, flat beds with a short riser at
    # each step, so the stone breaks into planes with crisp lit and shaded edges.
    def shelves(h, g):
        level = h * CLIFF_SHELVES + 0.15 * (g - 0.5)
        step = np.floor(level)
        return (step + smooth_field(0.0, 0.4, level - step)) / CLIFF_SHELVES
    k0, k_shelf, k_crag, k_grain, k_pit, k_joint, k_rock = style["relief"]
    relief = (k0 + k_shelf * shelves(height, grain) + k_crag * height + k_grain * (grain - 0.5)
              + k_pit * (pits - 0.5) - k_joint * joints + k_rock * stones)
    lam, flat = surface_light(relief, 10)
    t = (0.46 + 0.8 * (lam - flat) + 0.30 * (relief - 0.53) + 0.34 * (tone - 0.5)
         + 0.14 * (grain - 0.5) + 0.06 * (pits - 0.5))
    colour = ramp_field(style["ramp"], np.clip(t, 0.0, 1.0))
    # Lichen and weathering stains on the higher, rougher stone.
    stain = smooth_field(0.58, 0.78, tone) * smooth_field(0.45, 0.7, grain)
    colour = mix_field(colour, style["lichen"], 0.35 * stain)
    colour = mix_field(colour, style["crack"], 0.85 * joints)
    # Scrub in the lowest hollows, where the grain is also high.
    low = smooth_field(0.36, 0.28, relief) * smooth_field(0.6, 0.8, grain)
    colour = mix_field(colour, mix_field(*style["scrub"], np.clip(lam, 0.0, 1.0)), 0.8 * low)
    image = from_data(colour)
    # The outline follows the crags but not the joints: a joint reaching the rim
    # would notch it, and the wall below would extrude the notch into a column.
    heights = from_data(np.clip(0.22 + 0.62 * height, 0.0, 1.0) * 255)
    # Vertical streaks for the walls: the plateau's tone and joints smeared down
    # the face, so fractures line up with the joints above them.
    marks = from_data(np.clip(0.5 + 0.4 * (tone - 0.5) + 0.6 * (grain - 0.5) - 0.35 * joints, 0.0, 1.0) * 255)
    # Columns: the marks drawn out down the face, so fractures line up with
    # the plateau above; rough: the same marks barely softened, for texture.
    return image, heights, marks.filter(ImageFilter.GaussianBlur((0.3, 30))), marks.filter(ImageFilter.GaussianBlur(1.0))


def ice_face(style, base, shade, t, c, g, band):
    """A glacier front: blue ice whose depth follows the facet shade, crossed
    by broken debris bands, under a snow cornice at the lip. Arrays in, the
    face's (..., 3) colours out."""
    colour = ramp_field(style["wall"], np.clip(0.02 + 1.25 * shade + 0.36 * (c - 0.5), 0.0, 1.0))
    # A little of the snowfield above, so facets keep its tone.
    colour = mix_field(colour, base * (0.45 + 0.5 * shade)[..., None], 0.15)
    seam = (1 - smooth_field(0.0, 0.05, band)) * smooth_field(0.5, 0.66, g)
    colour = mix_field(colour, style["debris"], 0.40 * seam)
    lip = smooth_field(0.80, 0.92, t + 0.10 * (g - 0.5))
    return mix_field(colour, np.asarray(style["cornice"], dtype=np.float64) * (0.86 + 0.14 * t)[..., None], lip)


def wall_canvas(own, coverage, rise):
    """Wall alpha and height of a mass's face, on a canvas `rise` rows taller
    at the top than the cell and halo, so a face reaches its full height even
    above the cell. The footprint `own` lies in this frame's cell; its face may
    rise into the row above, where a rim that curves away under the next
    row's top must still cover what that row drew. Returns (alpha, height,
    pad), both images offset down by pad rows.

    Height: how far above the foot of the face each pixel stands (255 just
    under the lip, 0 at the ground), following the whole footprint (which
    continues past the cell edge) so a face running on into the next row has
    no false foot; the halo below is shorter than the wall, so its last row
    is carried on down."""
    width, height = own.size
    pad = rise - HALO if rise > HALO else 0
    tall = np.zeros((height + pad, width), dtype=np.uint8)
    tall[pad:] = np.asarray(own)
    solid = Image.new("L", (width, pad + height + rise))
    solid.paste(coverage.point(lambda v: 255 if v > 127 else 0), (0, pad))
    solid.paste(solid.crop((0, pad + height - 1, width, pad + height)).resize((width, rise)), (0, pad + height))
    solid = np.asarray(solid) != 0
    wall_alpha = np.zeros(tall.shape, dtype=np.uint8)
    wall_height = np.zeros(tall.shape, dtype=np.uint8)
    for d in range(rise + 1):
        # The footprint moved up d rows, and the face d rows above its foot.
        wall_alpha[:len(tall) - d] = np.maximum(wall_alpha[:len(tall) - d], tall[d:])
        wall_height = np.maximum(wall_height, np.where(solid[d:d + height + pad], int(d * 255 / rise), 0)
                                 .astype(np.uint8))
    return Image.fromarray(wall_alpha), Image.fromarray(wall_height), pad


def wall_rows(pad, height, rise):
    """Rows of a wall canvas (y from -pad) and the face rows they show."""
    rows = np.arange(-pad, height)
    return rows, np.minimum(rows + rise, height - 1)


def dome_shade(coverage, gain):
    """Broad light over a top: the gradient of the blurred footprint toward
    the light, as an L image."""
    dome = image_data(coverage.filter(ImageFilter.GaussianBlur(14)))
    height, width = dome.shape
    after_x, before_x = np.minimum(np.arange(width) + 2, width - 1), np.maximum(np.arange(width) - 2, 0)
    after_y, before_y = np.minimum(np.arange(height) + 2, height - 1), np.maximum(np.arange(height) - 2, 0)
    gx = dome[:, after_x] - dome[:, before_x]
    gy = dome[after_y, :] - dome[before_y, :]
    lx, ly, _ = CANOPY_LIGHT
    return from_data(np.clip(0.5 + (gx * lx + gy * ly) / 255 * gain, 0.0, 1.0) * 255)


# Contact shadows as (dx, dy, blur, strength) layers, thrown to the lower right.
# A frame knows the footprint only HALO pixels past its cell, so every layer's
# offset plus about two and a half blur radii stays within it: a longer throw
# would need rock the frame cannot see, and the shadow would stop square at the
# top and left of each cell beside a rim. The cliffs' longer throw is a darker
# far layer rather than a farther one.
CLIFF_SHADOW = ((2, 4, 1.5, 0.70), (6, 9, 2.5, 0.58), (10, 14, 3, 0.46))
MASS_SHADOW = ((2, 4, 1.5, 0.62), (6, 8, 2.5, 0.50), (9, 12, 3, 0.38))
SHADOW_PAD = 16               # room for the blur past the halo (edge rows repeated)


def contact_shadow(coverage, mask, colour, layers):
    """A mass's contact shadow over its cell, as an RGBA layer.

    When neither bottom corner is the mass's, the cell below draws nothing
    that could carry the shadow, so it continues into the frame's CLIFF_BELOW
    rows rather than stopping square at the cell edge. Nothing of the mass
    lies below such a cell, so that part is exact."""
    below = 0 if mask & 0b1100 else CLIFF_BELOW
    pad = SHADOW_PAD + below
    source = Image.fromarray(np.pad(np.asarray(coverage), pad, mode="edge"))
    shadow = None
    for dx, dy, blur, strength in layers:
        layer = shifted(source, dx, dy).filter(ImageFilter.GaussianBlur(blur)).point(
            lambda v, k=strength: int(v * k))
        shadow = layer if shadow is None else ImageChops.lighter(shadow, layer)
    left = pad + HALO
    out = Image.new("RGBA", (CELL, CELL + below), colour + (255,))
    out.putalpha(shadow.crop((left, left, left + CELL, left + CELL + below)))
    return out


# Each facet band of the cliff face turns its own way to the light.
FACET_TURNS = np.array([runtime_hash(level, 7, 0xface7) % 1000 / 1000 - 0.5 for level in range(CLIFF_FACETS + 1)])


def render_cliff(name, mask, colours, interior=0, foreign=0):
    """A 32x64 native frame: contact shadow, fractured wall, then the plateau.

    The same ownership rules as render_connected: the frame draws the rock whose
    footprint lies in its cell, and the next row's plateau covers this row's
    wall wherever the cliff continues. Where the rim curves away under the
    next row, that row's wall rises the full height into this row's cell and
    covers the rest, so no square of this row's wall shows past the corner.
    """
    frame = Image.new("RGBA", (CELL, CLIFF_TOP + CELL + CLIFF_BELOW))
    if not mask:
        return frame
    style = CLIFF_STYLES[name]
    surface, heights, columns, rough = cliff_surface(name, colours, interior, mask == 15)
    # Crags push the edge out and hollows pull it in; a blur keeps the rim a
    # continuous broken line rather than single spikes.
    smooth = heights.filter(ImageFilter.GaussianBlur(3))
    bumps = smooth.point(lambda v: int(smoothstep(0.35 * 255, 0.70 * 255, v) * 255 * style["amplitude"]))
    raw = ImageChops.add(canopy_field(mask, foreign), bumps).filter(ImageFilter.GaussianBlur(2.0))
    coverage = raw.point(lambda v: int(smoothstep(0.50 * 255, 0.54 * 255, v) * 255))
    cell = (HALO, HALO, HALO + CELL, HALO + CELL)
    own = Image.new("L", coverage.size)
    own.paste(coverage.crop(cell), cell[:2])

    # A long contact shadow: the cliff is taller than any foliage.
    frame.alpha_composite(contact_shadow(coverage, mask, style["shadow"], CLIFF_SHADOW), (0, CLIFF_TOP))

    # Wall: the face of this cell's footprint, up to its full height (see
    # wall_canvas), on a canvas padded `pad` rows at the top.
    wall_alpha, wall_height, pad = wall_canvas(own, coverage, CLIFF_RAISE)
    width, height = own.size
    rows, face = wall_rows(pad, height, CLIFF_RAISE)
    t = image_data(wall_height) / 255
    # The face shows the plateau above its foot; below the halo the last
    # row carries on.
    c = image_data(columns)[face] / 255
    g = image_data(rough)[face] / 255
    # Broad facets: the column field cut into bands, each turned its own
    # way to the light. Bands follow the field, so a facet crossing a
    # cell edge keeps its shade on both sides.
    turn = FACET_TURNS[(c * CLIFF_FACETS).astype(np.int64)]
    # Bedding: faint, broken seams under lit steps.
    band = (t * 2.6 + 0.25 * c) % 1.0
    ledge = (0.10 * (1 - smooth_field(0.0, 0.07, band)) - 0.05 * smooth_field(0.90, 1.0, band)) * smooth_field(0.42, 0.6, g)
    # The face is the stone of the plateau above it, in shade: darkest
    # at the foot, with skylight near the lip.
    shade = np.clip(0.30 + 0.34 * t ** 0.9 + 0.26 * turn - ledge, 0.0, 1.0)
    # Stretched twice down the face so it reads as a drop, not as the
    # top running on; frames in one row share rows, so it stays seamless.
    base = image_data(surface)[HALO + 10 + (rows - HALO + CLIFF_RAISE) // 2].astype(np.float64)
    if "cornice" in style:
        wall = from_data(ice_face(style, base, shade, t, c, g, band))
    else:
        wall = from_data(base * shade[..., None] * np.array((1.04, 1.0, 0.94)))
    region = (HALO, HALO - CLIFF_RAISE + pad, HALO + CELL, HALO + CELL + pad)
    layer = wall.crop(region).convert("RGBA")
    layer.putalpha(wall_alpha.crop(region))
    frame.alpha_composite(layer, (0, CLIFF_TOP - CLIFF_RAISE))

    # Broad light over the plateau: it rises toward the rims that face the
    # light and falls away toward those that do not.
    shade = dome_shade(coverage, 1.6)
    lighter = surface.point(lambda v: min(255, int(v * 1.12 + 4)))
    darker = surface.point(lambda v: int(v * 0.80))
    top = Image.composite(lighter, Image.composite(surface, darker, shade.point(lambda v: min(255, v * 2))),
                          shade.point(lambda v: max(0, v * 2 - 255)))
    # The rims catch the light: a lit lip toward it and along the top of each
    # south face, where the face drops into shade; east-facing rims fall dark.
    rim = coverage.filter(ImageFilter.GaussianBlur(0.8))
    chips = rough.point(lambda v: int(smoothstep(0.40 * 255, 0.55 * 255, v) * 255))
    lit = ImageChops.multiply(ImageChops.lighter(ImageChops.subtract(rim, shifted(rim, 2, 2)),
                                                 ImageChops.subtract(rim, shifted(rim, 0, -2))), chips
                              ).point(lambda v: min(255, v * 2))
    dark = ImageChops.subtract(rim, shifted(rim, -4, 0)).point(lambda v: min(255, v * 2))
    top = Image.composite(surface.point(lambda v: min(255, int(v * 1.16 + 6))), top, lit)
    top = Image.composite(surface.point(lambda v: int(v * 0.60)), top, dark)
    layer = top.crop(cell).convert("RGBA")
    layer.putalpha(own.crop(cell))
    frame.alpha_composite(layer, (0, CLIFF_TOP - CLIFF_RAISE))
    return frame


# A glacier icefall uses the cliffs' layout and wall construction, in ice: a
# wind-packed snow surface with patches of scoured blue ice, cut by crevasses
# that run across the flow, standing higher than the cliffs on sheer, fluted
# blue walls. Each crevasse follows a horizontal cell edge: its offset and
# width at a vertex depend only on that vertex's colour, and its course and
# opening between two vertices only on the colours of the edge's ends, so the
# cells above and below the edge draw the same crack and it runs on from cell
# to cell across the whole patch, opening and pinching out as it goes.
ICEFALL_RAISE = CLIFF_RAISE   # HD rows the snow surface stands above its footprint (20 native)
ICEFALL_STYLE = dict(
    snow=((0.0, (196, 210, 230)), (0.45, (226, 234, 246)), (1.0, (246, 250, 254))),
    ice=((0.0, (104, 164, 208)), (1.0, (150, 202, 232))),
    crevasse=((0.0, (150, 196, 226)), (0.45, (64, 118, 176)), (1.0, (22, 52, 100))),
    # Walls: deep blue at the foot, paler toward the lip, then a rind of snow.
    wall=((0.0, (34, 78, 136)), (0.55, (84, 146, 196)), (0.9, (160, 206, 232)), (1.0, (232, 242, 250))),
    amplitude=0.18,
)


def stretched_patch(rng, cells_x, cells_y, size=CLIFF_PATCH):
    """Smooth value noise in 0..1, with more lattice cells down than across."""
    lattice = Image.new("L", (cells_x + 3, cells_y + 3))
    lattice.putdata([rng.randrange(256) for _ in range((cells_x + 3) * (cells_y + 3))])
    sx, sy = size / cells_x, size / cells_y
    big = lattice.resize((int(sx * (cells_x + 3)), int(sy * (cells_y + 3))), Image.Resampling.BICUBIC)
    data = big.crop((int(sx), int(sy), int(sx) + size, int(sy) + size))
    return np.asarray(data).astype(np.float64).ravel() / 255


@functools.lru_cache(maxsize=4)
def icefall_patch(colour):
    """(tone, scour, crevasse, opening, grain) noise of one vertex colour."""
    rng = random.Random(fnv1a32(f"icefall-patch:{colour}"))
    return (octaves(rng, 2, 3), octaves(rng, 3, 2), stretched_patch(rng, 2, 3),
            octaves(rng, 3, 2), stretched_patch(rng, 5, 26))


@functools.lru_cache(maxsize=8)
def icefall_detail(interior):
    """The same fields wholly inside a filled cell, one set per interior variant."""
    rng = random.Random(fnv1a32(f"icefall-interior:{interior}"))
    return (octaves(rng, 1, 3, CELL), octaves(rng, 2, 2, CELL), stretched_patch(rng, 1, 2, CELL),
            octaves(rng, 2, 2, CELL), stretched_patch(rng, 3, 13, CELL))


ICEFALL_CRACK_OFFSET = (-38, 30)    # HD rows a crevasse sits from the edge at a vertex of each colour
ICEFALL_CRACK_WIDTH = (1.0, 8.0)     # its half width there: a snow bridge, or gaping


@functools.lru_cache(maxsize=4)
def icefall_crack_shape(first, second):
    """(harmonics, swell) of the crevasse between two vertex colours: terms
    that vanish at both vertices, so only the vertex values meet there. The
    harmonics fall off quickly, giving a jagged course rather than a wave."""
    rng = random.Random(fnv1a32(f"icefall-crack:{first}:{second}"))
    harmonics = [(k, rng.uniform(-1, 1) * 12 / k ** 1.3) for k in range(1, 9)]
    # A negative swell pinches the crack shut between the two vertices.
    return harmonics, rng.uniform(-11, 7)


def icefall_crack(first, second, t):
    """(row offset, half width) of the crevasse a fraction t along an edge."""
    harmonics, swell = icefall_crack_shape(first, second)
    ease = t * t * (3 - 2 * t)
    hump = math.sin(math.pi * t)
    offset = lerp(ICEFALL_CRACK_OFFSET[first], ICEFALL_CRACK_OFFSET[second], ease) \
        + sum(a * math.sin(k * math.pi * t) for k, a in harmonics)
    # Jagged walls: the width wavers with the higher harmonics too.
    jitter = sum(a * math.sin((k + 3) * math.pi * t) for k, a in harmonics[:4]) * 0.15
    width = lerp(ICEFALL_CRACK_WIDTH[first], ICEFALL_CRACK_WIDTH[second], ease) + swell * hump + jitter * hump
    # Slivers narrower than this close up, so a pinched crack ends cleanly.
    return offset, width if width > 2.0 else 0.0


@functools.lru_cache(maxsize=48)
def icefall_surface(colours, interior, filled):
    """Lit surface colour, outline heights and wall streaks of the cell plus halo."""
    style = ICEFALL_STYLE
    size = CELL + 2 * HALO
    patches = [icefall_patch((colours >> k) & 1) for k in range(4)]
    detail = icefall_detail(interior) if filled else None
    tone, scour, _, _, grain = corner_blend(patches, detail, len(patches[0]))
    # Crevasses of the cell's top and bottom edges, the only two it can see.
    bit = lambda k: (colours >> k) & 1
    crack = [0.0] * (size * size)
    near = [0.0] * (size * size)      # 1 under the near (south) lip, 0 on the far wall
    for edge_row, first, second in ((HALO, bit(0), bit(1)), (HALO + CELL, bit(2), bit(3))):
        for x in range(size):
            offset, width = icefall_crack(first, second, clamp((x - HALO + 0.5) / CELL))
            if width < 0.5:
                continue
            centre = edge_row + offset
            for y in range(max(0, int(centre - width - 2)), min(size, int(centre + width + 3))):
                core = clamp(1 - abs(y + 0.5 - centre) / width) ** 0.6
                i = y * size + x
                if core > crack[i]:
                    crack[i] = core
                    near[i] = smoothstep(-0.3, 0.6, (y + 0.5 - centre) / width)
    crack = np.array(crack).reshape(size, size)
    near = np.array(near).reshape(size, size)
    relief = 0.5 + 0.10 * (tone - 0.5) + 0.04 * (grain - 0.5) - 0.42 * crack
    lam, flat = surface_light(relief, 12)
    # Wind-packed snow with faint sastrugi; scoured ridges bare blue ice.
    colour = ramp_field(style["snow"], np.clip(0.5 + 0.7 * (tone - 0.5) + 0.25 * (grain - 0.5), 0.0, 1.0))
    bare = smooth_field(0.605, 0.635, scour + 0.10 * (grain - 0.5))
    colour = mix_field(colour, ramp_field(style["ice"], tone), 0.85 * bare)
    # The far wall faces the viewer and catches skylight; the gap under the
    # near lip falls into deep blue shadow.
    inner = ramp_field(style["crevasse"], np.clip(crack * (0.55 + 0.6 * near), 0.0, 1.0))
    colour = mix_field(colour, inner, smooth_field(0.0, 0.2, crack))
    shade = np.clip(0.92 + 0.9 * (lam - flat), 0.45, 1.12)
    image = from_data(colour * shade[..., None])
    # The outline follows the snow's broad swell, not the crevasses: a crack
    # meeting the rim shows on the wall below it instead.
    heights = from_data(np.clip(0.22 + 0.62 * tone, 0.0, 1.0) * 255)
    # Wall streaks: fluting from the fine grain, and crevasses carried down the face.
    marks = from_data(np.clip(0.5 + 0.9 * (grain - 0.5) - 0.5 * crack, 0.0, 1.0) * 255)
    return image, heights, marks.filter(ImageFilter.GaussianBlur((0.4, 30))), marks.filter(ImageFilter.GaussianBlur(1.0))


def icefall_wall(t, c, g):
    """Fluting: vertical runnels from the column field over blue that deepens
    toward the foot, with a snow rind at the lip."""
    colour = ramp_field(ICEFALL_STYLE["wall"], np.clip(t * 0.96 + 0.08 * (c - 0.5), 0.0, 1.0))
    shade = np.clip(0.82 + 0.42 * (c - 0.5) + 0.10 * (g - 0.5), 0.0, 1.0)
    shade = np.where(t > 0.94, np.maximum(shade, 0.98), shade)
    return colour * shade[..., None]


# The connected Barriers masses beside cliffs: each names its surface (the lit
# top, outline heights and wall marks over the cell and halo), its wall colour
# at a height fraction, how far it stands above its footprint (HD rows; 64 at
# most in a 32x64 frame), how much its outline swells, its contact-shadow tint
# and the colours of its lit lip and its far, shaded rim.
MASSES = {
    "icefall": dict(surface=lambda c, i, f: icefall_surface(c, i, f), wall=icefall_wall, lift=ICEFALL_RAISE,
                    amplitude=0.18, shadow=(18, 28, 52), lip=(252, 254, 255), far=(120, 160, 206)),
}


def _register_masses():
    import terrain_masses
    looks = {
        "sandstone_benches": dict(lift=CLIFF_RAISE, amplitude=0.20, shadow=(42, 26, 16), lip=(246, 226, 190), far=(150, 104, 70)),
        "obsidian_ridge": dict(lift=CLIFF_RAISE, amplitude=0.24, shadow=(10, 10, 16), lip=(150, 156, 176), far=(20, 20, 26)),
        "granite_tors": dict(lift=CLIFF_RAISE, amplitude=0.18, shadow=(20, 22, 18), lip=(214, 208, 194), far=(88, 84, 76)),
        "travertine_terraces": dict(lift=CLIFF_RAISE, amplitude=0.20, shadow=(48, 40, 28), lip=(248, 244, 234), far=(170, 150, 118)),
    }
    for name, (surface, wall) in terrain_masses.SURFACES.items():
        MASSES[name] = dict(surface=surface, wall=wall, **looks[name])


_register_masses()


def render_mass(name, mask, colours, interior=0, foreign=0):
    """A 32x64 native frame of a connected mass: contact shadow, wall, then the top.

    The same ownership rules as render_cliff: the frame draws the mass whose
    footprint lies in its cell, and the next row's top covers this row's wall
    wherever the mass continues. Corners of another mass (`foreign`) pull the
    rim back a quarter of the cell, leaving a valley between the two.
    """
    frame = Image.new("RGBA", (CELL, CLIFF_TOP + CELL + CLIFF_BELOW))
    if not mask:
        return frame
    look = MASSES[name]
    lift = look["lift"]
    surface, heights, columns, rough = surface_cached(name, colours, interior, mask == 15)
    smooth = heights.filter(ImageFilter.GaussianBlur(3))
    bumps = smooth.point(lambda v: int(smoothstep(0.35 * 255, 0.70 * 255, v) * 255 * look["amplitude"]))
    raw = ImageChops.add(canopy_field(mask, foreign), bumps).filter(ImageFilter.GaussianBlur(2.0))
    coverage = raw.point(lambda v: int(smoothstep(0.50 * 255, 0.54 * 255, v) * 255))
    cell = (HALO, HALO, HALO + CELL, HALO + CELL)
    own = Image.new("L", coverage.size)
    own.paste(coverage.crop(cell), cell[:2])

    frame.alpha_composite(contact_shadow(coverage, mask, look["shadow"], MASS_SHADOW), (0, CLIFF_TOP))

    # Wall: the face of this cell's footprint, as for cliffs (see wall_canvas).
    wall_alpha, wall_height, pad = wall_canvas(own, coverage, lift)
    width, height = own.size
    _, face = wall_rows(pad, height, lift)
    wall = from_data(look["wall"](image_data(wall_height) / 255, image_data(columns)[face] / 255,
                                  image_data(rough)[face] / 255))
    region = (HALO, HALO - lift + pad, HALO + CELL, HALO + CELL + pad)
    layer = wall.crop(region).convert("RGBA")
    layer.putalpha(wall_alpha.crop(region))
    frame.alpha_composite(layer, (0, CLIFF_TOP - lift))

    # Broad light over the top, rising toward the rims that face the light.
    shade = dome_shade(coverage, 1.4)
    lighter = surface.point(lambda v: min(255, int(v * 1.06 + 4)))
    darker = surface.point(lambda v: int(v * 0.86))
    top = Image.composite(lighter, Image.composite(surface, darker, shade.point(lambda v: min(255, v * 2))),
                          shade.point(lambda v: max(0, v * 2 - 255)))
    # A crisp lit lip toward the light; the far rims fall into shade.
    rim = coverage.filter(ImageFilter.GaussianBlur(0.8))
    lit = ImageChops.lighter(ImageChops.subtract(rim, shifted(rim, 2, 2)),
                             ImageChops.subtract(rim, shifted(rim, 0, -2))).point(lambda v: min(255, v * 2))
    dark = ImageChops.subtract(rim, shifted(rim, -4, 0)).point(lambda v: min(255, v * 2))
    top = Image.composite(Image.new("RGB", top.size, look["lip"]), top, lit.point(lambda v: v * 3 // 4))
    top = Image.composite(Image.composite(Image.new("RGB", top.size, look["far"]), surface,
                                          Image.new("L", top.size, 150)), top, dark)
    layer = top.crop(cell).convert("RGBA")
    layer.putalpha(own.crop(cell))
    frame.alpha_composite(layer, (0, CLIFF_TOP - lift))
    return frame


@functools.lru_cache(maxsize=96)
def surface_cached(name, colours, interior, filled):
    return MASSES[name]["surface"](colours, interior, filled)


def render_icefall(mask, colours, interior=0, foreign=0):
    return render_mass("icefall", mask, colours, interior, foreign)


# --------------------------------------------------------------------------
# Frames, provenance and CLI
# --------------------------------------------------------------------------


def frame_size(name, index):
    """Native (width, height) of a set's frame."""
    if index >= FRAMES:
        width, height = connected_size(name)
        return width // SCALE, height // SCALE
    return native_size(name), native_size(name)


def render_frame(name, index):
    use_frame(native_size(name))
    rng = random.Random(fnv1a32(f"decor:{name}:{index}"))
    if index < FRAMES:
        hd = SPECS[name].render(rng, index >= FULL).image()
    elif name in CLIFF_STYLES:
        hd = render_cliff(name, *connected_frame(index - FRAMES))
    elif name in MASS_SETS:
        hd = render_mass(name, *connected_frame(index - FRAMES))
    else:
        hd = render_connected(name, *connected_frame(index - FRAMES)[:3])
    native = hd.resize((hd.width // SCALE, hd.height // SCALE), Image.Resampling.BOX)
    return native, hd


def _job(task):
    # A worker renders a run of one material's frames, so they reuse its
    # cached textures.
    name, indices = task
    return [(name, index, native.tobytes(), hd.tobytes())
            for index in indices
            for native, hd in (render_frame(name, index),)]


def synthesize(names, jobs=None):
    jobs = jobs or min(8, os.cpu_count() or 1)
    # Split each material into as many runs as there are workers; strided runs
    # spread the costly filled-cell frames evenly.
    tasks = [(name, range(start, frame_count(name), jobs)) for name in names for start in range(jobs)]
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        rendered = list(pool.map(_job, tasks))
    out = {}
    for group in rendered:
        for name, index, native, hd in group:
            out.setdefault(name, [None] * frame_count(name))[index] = (
                Image.frombytes("RGBA", frame_size(name, index), native),
                Image.frombytes("RGBA", tuple(n * SCALE for n in frame_size(name, index)), hd))
    return out


ORDER = ["boulders", "hedge", "thicket", "ridge_rock", "outcrop", "cliffs", "glacier", "icefall",
         "sandstone_benches", "obsidian_ridge", "granite_tors", "travertine_terraces",
         "penitentes", "termite_mounds", "hoodoos", "petrified_stumps"]
assert sorted(ORDER) == sorted(SPECS)
# The first sets were numbered before any connected frames, which follow them;
# later sets append their object and connected frames together, so adding a set
# never renumbers an existing frame.
LEGACY_SETS = 5


def first_frame(name):
    k = ORDER.index(name)
    if k < LEGACY_SETS:
        return k * FRAMES
    total = LEGACY_SETS * FRAMES + 2 * CONNECTED_FRAMES
    for other in ORDER[LEGACY_SETS:k]:
        total += block_size(other)
        if other == FOREIGN_RETROFIT_AFTER:
            total += len(RETROFIT_FOREIGN) * FOREIGN_FRAMES
    return total


def generator_sha():
    return hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def provenance_path(root):
    return root / "content-src/gfx/terrain-decor/provenance.json"


def write(results, root):
    from highres_pack import register_frames, source_record

    path = provenance_path(root)
    document = json.loads(path.read_text()) if path.exists() else {"sets": {}}
    hd_frames = []
    for name, frames in results.items():
        hashes, hd_hashes = {}, {}
        for i, (native, hd) in enumerate(frames):
            n = frame_number(name, i)
            relative = f"{PREFIX}{n}.png"
            (root / relative).parent.mkdir(parents=True, exist_ok=True)
            native.save(root / relative, optimize=False)
            hashes[relative] = pixel_sha256(native)
            hd_hashes[f"{HD_PREFIX}{n}.png"] = pixel_sha256(hd)
            hd_frames.append({"id": f"terrain-decor{n}", "image": hd, "recipe": f"{HD_RECIPE}: {name}",
                              "sources": [source_record(Path(__file__).resolve(), root)]})
        document["sets"][name] = {
            "description": SPECS[name].note,
            "seed": f'fnv1a32("decor:{name}:<frame>")',
            "full": [first_frame(name) + i for i in range(FULL)],
            "edge": [first_frame(name) + i for i in range(FULL, FRAMES)],
            "connected": list(range(connected_first(name), connected_first(name) + 16 * CANOPY_VARIANTS)) if name in CONNECTED else [],
            "connected_interior": list(range(connected_first(name) + 16 * CANOPY_VARIANTS,
                                             connected_first(name) + CONNECTED_FRAMES)) if name in CONNECTED else [],
            "connected_foreign": list(range(foreign_first(name), foreign_first(name) + FOREIGN_FRAMES))
            if name in OPEN_EDGED else [],
            "native_size": native_size(name),
            "runtime_sha256": hashes,
            "highres_sha256": hd_hashes,
        }
        if name in CONNECTED and connected_size(name) != connected_size("hedge"):
            width, height = connected_size(name)
            document["sets"][name]["connected_native_size"] = [width // SCALE, height // SCALE]
    register_frames(hd_frames, HD_CATEGORY, root)
    document.update({
        "generator": "tools/artwork/terrain_decor.py",
        "method": "procedural",
        "generator_sha256": generator_sha(),
        "native_size": 64,
        "render_size": 64 * SCALE,
        "connected_native_size": list(frame_size(CONNECTED[0], FRAMES)),
        "connected_corners": True,
        "pillow": PIL.__version__,
    })
    document["sets"] = {name: document["sets"][name] for name in ORDER if name in document["sets"]}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n")


def write_catalog(root):
    """Add or refresh each obstacle material's decor block in the catalog."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    import terrain_catalog

    document = terrain_catalog.load(root / "content")
    # Connected foliage keeps the quiet "canopy" edge of the "cover" border
    # class (authored by terrain_synth.py), so no ground contour shows below it.
    for material in document["materials"]:
        if material["key"] in SPECS:
            first = first_frame(material["key"])
            material["decor"] = {
                "sprite": SPRITE,
                "full": list(range(first, first + FULL)),
                "edge": list(range(first + FULL, first + FRAMES)),
            }
            if material["key"] in CLEAR_OF_MASSES:
                material["decor"]["clear_of_masses"] = CLEAR_OF_MASSES[material["key"]]
        if material["key"] in CONNECTED:
            start = connected_first(material["key"])
            material["decor"]["connected"] = list(range(start, start + 16 * CANOPY_VARIANTS))
            material["decor"]["connected_interior"] = list(range(start + 16 * CANOPY_VARIANTS,
                                                                 start + CONNECTED_FRAMES))
            material["decor"].pop("connected_grid", None)
            material["decor"]["connected_corners"] = True
            if material["key"] in OPEN_EDGED:
                material["decor"]["connected_open"] = True
                first = foreign_first(material["key"])
                material["decor"]["connected_foreign"] = list(range(first, first + FOREIGN_FRAMES))
                # Rock-like masses terrace toward the middle of a large patch.
                material["decor"]["connected_tiers"] = True
            else:
                material["decor"].pop("connected_open", None)
                material["decor"].pop("connected_foreign", None)
                material["decor"].pop("connected_tiers", None)
    terrain_catalog.save(root / "content", document)


def connected_sheet(results):
    """Before/after patches, strips, corners, isolated vertices and a hole.

    Reproduce row painter order and corner masks rather than arranging isolated
    swatches. This makes grid stripes and directional joining errors visible.
    """
    width, height = 16, 12
    panel_w, panel_h = width * 32, height * 32
    out = Image.new("RGBA", (panel_w * 2, (panel_h + 28) * len(results)), (32, 35, 30, 255))
    labels = ImageDraw.Draw(out)
    grass = Image.open(ROOT / "content/data/gfx/terrain0.png").convert("RGBA")
    for row, (name, frames) in enumerate(results.items()):
        vertices = {(x, y) for y in range(4, 11) for x in range(2, 15)}
        vertices -= {(x, y) for y in range(6, 9) for x in range(6, 9)}
        vertices |= {(x, 2) for x in range(2, 11)} | {(12, y) for y in range(1, 5)} | {(14, 2)}
        for after in (False, True):
            patch = Image.new("RGBA", (panel_w, panel_h))
            for y in range(height):
                for x in range(width):
                    patch.paste(grass, (x * 32, y * 32))
            for y in range(height):
                for x in range(width):
                    mask = sum(1 << k for k, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1)))
                               if (x + dx, y + dy) in vertices)
                    count = mask.bit_count()
                    if not count or (not after and count < 2):
                        continue
                    variant = (sum(vertex_colour(x + dx, y + dy) << k
                                   for k, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))))
                               if after else fnv1a32(f"{name}:{x}:{y}"))
                    index = (FRAMES + connected_index(x, y, mask, variant) if after else
                             variant % FULL if count == 4 else FULL + variant % EDGE)
                    frame = frames[index][0]
                    px, py = x * 32 - offset_x(frame.width), y * 32 - offset_y(frame.height)
                    patch.alpha_composite(frame, (max(0, px), max(0, py)), (max(0, -px), max(0, -py)))
            oy = row * (panel_h + 28)
            out.paste(patch, (int(after) * panel_w, oy + 28))
            labels.text((int(after) * panel_w + 8, oy + 7), f"{name}: {'connected canopy' if after else 'previous bushes'}", fill="white")
    return out


def sheet(results, ground=None):
    """Review sheet: each frame over its ground tile (or a neutral green), at
    128 display pixels per frame, plus a 4x3 field of full frames drawn in row
    order with overlaps and the runtime's anchoring, at 2x."""
    names = list(results)
    cell = 128
    rows = []
    for name in names:
        frames = results[name]
        n = frames[0][0].width
        bg = (ground or {}).get(name)
        left, top = offset_x(n), offset_y(n)
        bottom = n - 32 - top

        def ground_tile(width, height, ox, oy):
            tile = Image.new("RGBA", (width, height), (82, 120, 64, 255))
            if bg:
                for gy in range(oy % 32 - 32, height, 32):
                    for gx in range(ox % 32 - 32, width, 32):
                        tile.paste(bg, (gx, gy))
            return tile

        strip = Image.new("RGBA", (FRAMES * cell, cell), (36, 36, 40, 255))
        for i, (native, _) in enumerate(frames[:FRAMES]):
            tile = ground_tile(n, n, left, top)
            tile.alpha_composite(native)
            strip.paste(tile.resize((cell, cell), Image.Resampling.NEAREST), (i * cell, 0))
        # Field: 4 x 3 cells, drawn back to front like the runtime.
        field_img = ground_tile(4 * 32 + 2 * left, 3 * 32 + top + bottom, left, top)
        rng = random.Random(fnv1a32(name))
        for fy in range(3):
            for fx in range(4):
                field_img.alpha_composite(frames[rng.randrange(FULL)][0], (fx * 32, fy * 32))
        field_img = field_img.resize((field_img.width * 2, field_img.height * 2), Image.Resampling.NEAREST)
        rows.append((strip, field_img))
    width = max(strip.width + 32 + field.width for strip, field in rows)
    out = Image.new("RGBA", (width, sum(max(cell, field.height) + 24 for _, field in rows)), (36, 36, 40, 255))
    y = 0
    for strip, field in rows:
        out.paste(strip, (0, y))
        out.paste(field, (strip.width + 32, y))
        y += max(cell, field.height) + 24
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--decor", action="append", help="decor name (repeatable; default all)")
    parser.add_argument("--sheet", type=Path, help="write a review sheet and nothing else")
    parser.add_argument("--write-catalog", action="store_true", help="also add decor blocks to the terrain catalog")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    names = args.decor or list(ORDER)
    root = args.root.resolve()
    results = synthesize(names)
    if args.sheet:
        ground = {}
        for name in names:
            path = root / f"content/data/gfx/terrain-{name}0.png"
            if path.exists():
                ground[name] = Image.open(path).convert("RGBA")
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        (connected_sheet(results) if all(n in CONNECTED for n in names) else sheet(results, ground)).save(args.sheet)
        print(f"Wrote {args.sheet}")
        return
    write(results, root)
    if args.write_catalog:
        write_catalog(root)
    print(f"Wrote {len(names)} decor sets ({sum(frame_count(n) for n in names)} frames)")


if __name__ == "__main__":
    main()
