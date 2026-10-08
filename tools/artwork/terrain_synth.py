#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise original procedural terrain materials for the terrain catalog.

Usage (with the pinned asset encoder interpreter, Pillow 12.2.0):

    ENC="$(python3 tools/package_assets.py --encoder-python)"
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

Each material renders sixteen periodic 128x128 variants and box-downsamples them
to 32x32. Ordinary materials render them independently and share ring 0 of the
perimeter across variants (tools/artwork/material_tiles.py) so any two variants
join; periodic materials share an outer band instead, and grid materials
(`variant_grid`) slice one block so each variant continues its neighbours. Animated materials
render several phases per variant (four; sixteen for water and deep water); the
frame layout is `variant + 16 * phase`.
Outputs are `data/gfx/terrain-<name>N.png`, the 128x128 HD frames the classic
tiles were downsampled from (`data/highres/v1/terrain-<name>N.png`, registered in
the pack through tools/artwork/highres_pack.py) and `datasrc/gfx/<name>/provenance.json`.

Pixel-exact reproduction (`--check`) is pinned to the encoder interpreter on
Linux x86-64: besides Pillow's resampling kernels, the renders depend on the C
library's sin/atan2/hypot and on round() boundaries, so another platform may
differ in a few low bits. The provenance records the platform.

Pure Python 3 plus Pillow; no numpy.
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
SYNTHESISED_LEGACY = ["water"]
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


def lattice_noise(rng, cells, cells_y=None):
    """Periodic value noise: a random lattice, tiled 3x3 and bicubically resized.

    Pillow does the interpolation in one C call; the centre crop of the 3x3
    tiling is periodic by construction. Values are clamped to [0, 1].
    """
    cells = max(1, min(N, int(cells)))
    cells_y = max(1, min(N, int(cells_y or cells)))
    values = [rng.random() for _ in range(cells * cells_y)]
    rows = []
    for _ in range(3):
        for y in range(cells_y):
            row = values[y * cells : (y + 1) * cells]
            rows.extend(row * 3)
    image = Image.frombytes("F", (cells * 3, cells_y * 3), struct.pack(f"{len(rows)}f", *rows))
    big = image.resize((N * 3, N * 3), Image.Resampling.BICUBIC)
    crop = big.crop((N, N, 2 * N, 2 * N))
    data = getattr(crop, "get_flattened_data", crop.getdata)()
    return [clamp01(v) for v in data]


def fbm(rng, cells, octaves=3, gain=0.5, lacunarity=2.0, cells_y=None):
    """Fractal sum of lattice noise, centred on 0.5 and roughly in [0.1, 0.9]."""
    total = [0.0] * (N * N)
    amplitude, norm = 1.0, 0.0
    cy = cells_y or cells
    for _ in range(octaves):
        layer = lattice_noise(rng, cells, cy)
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


def sample(values, fx, fy):
    """Bilinear, wrapping sample of a field at a fractional position."""
    x0 = math.floor(fx)
    y0 = math.floor(fy)
    tx = fx - x0
    ty = fy - y0
    x0 %= N
    y0 %= N
    x1 = (x0 + 1) % N
    y1 = (y0 + 1) % N
    top = values[y0 * N + x0] * (1 - tx) + values[y0 * N + x1] * tx
    bottom = values[y1 * N + x0] * (1 - tx) + values[y1 * N + x1] * tx
    return top * (1 - ty) + bottom * ty


def domain_warp(values, warp_x, warp_y, amount, amount_y=None):
    """Re-sample `values` displaced by two noise fields (±`amount` pixels)."""
    amount_y = amount if amount_y is None else amount_y
    return [
        sample(values, x + (wx - 0.5) * 2 * amount, y + (wy - 0.5) * 2 * amount_y)
        for x, y, wx, wy in zip(XS, YS, warp_x, warp_y)
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


def _worley_from_points(points, metric, rotations=None, stretch=1.0):
    """Nearest and second-nearest wrapped distance (in pixels) and nearest id."""
    f1 = [0.0] * (N * N)
    f2 = [0.0] * (N * N)
    ids = [0] * (N * N)
    half = N / 2
    chebyshev = metric == "chebyshev"
    angular = metric == "angular"
    for i in range(N * N):
        x, y = XS[i], YS[i]
        best, second, best_id = 1e9, 1e9, 0
        for pid, (px, py) in enumerate(points):
            dx = px - x
            if dx > half:
                dx -= N
            elif dx < -half:
                dx += N
            dy = py - y
            if dy > half:
                dy -= N
            elif dy < -half:
                dy += N
            if angular:
                d = _rotated_distance(dx, dy, rotations[pid], stretch)
            elif chebyshev:
                d = max(abs(dx), abs(dy))
            else:
                d = math.sqrt(dx * dx + dy * dy)
            if d < best:
                second, best, best_id = best, d, pid
            elif d < second:
                second = d
        f1[i], f2[i], ids[i] = best, second, best_id
    return f1, f2, ids


def worley_points(rng, count, metric="euclid", stretch=1.0):
    """Worley cells from `count` free points; distances in units of the mean cell.

    `angular` rotates a Chebyshev metric by a random angle per point (and
    optionally stretches it), giving angular chips without a shared orientation.
    """
    points = [(rng.random() * N, rng.random() * N) for _ in range(count)]
    rotations = None
    if metric == "angular":
        rotations = []
        for _ in range(count):
            angle = rng.random() * TAU
            rotations.append((math.cos(angle), math.sin(angle)))
    f1, f2, ids = _worley_from_points(points, metric, rotations, stretch)
    cell = N / math.sqrt(count)
    return [v / cell for v in f1], [v / cell for v in f2], ids, points


def worley(rng, cells, metric="euclid", jitter=1.0):
    """Jittered-grid Worley cells; F1/F2 in units of the cell size.

    Each pixel examines the nine surrounding cells with wrapped indices, so the
    field is periodic and the cost stays linear in the canvas size. Metrics:
    `euclid` (round cells), `chebyshev` (axis-aligned squares) and `angular`
    (a Chebyshev metric rotated by a random angle per cell).
    """
    cells = max(1, int(cells))
    size = N / cells
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
    f1 = [0.0] * (N * N)
    f2 = [0.0] * (N * N)
    ids = [0] * (N * N)
    chebyshev = metric == "chebyshev"
    angular = metric == "angular"
    for i in range(N * N):
        x, y = XS[i], YS[i]
        cx0 = int(x / size)
        cy0 = int(y / size)
        best, second, best_id = 1e9, 1e9, 0
        for oy in (-1, 0, 1):
            cy = (cy0 + oy) % cells
            wrap_y = ((cy0 + oy) // cells) * N
            for ox in (-1, 0, 1):
                cx = (cx0 + ox) % cells
                wrap_x = ((cx0 + ox) // cells) * N
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
    r = int(math.ceil(radius)) + 1
    inner = radius * (1 - softness)
    for oy in range(-r, r + 1):
        uy = int(math.floor(cy)) + oy
        y = uy % N
        for ox in range(-r, r + 1):
            ux = int(math.floor(cx)) + ox
            x = ux % N
            # Distances use the unwrapped pixel so stamps wrap across edges.
            d = math.hypot(ux + 0.5 - cx, uy + 0.5 - cy)
            if d >= radius:
                continue
            weight = 1.0 if d <= inner else 1 - smoothstep(inner, radius, d)
            i = y * N + x
            if mode == "max":
                if weight * value > mask[i]:
                    mask[i] = weight * value
            else:
                mask[i] = clamp01(mask[i] + weight * value)


def disc_profile(cx, cy, radius, lumps=()):
    """Dome height field of one disc (1 at the centre, 0 at the rim), wrapped.

    `lumps` is a list of (harmonic, amplitude, phase) radius modulations that
    turn the circle into an irregular stone outline.
    """
    height = [0.0] * (N * N)
    r = int(math.ceil(radius * 1.4)) + 1
    for oy in range(-r, r + 1):
        uy = int(math.floor(cy)) + oy
        y = uy % N
        for ox in range(-r, r + 1):
            ux = int(math.floor(cx)) + ox
            x = ux % N
            dx, dy = ux + 0.5 - cx, uy + 0.5 - cy
            angle = math.atan2(dy, dx)
            local = radius * (1 + sum(a * math.sin(h * angle + ph) for h, a, ph in lumps))
            d = math.hypot(dx, dy) / local
            if d < 1:
                height[y * N + x] = max(height[y * N + x], math.sqrt(1 - d * d))
    return height


def scatter(ctx, count, radius_min, radius_max, softness=0.5):
    """Random soft discs inside the tile, clear of the perimeter band.

    Returns the union mask and (x, y, r, u) tuples, where `u` is a uniform
    value for per-disc choices. Keeping stamps out of the band means the
    neutral perimeter never cuts or ghosts a feature.
    """
    margin = STAMP_MARGIN + radius_max
    mask = [0.0] * (N * N)
    discs = []
    for _ in range(count):
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


def render_domes(objects, tone_of, lumps_of=None, texture=None):
    """Painter-ordered domes: (height, rgb) fields from objects.

    `texture(u, angle, h)` optionally scales the tone per pixel, from the
    pixel's angle around the object centre and its dome height."""
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
    return [values[((y - dy) % N) * N + (x - dx) % N] for x, y in zip(XS, YS)]


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


def flat(color):
    return [float(color[0])] * (N * N), [float(color[1])] * (N * N), [float(color[2])] * (N * N)


def mix(rgb, color_or_rgb, mask, strength=1.0):
    """Blend toward a colour (or another RGB triple of fields) by mask*strength."""
    if isinstance(color_or_rgb, tuple) and len(color_or_rgb) == 3 and not isinstance(color_or_rgb[0], list):
        target = flat(color_or_rgb)
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


def gradient_light(height, light=(-0.7, -0.7)):
    """Normalised directional gradient of a height field in [-1, 1]."""
    lx, ly = light
    g = [0.0] * (N * N)
    for i in range(N * N):
        x, y = XS[i], YS[i]
        gx = height[y * N + (x + 1) % N] - height[y * N + (x - 1) % N]
        gy = height[((y + 1) % N) * N + x] - height[((y - 1) % N) * N + x]
        g[i] = gx * lx + gy * ly
    rms = math.sqrt(sum(v * v for v in g) / len(g)) or 1.0
    return [max(-1.0, min(1.0, v / (2.5 * rms))) for v in g]


def shade(rgb, height, strength, light=(-0.7, -0.7)):
    """Bevel: brighten slopes facing `light`, darken the others."""
    g = gradient_light(height, light)
    return [[c * (1 - strength * v) for c, v in zip(channel, g)] for channel in rgb]


def grain(rgb, rng, amount=0.12, cells=48):
    """Fine per-pixel grain, as the native tiles have (about one native pixel)."""
    fine = lattice_noise(rng, cells)
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
    # Positional variants ("variant_grid"): the sixteen variants are the cells
    # of one periodic GRID x GRID block, variant `gx + GRID * gy`, which the
    # runtime repeats by cell position. Requires `periodic`; the renderer gets
    # the block's cell from `ctx.variant` and continues across every edge.
    grid: int = 0
    note: str = ""

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


class Ctx:
    """Per-render context: geometry rng, per-phase rng and phase time."""

    def __init__(self, name, variant, phase, phases):
        self.name, self.variant, self.phase, self.phases = name, variant, phase, phases
        self.rng = random.Random(fnv1a32(f"{name}:{variant}:0"))
        self.phase_rng = random.Random(fnv1a32(f"{name}:{variant}:{phase}"))
        self.t = phase / phases
        # Shared by every variant of a periodic material (and every phase).
        self.base_rng = random.Random(fnv1a32(f"{name}:base"))

    def interior_point(self, margin):
        """A position at least `margin` render pixels from every edge."""
        span = max(1.0, N - 2 * margin)
        return margin + self.rng.random() * span, margin + self.rng.random() * span


def ground(ctx, cells, octaves, anchors, warp=0.0, stretch=(0.02, 0.98), grit=0.12):
    """Warped fractal noise through a dark/mid/light ramp: the painterly base."""
    rng = ctx.rng
    f = fbm(rng, cells, octaves)
    if warp:
        f = domain_warp(f, fbm(rng, 3, 2), fbm(rng, 3, 2), warp)
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
    cores = [0.0] * (N * N)
    if core:
        for cx, cy, _, _ in discs:
            stamp_disc(cores, cx, cy, core, 1.0, 0.5)
    return halo, cores, discs


# Barren ground: muted warm neutrals, inhibits growth like sand but is not a shore.

@recipe(name="dirt", group="barren", label="Dirt", profile="soft",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (104, 82, 56), "mid": (136, 108, 74), "light": (160, 132, 96), "pebble": (86, 68, 48)},
        style=Style(luma=116, std=9, grain_max=8))
def render_dirt(ctx):
    p = RECIPES["dirt"].palette
    rgb, _ = ground(ctx, 5, 4, (p["dark"], p["mid"], p["light"]), warp=10)
    pebbles, _ = scatter(ctx, 40, 1.6, 2.6, softness=0.6)
    rgb = mix(rgb, p["pebble"], pebbles, 0.8)
    return rgb, None


@recipe(name="clay", group="barren", label="Clay", profile="soft",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (134, 88, 60), "mid": (166, 114, 80), "light": (186, 138, 100), "crack": (116, 74, 52)},
        style=Style(luma=124, std=8, grain_max=8))
def render_clay(ctx):
    p = RECIPES["clay"].palette
    rng = ctx.rng
    f = fbm(rng, 4, 3)
    f1, f2, _, _ = worley_points(rng, 6)
    wx, wy = fbm(rng, 4, 2), fbm(rng, 4, 2)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 5)
    plates = normalize(domain_warp([1 - v for v in f1], wx, wy, 5))
    f = normalize([0.6 * a + 0.4 * b for a, b in zip(f, plates)])
    rgb = ramp(f, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = grain(rgb, rng, 0.08)
    cracks = band(edge, 0.03, 0.05)
    rgb = mix(rgb, p["crack"], cracks, 0.45)
    return rgb, None


@recipe(name="gravel", group="barren", label="Gravel", profile="rock",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"fines": (98, 86, 70), "gap": (50, 43, 36),
                 "stones": [(168, 156, 136), (146, 138, 126), (178, 162, 136), (128, 122, 114),
                            (160, 140, 118), (132, 133, 134), (200, 188, 162), (156, 128, 104),
                            (176, 150, 120), (214, 202, 176)]},
        style=Style(luma=128, std=32, grain_max=26, match=0.8), periodic=True)
def render_gravel(ctx):
    """Rounded pebbles, after close-up photographs of pebble beds: a dark bed
    of fines under packed, overlapping stones of mixed grey and warm tones,
    each lit from the upper left with a dark contact shadow to the lower
    right, and a few larger stones on top."""
    p = RECIPES["gravel"].palette
    base = ctx.base_rng
    rgb = ramp(normalize(fbm(base, 12, 2)), [(0.0, p["gap"]), (1.0, p["fines"])])
    speck = lattice_noise(base, 64)

    def tone(u):
        colour = p["stones"][int(u * 997) % len(p["stones"])]
        value = 0.86 + 0.26 * ((u * 7919) % 1.0)
        return [c * value for c in colour]

    def lumps(u):
        return [(h, (0.05 + 0.06 * ((u * 31 * h) % 1.0)) * 3 / (h + 1), u * TAU * h) for h in (2, 3, 5)]

    # Grit, stones and a few large stones: a size hierarchy with the dark bed
    # showing between them, which is what separates stones at 32 px.
    for count, radius, spacing in ((40, (4.0, 6.0), 1.05), (18, (8.0, 11.0), 1.0), (3, (11.0, 14.0), 0.95)):
        shared, own = object_field(ctx, count, radius, spacing=spacing)
        height, stones = render_domes(shared + own, tone, lumps)
        stones = shade(stones, height, 0.5)
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
        style=Style(luma=92, std=24, grain_max=18, match=0.3), periodic=True)
def render_flower_meadow(ctx):
    """Grass with readable flower heads, after wildflower-meadow photographs:
    patches of one colour plus scattered singles, each head a ring of
    saturated petals around a contrasting centre with a soft shadow."""
    p = RECIPES["flower_meadow"].palette
    base = ctx.base_rng
    grass = ramp(normalize(fbm(base, 8, 3)), [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    blades = lattice_noise(base, 48)
    grass = [[c * (0.88 + 0.24 * v) for c, v in zip(channel, blades)] for channel in grass]
    rgb = grass

    def bloom(rng, rgb, centres, inside):
        masks = {k: [0.0] * (N * N) for k in ("petal", "centre", "shadow")}
        petal_rgb = [[0.0] * (N * N) for _ in range(3)]
        centre_rgb = [[0.0] * (N * N) for _ in range(3)]
        for cx, cy, colour, centre_colour in centres:
            radius = rng.uniform(6.5, 8.0)
            local = {k: [0.0] * (N * N) for k in masks}
            stamp_flower(local, cx, cy, radius, rng.choice((4, 5, 5, 6)), rng.uniform(0, TAU))
            for k in masks:
                masks[k] = [max(a, b) for a, b in zip(masks[k], local[k])]
            for i, v in enumerate(local["petal"]):
                if v:
                    for k in range(3):
                        petal_rgb[k][i] = colour[k]
            for i, v in enumerate(local["centre"]):
                if v:
                    for k in range(3):
                        centre_rgb[k][i] = centre_colour[k]
        if inside is not None:
            for k in masks:
                masks[k] = [m * w for m, w in zip(masks[k], inside)]
        rgb = mix(rgb, p["shadow"], masks["shadow"], 0.55)
        rgb = mix(rgb, shade(petal_rgb, masks["petal"], 0.25), masks["petal"], 1.0)
        rgb = mix(rgb, centre_rgb, masks["centre"], 1.0)
        return rgb

    def patch_centres(rng, patches, singles, area):
        out = []
        for _ in range(patches):
            px, py = area()
            colour = rng.choice(p["flowers"])
            centre = rng.choice(p["centres"])
            # Blooms in a drift keep about one bloom of grass between them, so
            # at 32 px each stays a separate round dot instead of merging.
            drift = []
            for _ in range(rng.randint(3, 6) * 8):
                if len(drift) >= 6:
                    break
                a, d = rng.uniform(0, TAU), rng.uniform(0, 18)
                x, y = px + math.cos(a) * d, py + math.sin(a) * d
                if all(math.hypot(x - qx, y - qy) > 15 for qx, qy in drift):
                    drift.append((x, y))
            out.extend((x, y, colour, centre) for x, y in drift)
        for _ in range(singles):
            x, y = area()
            out.append((x, y, rng.choice(p["flowers"]), rng.choice(p["centres"])))
        return out

    # Shared periodic flowers (anywhere; stamps wrap), then per-variant patches.
    rng = ctx.rng
    own = patch_centres(rng, rng.randint(2, 3), rng.randint(1, 2),
                        lambda: ctx.interior_point(PERIODIC_FADE + 8))
    rgb = bloom(rng, rgb, own, interior_weight(PERIODIC_BAND, 4))
    return rgb, None


# Rough ground: slow going; cool and desaturated apart from mud.

@recipe(name="mud", group="rough", label="Mud", profile="soft",
        seam={"height": 1},
        palette={"dark": (62, 48, 36), "mid": (88, 70, 52), "light": (116, 96, 74), "sheen": (150, 132, 112)},
        style=Style(luma=76, std=8, grain_max=7))
def render_mud(ctx):
    p = RECIPES["mud"].palette
    rgb, _ = ground(ctx, 4, 4, (p["dark"], p["mid"], p["light"]), warp=14)
    # Two or three soft specular pools so the surface reads wet.
    pools, _ = scatter(ctx, ctx.rng.randint(2, 3), 16, 24, softness=0.85)
    rgb = mix(rgb, p["sheen"], pools, 0.4)
    return rgb, None


@recipe(name="marsh", group="rough", label="Marsh", profile="soft",
        seam={"height": 1},
        palette={"tussock": (138, 140, 72), "dry": (176, 156, 96), "green": (100, 124, 58),
                 "moss": (78, 92, 52), "water": (112, 128, 146), "sky": (156, 170, 184),
                 "rim": (46, 50, 40), "reed": (132, 100, 60)},
        style=Style(luma=104, std=22, grain_max=16, match=0.45), periodic=True)
def render_marsh(ctx):
    """Tussock marsh after photographs of bogs and reed beds: flat tussocks
    of short straw and olive blades on lighter wet moss, with a few larger
    pools that reflect the sky (light grey-blue with a thin dark rim) and
    reed tufts."""
    p = RECIPES["marsh"].palette
    base = ctx.base_rng
    rgb = ramp(normalize(fbm(base, 6, 3)), [(0.0, p["moss"]), (0.6, p["green"]), (1.0, p["tussock"])])
    rng = ctx.rng
    shared, own = object_field(ctx, 22, (6.0, 9.5), spacing=0.9)
    blades = [0.0] * (N * N)
    blade_rgb = [[0.0] * (N * N) for _ in range(3)]
    for x, y, r, u in sorted(shared + own, key=lambda o: o[1]):
        tone = [mix_channel(p["green"][k], p["tussock"][k], p["dry"][k], (u * 5.3) % 1.0) for k in range(3)]
        local = [0.0] * (N * N)
        # Short blades fanning upward and outward from the tussock base.
        for j in range(9):
            t = (u * 97 + j * 0.618) % 1.0
            angle = -math.pi / 2 + (t - 0.5) * 2.6
            length = r * (0.8 + 0.6 * ((u * 13 + j * 0.37) % 1.0))
            stamp_stroke(local, x, y + r * 0.35, angle, length, 2.2)
        shade_ = 0.85 + 0.3 * ((u * 7.7) % 1.0)
        for i, v in enumerate(local):
            if v > blades[i]:
                blades[i] = v
                for k in range(3):
                    blade_rgb[k][i] = tone[k] * shade_ * (0.85 + 0.25 * v)
    rgb = mix(rgb, p["moss"], [b * 0.6 for b in shifted(blades, 2, 2)], 0.5)
    rgb = mix(rgb, blade_rgb, blades, 1.0)
    pools = [0.0] * (N * N)
    for _ in range(rng.randint(1, 2)):
        radius = rng.uniform(12, 18)
        cx, cy = ctx.interior_point(PERIODIC_FADE + radius + 2)
        lumps_ = [(h, rng.uniform(0.08, 0.16), rng.uniform(0, TAU)) for h in (2, 3, 4)]
        pool = disc_profile(cx, cy, radius, lumps_)
        pools = [max(a, b) for a, b in zip(pools, pool)]
    rim = [smoothstep(0.0, 0.12, v) for v in pools]
    inner = [smoothstep(0.12, 0.3, v) for v in pools]
    water = mix(flat(p["water"]), p["sky"], normalize(fbm(rng, 4, 2)), 0.6)
    rgb = mix(rgb, p["rim"], rim, 0.9)
    rgb = mix(rgb, water, inner, 1.0)
    reeds = [0.0] * (N * N)
    for _ in range(rng.randint(1, 2)):
        cx, cy = ctx.interior_point(PERIODIC_FADE + 10)
        for _ in range(rng.randint(5, 9)):
            a = -math.pi / 2 + rng.uniform(-0.6, 0.6)
            x0, y0 = cx + rng.uniform(-5, 5), cy + rng.uniform(-2, 4)
            stamp_stroke(reeds, x0, y0, a, rng.uniform(7, 12), 1.8)
    rgb = mix(rgb, p["rim"], shifted(reeds, 2, 2), 0.35)
    rgb = mix(rgb, p["reed"], reeds, 0.9)
    return rgb, None


def mix_channel(a, b, c, t):
    """Three-stop ramp for one channel at t in [0, 1]."""
    return a + (b - a) * (t / 0.5) if t < 0.5 else b + (c - b) * ((t - 0.5) / 0.5)


@recipe(name="deep_snow", group="rough", label="Deep snow", profile="soft",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512, "fringe": [196, 210, 232], "fringe_q8": 64,
              "fringe_width_q8": 384},
        palette={"dark": (156, 170, 210), "mid": (204, 208, 234), "light": (236, 236, 250), "sparkle": (252, 252, 255)},
        style=Style(luma=203, std=9, grain_max=6))
def render_deep_snow(ctx):
    p = RECIPES["deep_snow"].palette
    rgb, f = ground(ctx, 3, 3, (p["dark"], p["mid"], p["light"]), grit=0.05)
    rgb = shade(rgb, f, 0.12, light=(-0.6, -0.8))
    sparkle, _, _ = soft_blobs(ctx, 6, 3.0, 4.0)
    rgb = mix(rgb, p["sparkle"], sparkle, 0.6)
    return rgb, None


@recipe(name="scree", group="rough", label="Scree", profile="rock",
        seam={"height": 1},
        palette={"dark": (84, 86, 92), "mid": (118, 120, 124), "light": (152, 154, 156), "dirt": (62, 56, 50)},
        style=Style(luma=118, std=14, grain_max=12))
def render_scree(ctx):
    p = RECIPES["scree"].palette
    rng = ctx.rng
    # Twelve to sixteen angular chips in random orientations, slightly
    # stretched, with a hard bevel along their rims and dark dirt between them.
    f1, f2, ids, _ = worley_points(rng, rng.randint(12, 16), metric="angular", stretch=1.35)
    edge = [b - a for a, b in zip(f1, f2)]
    grit = fbm(rng, 10, 2)
    tone = normalize([0.25 * (1 - a) + 0.75 * g for a, g in zip(f1, grit)], 0.05, 0.95)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(rng, 16, 0.14))
    top = [smoothstep(0.08, 0.30, e) for e in edge]
    rgb = shade(rgb, top, 0.45, light=(-0.7, -0.7))
    gaps = band(edge, 0.06, 0.06)
    rgb = mix(rgb, p["dirt"], gaps, 0.85)
    rgb = grain(rgb, rng, 0.08)
    return rgb, None


# Paths: warm and light, faster movement.

@recipe(name="dirt_track", group="paths", label="Dirt track", profile="sand",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (142, 116, 82), "mid": (170, 142, 104), "light": (192, 166, 126)},
        style=Style(luma=146, std=7, grain_max=6))
def render_dirt_track(ctx):
    p = RECIPES["dirt_track"].palette
    rng = ctx.rng
    rgb, _ = ground(ctx, 4, 3, (p["dark"], p["mid"], p["light"]), warp=8)
    worn, _ = scatter(ctx, 3, 14, 22, softness=0.9)
    rgb = mix(rgb, p["light"], worn, 0.5)
    # Two faint parallel ruts across the tile (one direction, like the
    # boardwalk planks), a few luma darker, wrapping in x.
    centre = 40 + rng.random() * 16
    wobble = fbm(rng, 2, 2)
    ruts = []
    for i in range(N * N):
        y = YS[i] + (wobble[i] - 0.5) * 6
        d = min(abs(y - centre), abs(y - centre - 32))
        ruts.append(1 - smoothstep(2.0, 6.0, d))
    rgb = [[c * (1 - 0.04 * r) for c, r in zip(channel, ruts)] for channel in rgb]
    return rgb, None


@recipe(name="boardwalk", group="paths", label="Boardwalk", profile="crisp",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (108, 90, 68), "mid": (156, 134, 104), "light": (182, 160, 128), "gap": (76, 60, 44)},
        style=Style(luma=134, std=11, grain_max=10, match=0.6),
        note="planks run one way: accepted compromise")
def render_boardwalk(ctx):
    p = RECIPES["boardwalk"].palette
    rng = ctx.rng
    planks = 4
    height = N // planks
    wood = normalize(fbm(rng, 2, 3, cells_y=24))
    tone = [0.0] * (N * N)
    gaps = [0.0] * (N * N)
    shifts = [int(rng.random() * N) for _ in range(planks)]
    brightness = [1 + (rng.random() - 0.5) * 0.18 for _ in range(planks)]
    joints = [rng.random() * N for _ in range(planks)]
    for i in range(N * N):
        x, y = XS[i], YS[i]
        k = y // height
        tone[i] = clamp01(wood[y * N + (x + shifts[k]) % N] * brightness[k])
        edge = min(y - k * height, (k + 1) * height - y)
        gap = 1 - smoothstep(1.0, 3.0, edge + 0.5)
        jx = abs(((x + 0.5 - joints[k]) + N / 2) % N - N / 2)
        gap = max(gap, 1 - smoothstep(1.0, 2.5, jx))
        gaps[i] = gap
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = grain(rgb, rng, 0.08)
    rgb = mix(rgb, p["gap"], gaps, 0.85)
    return rgb, None


# Fertile ground: warm, saturated undertone; the new strategic resource.

@recipe(name="loam", group="fertile", label="Loam", profile="soft",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (52, 38, 24), "mid": (74, 58, 36), "light": (98, 80, 52), "fleck": (96, 108, 56)},
        style=Style(luma=66, std=8, grain_max=8))
def render_loam(ctx):
    p = RECIPES["loam"].palette
    rgb, _ = ground(ctx, 5, 4, (p["dark"], p["mid"], p["light"]), warp=8)
    # Sparse soft green-brown flecks, about two native pixels.
    flecks, _, _ = soft_blobs(ctx, 22, 4.0, 5.5)
    rgb = mix(rgb, p["fleck"], flecks, 0.8)
    return rgb, None


@recipe(name="moss", group="fertile", label="Moss", profile="soft",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (26, 60, 38), "mid": (38, 84, 50), "light": (62, 110, 68)},
        style=Style(luma=68, std=7, grain_max=6))
def render_moss(ctx):
    p = RECIPES["moss"].palette
    rng = ctx.rng
    f1, _, _, _ = worley_points(rng, 12)
    pillows = normalize([1 - v for v in f1])
    detail = fbm(rng, 6, 3)
    h = normalize([0.6 * a + 0.4 * b for a, b in zip(pillows, detail)])
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, h, 0.2)
    rgb = grain(rgb, rng, 0.10)
    return rgb, None


@recipe(name="spring_meadow", group="fertile", label="Spring meadow", profile="brush",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (78, 118, 50), "mid": (104, 146, 66), "light": (134, 170, 90), "dot": (236, 228, 170)},
        style=Style(luma=105, std=7, grain_max=7))
def render_spring_meadow(ctx):
    p = RECIPES["spring_meadow"].palette
    rgb, _ = ground(ctx, 8, 3, (p["dark"], p["mid"], p["light"]))
    rgb = desaturate(rgb, 0.18)
    dots, _, _ = soft_blobs(ctx, 10, 4.0, 5.5)
    rgb = mix(rgb, p["dot"], dots, 0.7)
    return rgb, None


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


def wave_fields(phase, phases=WAVE_PHASES):
    """(swell, chop, front) on the WAVE_SIZE block for one phase: swell and chop
    in [-1, 1], and the swell's slope along its travel, positive on the leading
    face that catches the light."""
    key = (phase, phases)
    if key in _WAVE_CACHE:
        return _WAVE_CACHE[key]
    rng = random.Random(fnv1a32("water:waves"))
    swell = [(kx, ky, c, a, rng.randrange(WAVE_SIZE)) for kx, ky, c, a in WAVE_SWELL]
    chop = [(kx, ky, c, a, rng.randrange(WAVE_SIZE)) for kx, ky, c, a in WAVE_CHOP]
    warp = [(kx, ky, a, rng.randrange(WAVE_SIZE)) for kx, ky, a in WAVE_WARP]
    size, table, quarter = WAVE_SIZE, WAVE_SIN, WAVE_SIZE // 4
    shift = size // phases

    def warp_at(x, y, axis):
        return int(round(sum(a * table[(kx * x + ky * y + ph + quarter * axis * (i + 1)) % size]
                             for i, (kx, ky, a, ph) in enumerate(warp))))

    out = []
    for comps in (swell, chop):
        total = sum(c[3] for c in comps)
        terms = [(kx, ky, a / total, ph - c * phase * shift) for kx, ky, c, a, ph in comps]
        values = [0.0] * (size * size)
        for y in range(size):
            row = y * size
            for x in range(size):
                xw, yw = x + warp_at(x, y, 0), y + warp_at(x, y, 1)
                values[row + x] = sum(a * table[(kx * xw + ky * yw + ph) % size] for kx, ky, a, ph in terms)
        out.append(values)
    swell_values = out[0]
    front = [0.0] * (size * size)
    for y in range(size):
        up, down, row = ((y - 1) % size) * size, ((y + 1) % size) * size, y * size
        for x in range(size):
            ahead = swell_values[row + (x + 2) % size] + swell_values[down + x]
            behind = swell_values[row + (x - 2) % size] + swell_values[up + x]
            front[row + x] = (behind - ahead) * 6
    out.append(front)
    _WAVE_CACHE.clear()  # One phase per render job; keep a single block resident.
    _WAVE_CACHE[key] = tuple(out)
    return _WAVE_CACHE[key]


def wave_cell(ctx, values):
    """The N x N cell of a block field that variant `ctx.variant` shows."""
    gx, gy = ctx.variant % WAVE_GRID, ctx.variant // WAVE_GRID
    out = []
    for y in range(gy * N, gy * N + N):
        row = y * WAVE_SIZE + gx * N
        out.extend(values[row:row + N])
    return out


def render_waves(ctx, palette, swell_weight, chop_weight, glint, front):
    """Swell bands lit on their leading face, with glints where chop crests
    ride swell crests."""
    s, c, f = (wave_cell(ctx, values) for values in wave_fields(ctx.phase, ctx.phases))
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
        style=Style(luma=38, std=4, grain_max=5, match=0.5),
        preview=(32, 26, 76), minimap=(16, 12, 52))
def render_dark_water(ctx):
    p = RECIPES["dark_water"].palette
    rng = ctx.rng
    murk = normalize(fbm(rng, 3, 2))
    rgb = mix(flat(p["tint"]), p["murk"], murk, 0.6)
    specks, _, _ = soft_blobs(ctx, 8, 3.0, 4.5)
    rgb = mix(rgb, p["speck"], specks, 0.6)
    return rgb, None


@recipe(name="ridge_rock", group="ridges", label="Ridge", profile="rock",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={"dark": (72, 72, 76), "mid": (104, 104, 106), "light": (138, 138, 136), "crack": (46, 46, 50)},
        style=Style(luma=102, std=12, grain_max=10))
def render_ridge_rock(ctx):
    p = RECIPES["ridge_rock"].palette
    rng = ctx.rng
    # Stretched, broken strata: anisotropic noise warped along one axis.
    strata = fbm(rng, 2, 3, cells_y=8, gain=0.4)
    strata = domain_warp(strata, fbm(rng, 3, 2), fbm(rng, 3, 2), 12, 3)
    base = fbm(rng, 5, 3)
    h = normalize([0.75 * s + 0.25 * b for s, b in zip(strata, base)], 0.03, 0.97)
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = grain(rgb, rng, 0.08)
    # Sparse cracks of varying width along the strata.
    ridged = [abs(2 * v - 1) for v in domain_warp(fbm(rng, 2, 2, cells_y=5), fbm(rng, 3, 2), fbm(rng, 3, 2), 8, 3)]
    widths = fbm(rng, 3, 2)
    cracks = [1 - smoothstep(0.0, 0.05 + 0.10 * w, r) for r, w in zip(ridged, widths)]
    rgb = shade(rgb, h, 0.7, light=(-1.0, -0.3))
    rgb = mix(rgb, p["crack"], cracks, 0.6)
    return rgb, None


@recipe(name="outcrop", group="ridges", label="Outcrop", profile="rock",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={"dark": (80, 80, 76), "mid": (110, 112, 108), "light": (140, 142, 136), "lichen": (120, 130, 70)},
        style=Style(luma=108, std=12, grain_max=10),
        note="placeholder until the image-generated outcrop lands")
def render_outcrop(ctx):
    p = RECIPES["outcrop"].palette
    rng = ctx.rng
    f1, f2, ids, _ = worley_points(rng, 5)
    wx, wy = fbm(rng, 4, 2), fbm(rng, 4, 2)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 6)
    grit = fbm(rng, 10, 3)
    tone = normalize(grit, 0.05, 0.95)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(rng, 5, 0.10))
    # Slabs are flat with a bevel along their rims; the rim height comes from
    # the distance to the nearest other slab.
    rim = [smoothstep(0.0, 0.3, e) for e in edge]
    rgb = shade(rgb, rim, 0.4, light=(-0.8, -0.8))
    gaps = band(edge, 0.03, 0.05)
    rgb = mix(rgb, p["dark"], gaps, 0.65)
    rgb = grain(rgb, rng, 0.10)
    lichen, _ = scatter(ctx, 10, 2.6, 4.2, softness=0.6)
    lichen = [l * (1 - g) for l, g in zip(lichen, gaps)]
    rgb = mix(rgb, p["lichen"], lichen, 0.75)
    return rgb, None


# Void: nothing crosses, nothing flies. The depth cue is a cast onto every
# neighbour (rank above all other materials), since a hole this dark cannot
# show a lip of its own.

@recipe(name="void_hole", group="void", label="Hole", profile="crisp",
        seam={"height": 8, "cast_q8": 96, "cast_width_q8": 512},
        palette={"dark": (6, 6, 10), "mid": (10, 10, 14), "light": (14, 14, 20)},
        style=Style(luma=10, std=2, grain_max=3),
        preview=(28, 24, 36), minimap=(16, 14, 22))
def render_void_hole(ctx):
    p = RECIPES["void_hole"].palette
    rgb, _ = ground(ctx, 3, 2, (p["dark"], p["mid"], p["light"]), grit=0.0)
    return rgb, None


@recipe(name="chasm", group="void", label="Chasm", profile="rock",
        seam={"height": 8, "cast_q8": 96, "cast_width_q8": 512},
        palette={"dark": (26, 20, 24), "mid": (46, 38, 42), "light": (74, 64, 66), "floor": (6, 4, 8)},
        style=Style(luma=40, std=9, grain_max=8),
        preview=(48, 38, 44), minimap=(30, 22, 28))
def render_chasm(ctx):
    p = RECIPES["chasm"].palette
    rng = ctx.rng
    # Rock walls as stretched strata lit hard from the left, with one or two
    # near-black floor channels (elongated Worley lows) between them.
    strata = domain_warp(fbm(rng, 2, 3, cells_y=6), fbm(rng, 3, 2), fbm(rng, 3, 2), 8, 3)
    base = fbm(rng, 4, 3)
    h = normalize([0.6 * s + 0.4 * b for s, b in zip(strata, base)], 0.03, 0.97)
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, h, 0.7, light=(-1.0, 0.0))
    rgb = grain(rgb, rng, 0.08)
    f1, _, ids, _ = worley_points(rng, 6, metric="angular", stretch=2.6)
    deep = [1.0 if k < 2 else 0.0 for k in rng.sample(range(6), 6)]
    floor = [(1 - smoothstep(0.18, 0.34, d)) * deep[i] for d, i in zip(f1, ids)]
    rgb = mix(rgb, p["floor"], floor, 0.9)
    return rgb, None


# Obstacles: height cues (lit top, cast shadow); placeholders for image-generated art.

@recipe(name="boulders", group="obstacles", label="Boulders", profile="rock",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"ground_dark": (112, 100, 84), "ground": (128, 114, 96), "ground_light": (144, 130, 110),
                 "rock_dark": (84, 88, 92), "rock_light": (138, 142, 142), "shadow": (40, 36, 34)},
        style=Style(luma=108, std=14, grain_max=12, match=0.5),
        note="placeholder until the image-generated boulders land")
def render_boulders(ctx):
    """Dusty ground under the raised boulder decor: quiet warm earth with fine
    grit and a few faint stones, so the decor sprites carry the boulders."""
    p = RECIPES["boulders"].palette
    rgb, _ = ground(ctx, 6, 3, (p["ground_dark"], p["ground"], p["ground_light"]), warp=6, grit=0.1)
    pebbles, _, _ = soft_blobs(ctx, 10, 2.5, 4.0)
    rgb = mix(rgb, p["ground_dark"], pebbles, 0.35)
    return rgb, None


@recipe(name="hedge", group="obstacles", label="Hedge", profile="brush",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"dark": (22, 50, 24), "mid": (36, 76, 36), "light": (64, 108, 52), "gap": (10, 26, 14)},
        style=Style(luma=56, std=10, grain_max=9),
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_hedge(ctx):
    """Leaf litter and dark soil under the raised hedge decor."""
    p = RECIPES["hedge"].palette
    rgb, _ = ground(ctx, 8, 3, (p["gap"], p["dark"], p["mid"]), warp=8, grit=0.14)
    leaves, _, _ = soft_blobs(ctx, 14, 2.5, 4.5)
    rgb = mix(rgb, p["light"], leaves, 0.25)
    return rgb, None


@recipe(name="thicket", group="obstacles", label="Thicket", profile="brush",
        seam={"height": 4, "cast_q8": 88, "cast_width_q8": 832},
        palette={"dark": (40, 54, 26), "mid": (62, 80, 38), "light": (92, 110, 58),
                 "twig": (96, 78, 52), "twig_dark": (52, 42, 30)},
        style=Style(luma=65, std=11, grain_max=10, match=0.7),
        note="ground under the raised decor sprites (tools/artwork/terrain_decor.py)")
def render_thicket(ctx):
    """Shaded undergrowth with a few faint fallen twigs under the thicket decor."""
    p = RECIPES["thicket"].palette
    rgb, _ = ground(ctx, 7, 3, (p["dark"], p["mid"], p["light"]), warp=8, grit=0.12)
    rng = ctx.rng
    twigs = [0.0] * (N * N)
    for _ in range(rng.randint(3, 5)):
        length = 14 + rng.random() * 12
        x, y = ctx.interior_point(STAMP_MARGIN + length / 2)
        angle = rng.random() * TAU
        stamp_stroke(twigs, x - math.cos(angle) * length / 2, y - math.sin(angle) * length / 2,
                     angle, length, 2.5)
    rgb = mix(rgb, p["twig_dark"], twigs, 0.35)
    return rgb, None


@recipe(name="lava", group="lava", label="Lava", profile="fractured",
        seam={"height": 4, "fringe": [214, 110, 40], "fringe_q8": 128, "fringe_width_q8": 640},
        palette={"crust_dark": (36, 18, 16), "crust": (50, 24, 20), "crust_warm": (96, 42, 26),
                 "glow": (170, 70, 24), "glow_bright": (220, 150, 60)},
        style=Style(luma=58, std=17, grain_max=13, match=0.8), phases=4, animation_ticks=8,
        preview=(168, 72, 30), minimap=(176, 68, 28),
        note="placeholder until the image-generated lava lands")
def render_lava(ctx):
    p = RECIPES["lava"].palette
    rng = ctx.rng
    f1, f2, ids = worley(rng, 2)
    wx, wy = fbm(rng, 4, 2), fbm(rng, 4, 2)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, 10)
    crust_noise = normalize(fbm(rng, 6, 3))
    rgb = ramp(crust_noise, [(0.0, p["crust_dark"]), (1.0, p["crust"])])
    rgb = shade(rgb, [1 - v for v in f1], 0.2)
    rgb = grain(rgb, rng, 0.10)
    channel = band(edge, 0.03, 0.045)
    warm = band(edge, 0.08, 0.10)
    rgb = mix(rgb, p["crust_warm"], warm, 0.35)
    offsets = [rng.random() for _ in range(4)]
    pulse = [0.5 + 0.5 * math.sin(TAU * (ctx.t + offsets[i])) for i in ids]
    glow = [
        [a + (b - a) * q for q in pulse] for a, b in zip(p["glow"], p["glow_bright"])
    ]
    rgb = mix(rgb, glow, channel, 0.9)
    return rgb, None


@recipe(name="ember_field", group="lava", label="Ember field", profile="fractured",
        seam={"height": 4, "fringe": [160, 80, 40], "fringe_q8": 96, "fringe_width_q8": 512},
        palette={"dark": (50, 28, 24), "mid": (60, 34, 28), "light": (80, 44, 32),
                 "ember_dim": (120, 50, 30), "ember": (230, 120, 40), "ember_core": (250, 200, 80)},
        style=Style(luma=48, std=10, grain_max=9, match=0.7), phases=4, animation_ticks=8,
        preview=(110, 52, 36), minimap=(120, 48, 30),
        note="placeholder until the image-generated ember field lands")
def render_ember_field(ctx):
    p = RECIPES["ember_field"].palette
    rng = ctx.rng
    rgb, _ = ground(ctx, 5, 4, (p["dark"], p["mid"], p["light"]), warp=8)
    # Cracks carrying a dim steady glow, sharing lava's DNA.
    f1, f2, _ = worley(rng, 4)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], fbm(rng, 4, 2), fbm(rng, 4, 2), 6)
    cracks = band(edge, 0.03, 0.04)
    rgb = mix(rgb, p["ember_dim"], cracks, 0.35)
    # Eight to twelve embers with a three-pixel soft halo, inside the tile.
    _, _, embers = soft_blobs(ctx, rng.randint(8, 12), 3.5, 5.0)
    glow = [0.0] * (N * N)
    core = [0.0] * (N * N)
    for cx, cy, r, offset in embers:
        intensity = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(TAU * (ctx.t + offset)))
        stamp_disc(glow, cx, cy, r * 2.6, intensity * 0.7, 0.9)
        stamp_disc(core, cx, cy, r, intensity, 0.6)
    rgb = mix(rgb, p["ember_dim"], glow, 1.0)
    ember_color = [
        [a + (b - a) * c for c in core] for a, b in zip(p["ember"], p["ember_core"])
    ]
    rgb = mix(rgb, ember_color, core, 1.0)
    return rgb, None


assert set(SYNTH_ORDER) == set(RECIPES), "every built-in needs a recipe and vice versa"

# Pair treatments (B5): boundary families chosen for specific contacts.
PAIR_TREATMENTS = (
    [{"a": name, "b": "grass", "profile": "rock"} for name in ("boulders", "ridge_rock", "outcrop")]
    + [{"a": name, "b": "grass", "profile": "brush"} for name in ("hedge", "thicket")]
    + [
        {"a": "water", "b": "deep_water", "profile": "soft"},
        {"a": "deep_water", "b": "dark_water", "profile": "soft"},
    ]
    + [
        {"a": name, "b": ground_name, "profile": "fractured"}
        for name in ("lava", "ember_field")
        for ground_name in ("grass", "sand", "dirt", "gravel")
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
    """Render the sixteen variants of one phase, before perimeter sharing.

    Returns (native 32x32 tiles, 128x128 HD tiles) from the same renders, so
    the HD frames are the exact source the classic tiles were downsampled from.
    Frame 0 is the master the runtime blends every other variant's border
    toward, so the render with the quietest perimeter takes that slot (the
    sixteen seeds are unchanged; only their order is). The pooled 32-pixel
    statistics of all variants define the material's style transform, which
    is applied to every variant at both resolutions.
    """
    rec = RECIPES[name]
    renders = [render_fields(name, variant, phase) for variant in range(VARIANTS)]
    if rec.periodic:
        order = list(range(VARIANTS))
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
    jobs = jobs or min(32, os.cpu_count() or 1)
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
    """Flatten {phase: tiles} into frame order `variant + 16 * phase`."""
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
        "variants": [{"frame": i, "weight": 1} for i in range(VARIANTS)],
    }
    if rec.phases > 1:
        block["animation_frames"] = rec.phases
        block["animation_stride"] = VARIANTS
        block["animation_ticks"] = rec.animation_ticks
    if rec.periodic:
        block["edges"] = "periodic"
    if rec.grid:
        block["variant_grid"] = rec.grid
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
        "tile_size": TILE,
        "variants": VARIANTS,
        "phases": rec.phases,
        "frame_layout": "variant + 16 * phase",
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
    path = root / "datasrc/gfx" / name / "provenance.json"
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
    provenance_path = root / "datasrc/gfx" / name / "provenance.json"
    if not provenance_path.exists():
        return [f"{name}: missing {provenance_path.relative_to(root)}"]
    provenance = json.loads(provenance_path.read_text())
    if provenance.get("method") != "procedural":
        return [f"{name}: provenance method is {provenance.get('method')!r}; mark the recipe placeholder_only"]
    recorded = provenance.get("runtime_sha256", {})
    expected_names = [f"{FRAME_PREFIX}{name}{i}.png" for i in range(VARIANTS * rec.phases)]
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
    """Every material: name and stats, 16 variants at 1x and 4x, a 2x2 random join.

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
    block_h = 20 + 16 + TILE + 8 + max(2 * TILE * scale, 3 * TILE + 8 + 6 * TILE) + 8
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
        y += TILE + 8
        block.alpha_composite(tiles_to_sheet(tiles, columns=8, scale=scale), (4, y))
        rng = random.Random(fnv1a32(name + ":join"))
        picks = [tiles[rng.randrange(len(tiles))] for _ in range(4)]
        block.alpha_composite(tiles_to_sheet(picks, columns=2, scale=scale), (4 + grid_w + 16, y))
        # A 3x3 field of random variants at 1x and 2x: the grid rhythm check.
        field = [tiles[rng.randrange(len(tiles))] for _ in range(9)]
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
    parser.add_argument("--jobs", type=int, default=0, help="parallel render processes (default: CPUs)")
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
        print(f"Wrote {len(names)} materials under {root / 'data/gfx'} and {root / 'datasrc/gfx'}")


if __name__ == "__main__":
    main()
