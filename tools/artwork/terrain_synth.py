#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise original procedural terrain materials for the terrain catalog.

Usage (with the pinned asset encoder interpreter, Pillow 12.2.0):

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/terrain_synth.py                    # write all frames
    "$ENC" tools/artwork/terrain_synth.py --material mud --contact-sheet artifacts/terrain/mud.png
    "$ENC" tools/artwork/terrain_synth.py --check            # re-synthesise and compare
    "$ENC" tools/artwork/terrain_synth.py --emit-catalog     # print catalog blocks
    "$ENC" tools/artwork/terrain_synth.py --hd               # 128x128 renders under artifacts/

Every material is synthesised from scratch: periodic value noise, fractal sums,
domain warps, Worley cells, scattered stamps and palette ramps, all driven by
`random.Random(seed).random()` with `seed = fnv1a32("<material>:<variant>:<phase>")`.
No pixel of an existing tile is ever read into an output; the native grass, sand,
trail, ice and water tiles are only measured for the style targets (luma mean and
spread, neighbour grain, saturation) that keep the new art low-contrast and
painterly beside them.

Each material renders sixteen independent 128x128 variants, box-downsamples them
to 32x32 and shares the perimeter across variants (tools/artwork/material_tiles.py)
so any two variants join. Animated materials render four phases per variant; the
frame layout is `variant + 16 * phase`. Outputs are `data/gfx/terrain-<name>N.png`
and `datasrc/gfx/<name>/provenance.json`.

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
from material_tiles import (  # noqa: E402
    ROOT,
    SHEET,
    STYLE_REFERENCES,
    TILE,
    color_distance,
    fnv1a32,
    load_frames,
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

PILLOW_VERSION = "12.2.0"
N = SHEET  # render size; four render pixels per native pixel
VARIANTS = 16
FRAME_PREFIX = "data/gfx/terrain-"
XS = [i % N for i in range(N * N)]
YS = [i // N for i in range(N * N)]
TAU = 2 * math.pi
# Weight of a variant's own render against variant 0's: 0 in the outer two
# native pixels, 1 from five native pixels inward. Every variant therefore
# carries variant 0's border band, so any two variants (and the torus seam)
# join without cutting stamped features, and the runtime's four-pixel blend
# toward variant 0 (tools/terrain_tileset.py seamless_sources) is a no-op.
BORDER_WEIGHTS = {}
BAND = 16  # render pixels from an edge inside which stamped features are shared


def border_weight(inner=6.0, outer=16.0):
    key = (inner, outer)
    if key not in BORDER_WEIGHTS:
        BORDER_WEIGHTS[key] = [
            smoothstep(inner, outer, min(x + 0.5, y + 0.5, N - x - 0.5, N - y - 0.5))
            for x, y in zip(XS, YS)
        ]
    return BORDER_WEIGHTS[key]


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


def domain_warp(values, warp_x, warp_y, amount):
    """Re-sample `values` displaced by two noise fields (±`amount` pixels)."""
    return [
        sample(values, x + (wx - 0.5) * 2 * amount, y + (wy - 0.5) * 2 * amount)
        for x, y, wx, wy in zip(XS, YS, warp_x, warp_y)
    ]


def _worley_from_points(points, metric):
    """Nearest and second-nearest wrapped distance (in pixels) and nearest id."""
    f1 = [0.0] * (N * N)
    f2 = [0.0] * (N * N)
    ids = [0] * (N * N)
    half = N / 2
    chebyshev = metric == "chebyshev"
    for i in range(N * N):
        x, y = XS[i], YS[i]
        best, second, best_id = 1e9, 1e9, 0
        for pid, (px, py) in enumerate(points):
            dx = abs(px - x)
            if dx > half:
                dx = N - dx
            dy = abs(py - y)
            if dy > half:
                dy = N - dy
            d = max(dx, dy) if chebyshev else math.sqrt(dx * dx + dy * dy)
            if d < best:
                second, best, best_id = best, d, pid
            elif d < second:
                second = d
        f1[i], f2[i], ids[i] = best, second, best_id
    return f1, f2, ids


def worley_points(rng, count, metric="euclid"):
    """Worley cells from `count` free points; distances in units of the mean cell."""
    points = [(rng.random() * N, rng.random() * N) for _ in range(count)]
    f1, f2, ids = _worley_from_points(points, metric)
    cell = N / math.sqrt(count)
    return [v / cell for v in f1], [v / cell for v in f2], ids, points


def worley(rng, cells, metric="euclid", jitter=1.0):
    """Jittered-grid Worley cells; F1/F2 in units of the cell size.

    Each pixel examines the nine surrounding cells with wrapped indices, so the
    field is periodic and the cost stays linear in the canvas size.
    """
    cells = max(1, int(cells))
    size = N / cells
    px = [[0.0] * cells for _ in range(cells)]
    py = [[0.0] * cells for _ in range(cells)]
    for cy in range(cells):
        for cx in range(cells):
            px[cy][cx] = (cx + 0.5 + (rng.random() - 0.5) * jitter) * size
            py[cy][cx] = (cy + 0.5 + (rng.random() - 0.5) * jitter) * size
    f1 = [0.0] * (N * N)
    f2 = [0.0] * (N * N)
    ids = [0] * (N * N)
    chebyshev = metric == "chebyshev"
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
                d = max(abs(dx), abs(dy)) if chebyshev else math.sqrt(dx * dx + dy * dy)
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
        y = (int(math.floor(cy)) + oy) % N
        for ox in range(-r, r + 1):
            x = (int(math.floor(cx)) + ox) % N
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
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
        y = (int(math.floor(cy)) + oy) % N
        for ox in range(-r, r + 1):
            x = (int(math.floor(cx)) + ox) % N
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            angle = math.atan2(dy, dx)
            local = radius * (1 + sum(a * math.sin(h * angle + ph) for h, a, ph in lumps))
            d = math.hypot(dx, dy) / local
            if d < 1:
                height[y * N + x] = max(height[y * N + x], math.sqrt(1 - d * d))
    return height


def scatter_discs(rng, count, radius_min, radius_max, softness=0.5):
    """Random soft discs; returns the union mask and the list of (x, y, r)."""
    mask = [0.0] * (N * N)
    discs = []
    for _ in range(count):
        cx, cy = rng.random() * N, rng.random() * N
        radius = radius_min + rng.random() * (radius_max - radius_min)
        stamp_disc(mask, cx, cy, radius, 1.0, softness)
        discs.append((cx, cy, radius))
    return mask, discs


def scatter(ctx, count, radius_min, radius_max, softness=0.5):
    """Random soft discs that respect the shared border band.

    Discs that could touch the band come from the shared stream at reduced
    density, so every variant shows the same few border features instead of
    cut or ghosted ones; the rest come from the variant's own stream and stay
    clear of the band. Returns the union mask and (x, y, r, u) tuples, where
    `u` is a uniform value from the disc's stream for per-disc choices.
    """
    margin = BAND + radius_max
    inside = max(0.0, (N - 2 * margin) / N) ** 2
    mask = [0.0] * (N * N)
    discs = []
    shared = ctx.shared_rng
    for _ in range(int(round(count * (1 - inside) * 0.15))):
        cx, cy = ctx.band_point(margin)
        radius = radius_min + shared.random() * (radius_max - radius_min)
        discs.append((cx, cy, radius, shared.random()))
    for _ in range(int(round(count * inside))):
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


def tint(rgb, factor_r, factor_g, factor_b):
    return [[c * f for c in channel] for channel, f in zip(rgb, (factor_r, factor_g, factor_b))]


def scale_rgb(rgb, factors):
    return [[c * f for c, f in zip(channel, factors)] for channel in rgb]


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
    """Weak bevel: brighten slopes facing `light`, darken the others."""
    g = gradient_light(height, light)
    return [[c * (1 - strength * v) for c, v in zip(channel, g)] for channel in rgb]


def alpha_from(values, low, high):
    return [low + (high - low) * clamp01(v) for v in values]


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
    same transform to every variant of a material so their shared border band
    stays identical. Statistics only: nothing is read from existing art.
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


def blend_border(own, reference, alpha_own, alpha_ref, band=(6.0, 16.0)):
    """Replace a variant's border band with variant 0's render (see border_weight)."""
    w = border_weight(*band)
    rgb = [[r + (o - r) * t for o, r, t in zip(oc, rc, w)] for oc, rc in zip(own, reference)]
    if alpha_own is None and alpha_ref is None:
        return rgb, None
    alpha_own = alpha_own or [255.0] * (N * N)
    alpha_ref = alpha_ref or [255.0] * (N * N)
    return rgb, [r + (o - r) * t for o, r, t in zip(alpha_own, alpha_ref, w)]


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
    band: tuple = (6.0, 16.0)
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
        # Features that may cross the shared border band are drawn from this
        # stream, which is the same for every variant (see BORDER_WEIGHT).
        self.shared_rng = random.Random(fnv1a32(f"{name}:0:0"))
        self.t = phase / phases

    def interior_point(self, margin):
        """A position at least `margin` render pixels from every edge."""
        return margin + self.rng.random() * (N - 2 * margin), margin + self.rng.random() * (N - 2 * margin)

    def band_point(self, margin):
        """A shared position whose centre lies within `margin` of an edge."""
        rng = self.shared_rng
        along = rng.random() * N
        depth = rng.random() * margin
        side = rng.randrange(4)
        if side == 0:
            return along, depth
        if side == 1:
            return along, N - depth
        if side == 2:
            return depth, along
        return N - depth, along


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


def cell_jitter(rng, count, spread, hue=0.25):
    """Per-cell brightness jitter with a small, hue-neutral colour drift."""
    jitters = []
    for _ in range(count):
        value = 1 + (rng.random() - 0.5) * 2 * spread
        warm = (rng.random() - 0.5) * 2 * spread * hue
        jitters.append((value + warm, value, value - warm))
    return jitters


def grain(rgb, rng, amount=0.12, cells=48):
    """Fine per-pixel grain, as the native tiles have (about one native pixel)."""
    fine = lattice_noise(rng, cells)
    return [[c * (1 + amount * (v - 0.5) * 2) for c, v in zip(channel, fine)] for channel in rgb]


def apply_cell_jitter(rgb, ids, jitters):
    return [
        [c * jitters[i][k] for c, i in zip(channel, ids)] for k, channel in enumerate(rgb)
    ]


# Barren ground: muted warm neutrals, inhibits growth like sand but is not a shore.

@recipe(name="dirt", group="barren", label="Dirt", profile="soft",
        seam={"height": 2, "cast_q8": 40, "cast_width_q8": 448},
        palette={"dark": (104, 82, 56), "mid": (136, 108, 74), "light": (160, 132, 96), "pebble": (86, 68, 48)},
        style=Style(luma=116, std=9, grain_max=8))
def render_dirt(ctx):
    p = RECIPES["dirt"].palette
    rgb, _ = ground(ctx, 5, 4, (p["dark"], p["mid"], p["light"]), warp=10)
    pebbles, _ = scatter(ctx, 70, 1.6, 2.6, softness=0.6)
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
        palette={"dark": (108, 102, 94), "mid": (138, 130, 118), "light": (162, 154, 140)},
        style=Style(luma=128, std=12, grain_max=11))
def render_gravel(ctx):
    p = RECIPES["gravel"].palette
    rng = ctx.rng
    f1, f2, ids = worley(rng, 6)
    grit = fbm(rng, 16, 2)
    tone = normalize([0.5 * (1 - a) + 0.5 * g for a, g in zip(f1, grit)])
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(rng, 36, 0.08))
    rgb = shade(rgb, [1 - v for v in f1], 0.22)
    gaps = band([b - a for a, b in zip(f1, f2)], 0.08, 0.10)
    rgb = mix(rgb, p["dark"], gaps, 0.6)
    rgb = grain(rgb, rng, 0.08)
    return rgb, None


@recipe(name="flower_meadow", group="barren", label="Flower meadow", profile="brush",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (44, 110, 40), "mid": (60, 126, 48), "light": (74, 138, 58),
                 "flowers": [(232, 190, 210), (240, 224, 150), (200, 160, 220)]},
        style=Style(luma=86, std=9, grain_max=9, match=0.7),
        note="placeholder until the image-generated flower meadow lands")
def render_flower_meadow(ctx):
    p = RECIPES["flower_meadow"].palette
    rng = ctx.rng
    rgb, _ = ground(ctx, 8, 3, (p["dark"], p["mid"], p["light"]))
    _, blooms = scatter(ctx, 30, 3.0, 4.6)
    for index, color in enumerate(p["flowers"]):
        mask = [0.0] * (N * N)
        for cx, cy, radius, u in blooms:
            if int(u * len(p["flowers"])) == index:
                stamp_disc(mask, cx, cy, radius, 1.0, 0.45)
        rgb = mix(rgb, color, mask, 0.9)
    return rgb, None


# Rough ground: slow going; cool and desaturated apart from mud.

@recipe(name="mud", group="rough", label="Mud", profile="soft",
        seam={"height": 1},
        palette={"dark": (62, 48, 36), "mid": (88, 70, 52), "light": (116, 96, 74), "sheen": (140, 120, 100)},
        style=Style(luma=76, std=8, grain_max=7))
def render_mud(ctx):
    p = RECIPES["mud"].palette
    rgb, _ = ground(ctx, 4, 4, (p["dark"], p["mid"], p["light"]), warp=14)
    sheen, _ = scatter_discs(ctx.rng, 4, 12, 20, softness=0.9)
    rgb = mix(rgb, p["sheen"], sheen, 0.45)
    return rgb, None


@recipe(name="marsh", group="rough", label="Marsh", profile="soft",
        seam={"height": 1},
        palette={"dark": (50, 66, 54), "mid": (74, 96, 78), "light": (104, 122, 96), "pool": (40, 70, 90)},
        style=Style(luma=88, std=10, grain_max=9), alpha_range=(205, 255))
def render_marsh(ctx):
    p = RECIPES["marsh"].palette
    rng = ctx.rng
    reeds = normalize(fbm(rng, 12, 3, cells_y=3))
    rgb = ramp(reeds, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = grain(rgb, rng, 0.10)
    low = fbm(rng, 5, 2)
    pools = band(low, 0.27, 0.06)
    rgb = mix(rgb, p["pool"], pools, 0.9)
    alpha = [255 - 40 * m for m in pools]
    return rgb, alpha


@recipe(name="deep_snow", group="rough", label="Deep snow", profile="soft",
        seam={"height": 2, "cast_q8": 32, "cast_width_q8": 448},
        palette={"dark": (196, 206, 218), "mid": (222, 228, 236), "light": (240, 244, 248), "sparkle": (252, 252, 255)},
        style=Style(luma=226, std=5, grain_max=5))
def render_deep_snow(ctx):
    p = RECIPES["deep_snow"].palette
    rgb, _ = ground(ctx, 3, 2, (p["dark"], p["mid"], p["light"]))
    sparkle, _ = scatter(ctx, 40, 0.9, 1.4, softness=0.3)
    rgb = mix(rgb, p["sparkle"], sparkle, 1.0)
    return rgb, None


@recipe(name="scree", group="rough", label="Scree", profile="rock",
        seam={"height": 1},
        palette={"dark": (92, 96, 104), "mid": (118, 122, 128), "light": (148, 152, 156)},
        style=Style(luma=120, std=13, grain_max=12))
def render_scree(ctx):
    p = RECIPES["scree"].palette
    rng = ctx.rng
    f1, f2, ids = worley(rng, 5, metric="chebyshev", jitter=0.9)
    grit = fbm(rng, 12, 2)
    tone = normalize([0.55 * (1 - a) + 0.45 * g for a, g in zip(f1, grit)])
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(rng, 25, 0.09))
    rgb = shade(rgb, [1 - v for v in f1], 0.3)
    gaps = band([b - a for a, b in zip(f1, f2)], 0.06, 0.08)
    rgb = mix(rgb, p["dark"], gaps, 0.55)
    rgb = grain(rgb, rng, 0.08)
    return rgb, None


# Paths: warm and light, faster movement.

@recipe(name="dirt_track", group="paths", label="Dirt track", profile="sand",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (142, 116, 82), "mid": (170, 142, 104), "light": (192, 166, 126)},
        style=Style(luma=146, std=7, grain_max=6))
def render_dirt_track(ctx):
    p = RECIPES["dirt_track"].palette
    rgb, _ = ground(ctx, 4, 3, (p["dark"], p["mid"], p["light"]), warp=8)
    worn, _ = scatter_discs(ctx.rng, 4, 14, 24, softness=0.9)
    rgb = mix(rgb, p["light"], worn, 0.5)
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


# Fertile ground: dark saturated green, the new strategic resource.

@recipe(name="loam", group="fertile", label="Loam", profile="soft",
        seam={"height": 2, "cast_q8": 56, "cast_width_q8": 512},
        palette={"dark": (26, 32, 20), "mid": (44, 52, 30), "light": (64, 72, 42), "fleck": (90, 128, 56)},
        style=Style(luma=48, std=8, grain_max=8))
def render_loam(ctx):
    p = RECIPES["loam"].palette
    rgb, _ = ground(ctx, 5, 4, (p["dark"], p["mid"], p["light"]), warp=8)
    flecks, _ = scatter(ctx, 50, 2.2, 3.4, softness=0.5)
    rgb = mix(rgb, p["fleck"], flecks, 0.85)
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
        palette={"dark": (64, 118, 42), "mid": (92, 150, 58), "light": (130, 178, 84), "dot": (236, 226, 160)},
        style=Style(luma=126, std=7, grain_max=7))
def render_spring_meadow(ctx):
    p = RECIPES["spring_meadow"].palette
    rgb, _ = ground(ctx, 8, 3, (p["dark"], p["mid"], p["light"]))
    dots, _ = scatter(ctx, 16, 2.4, 3.4, softness=0.5)
    rgb = mix(rgb, p["dot"], dots, 0.85)
    return rgb, None


# Deep water: translucent tints that darken the shared scrolling ocean.

@recipe(name="deep_water", group="deep_water", label="Deep water", profile="soft",
        seam={"height": 6, "cast_q8": 72, "cast_width_q8": 640},
        palette={"tint": (10, 60, 160), "swell": (20, 76, 176), "caustic": (60, 120, 200), "alpha": (170, 200)},
        style=Style(luma=52, std=6, grain_max=6, match=0.5), alpha_range=(160, 210),
        preview=(26, 48, 140), minimap=(0, 28, 96))
def render_deep_water(ctx):
    p = RECIPES["deep_water"].palette
    rng = ctx.rng
    swell = fbm(rng, 4, 2)
    rgb = mix(flat(p["tint"]), p["swell"], swell, 0.6)
    alpha = alpha_from(normalize(fbm(rng, 4, 3)), *p["alpha"])
    f1, f2, _ = worley(rng, 2)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], fbm(rng, 4, 2), fbm(rng, 4, 2), 10)
    caustics = band(edge, 0.03, 0.05)
    rgb = mix(rgb, p["caustic"], caustics, 0.2)
    return rgb, alpha


@recipe(name="dark_water", group="deep_water", label="Dark water", profile="soft",
        seam={"height": 7, "cast_q8": 80, "cast_width_q8": 704},
        palette={"tint": (14, 38, 78), "murk": (20, 48, 92), "speck": (6, 14, 30), "alpha": (225, 240)},
        style=Style(luma=34, std=4, grain_max=5, match=0.5), alpha_range=(215, 245),
        preview=(16, 34, 84), minimap=(0, 20, 64))
def render_dark_water(ctx):
    p = RECIPES["dark_water"].palette
    rng = ctx.rng
    murk = fbm(rng, 4, 3)
    rgb = mix(flat(p["tint"]), p["murk"], murk, 0.5)
    alpha = alpha_from(normalize(fbm(rng, 3, 2)), *p["alpha"])
    specks, _ = scatter(ctx, 50, 0.9, 1.6, softness=0.4)
    rgb = mix(rgb, p["speck"], specks, 0.8)
    return rgb, alpha


# Ridges: impassable to walkers and swimmers, but projectiles cross.

@recipe(name="ridge_rock", group="ridges", label="Ridge", profile="rock",
        seam={"height": 4, "cast_q8": 72, "cast_width_q8": 768},
        palette={"dark": (68, 74, 84), "mid": (98, 104, 112), "light": (128, 134, 140), "crack": (52, 56, 64)},
        style=Style(luma=102, std=12, grain_max=10))
def render_ridge_rock(ctx):
    p = RECIPES["ridge_rock"].palette
    rng = ctx.rng
    rgb, f = ground(ctx, 4, 4, (p["dark"], p["mid"], p["light"]), warp=14, grit=0.10)
    f1, f2, _ = worley(rng, 3)
    edge = domain_warp([b - a for a, b in zip(f1, f2)], fbm(rng, 4, 2), fbm(rng, 4, 2), 8)
    cracks = band(edge, 0.05, 0.07)
    height = [h * 0.7 + (1 - c) * 0.3 for h, c in zip(f, cracks)]
    rgb = shade(rgb, height, 0.5, light=(-1.0, -0.6))
    rgb = mix(rgb, p["crack"], cracks, 0.5)
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
    lichen, _ = scatter(ctx, 14, 2.6, 4.2, softness=0.6)
    lichen = [l * (1 - g) for l, g in zip(lichen, gaps)]
    rgb = mix(rgb, p["lichen"], lichen, 0.75)
    return rgb, None


# Void: nothing crosses, nothing flies; near black with a rim from seam fringes.

@recipe(name="void_hole", group="void", label="Hole", profile="crisp",
        seam={"height": 0, "fringe": [54, 50, 66], "fringe_q8": 80, "fringe_width_q8": 384},
        palette={"dark": (6, 6, 10), "mid": (10, 10, 14), "light": (14, 14, 20), "rim": (54, 50, 66)},
        style=Style(luma=10, std=2, grain_max=3),
        preview=(28, 24, 36), minimap=(16, 14, 22))
def render_void_hole(ctx):
    p = RECIPES["void_hole"].palette
    rgb, _ = ground(ctx, 3, 2, (p["dark"], p["mid"], p["light"]))
    return rgb, None


@recipe(name="chasm", group="void", label="Chasm", profile="rock",
        seam={"height": 0, "fringe": [44, 34, 40], "fringe_q8": 64, "fringe_width_q8": 384},
        palette={"dark": (14, 10, 16), "mid": (22, 16, 24), "light": (44, 34, 40)},
        style=Style(luma=22, std=5, grain_max=5),
        preview=(40, 30, 38), minimap=(30, 22, 28))
def render_chasm(ctx):
    p = RECIPES["chasm"].palette
    streaks = normalize(fbm(ctx.rng, 10, 3, cells_y=3))
    rgb = ramp(streaks, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    return rgb, None


# Obstacles: dark, shadowed silhouettes; placeholders for image-generated art.

@recipe(name="boulders", group="obstacles", label="Boulders", profile="rock",
        seam={"height": 4, "cast_q8": 80, "cast_width_q8": 768},
        palette={"ground_dark": (46, 48, 50), "ground": (58, 60, 62), "ground_light": (70, 72, 72),
                 "rock_dark": (78, 80, 82), "rock_light": (112, 114, 112), "shadow": (26, 28, 30)},
        style=Style(luma=74, std=14, grain_max=12, match=0.5), band=(8.0, 20.0),
        note="placeholder until the image-generated boulders land")
def render_boulders(ctx):
    p = RECIPES["boulders"].palette
    rng = ctx.rng
    f1, _, _ = worley(rng, 10)
    grit = fbm(rng, 12, 2)
    tone = normalize([0.5 * (1 - a) + 0.5 * g for a, g in zip(f1, grit)])
    rgb = ramp(tone, [(0.0, p["ground_dark"]), (0.5, p["ground"]), (1.0, p["ground_light"])])
    rgb = shade(rgb, [1 - v for v in f1], 0.15)
    # Two stones cross the shared border band and are identical in every
    # variant; the others vary and stay clear of the band.
    stones = []
    shared = ctx.shared_rng
    for _ in range(2):
        cx, cy = ctx.band_point(14)
        r = 7 + shared.random() * 4
        lumps = [(2, 0.08 + shared.random() * 0.08, shared.random() * TAU),
                 (3, 0.05 + shared.random() * 0.07, shared.random() * TAU),
                 (5, 0.03 + shared.random() * 0.03, shared.random() * TAU)]
        stones.append((cx, cy, r, lumps))
    for _ in range(rng.randint(4, 6)):
        for _attempt in range(20):
            r = 8 + rng.random() * 7
            cx, cy = ctx.interior_point(20 + r)
            if all(math.hypot(cx - ox, cy - oy) > (r + orr) * 0.85 for ox, oy, orr, _ in stones):
                break
        lumps = [(2, 0.08 + rng.random() * 0.08, rng.random() * TAU),
                 (3, 0.05 + rng.random() * 0.07, rng.random() * TAU),
                 (5, 0.03 + rng.random() * 0.03, rng.random() * TAU)]
        stones.append((cx, cy, r, lumps))
    height = [0.0] * (N * N)
    for cx, cy, r, lumps in stones:
        dome = disc_profile(cx, cy, r, lumps)
        height = [max(a, b) for a, b in zip(height, dome)]
    coverage = [smoothstep(0.0, 0.12, h) for h in height]
    # Contact shadow: the stone outline shifted down-right, outside the stone.
    shadow = [0.0] * (N * N)
    for i in range(N * N):
        x, y = XS[i], YS[i]
        shadow[i] = coverage[((y - 5) % N) * N + (x - 4) % N] * (1 - coverage[i])
    rgb = mix(rgb, p["shadow"], shadow, 0.7)
    rock_tone = normalize([h * 0.6 + g * 0.4 for h, g in zip(height, grit)])
    rock = ramp(rock_tone, [(0.0, p["rock_dark"]), (1.0, p["rock_light"])])
    rock = shade(rock, height, 0.55, light=(-0.8, -0.8))
    rock = grain(rock, rng, 0.10)
    rgb = mix(rgb, rock, coverage, 1.0)
    return rgb, None


@recipe(name="hedge", group="obstacles", label="Hedge", profile="brush",
        seam={"height": 4, "cast_q8": 80, "cast_width_q8": 768},
        palette={"dark": (22, 50, 24), "mid": (34, 72, 34), "light": (56, 98, 46), "gap": (14, 34, 18)},
        style=Style(luma=56, std=9, grain_max=9),
        note="placeholder until the image-generated hedge lands")
def render_hedge(ctx):
    p = RECIPES["hedge"].palette
    rng = ctx.rng
    f1, f2, _ = worley(rng, 6)
    pillows = normalize([1 - v for v in f1])
    leaves = fbm(rng, 12, 3)
    h = normalize([0.55 * a + 0.45 * b for a, b in zip(pillows, leaves)])
    rgb = ramp(h, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = shade(rgb, h, 0.3)
    gaps = band([b - a for a, b in zip(f1, f2)], 0.05, 0.08)
    rgb = mix(rgb, p["gap"], gaps, 0.6)
    rgb = grain(rgb, rng, 0.10)
    return rgb, None


@recipe(name="thicket", group="obstacles", label="Thicket", profile="brush",
        seam={"height": 4, "cast_q8": 80, "cast_width_q8": 768},
        palette={"dark": (40, 54, 26), "mid": (62, 80, 38), "light": (92, 110, 58),
                 "twig": (96, 78, 52), "twig_dark": (52, 42, 30)},
        style=Style(luma=65, std=11, grain_max=10, match=0.7), band=(8.0, 20.0),
        note="placeholder until the image-generated thicket lands")
def render_thicket(ctx):
    p = RECIPES["thicket"].palette
    rng = ctx.rng
    rgb, _ = ground(ctx, 5, 3, (p["dark"], p["mid"], p["light"]), warp=10)
    shared = ctx.shared_rng
    for color, count, width in ((p["twig_dark"], 12, 5.5), (p["twig"], 9, 4.5)):
        twigs = [0.0] * (N * N)
        # Four twigs per colour cross the shared band identically in every variant.
        for _ in range(4):
            x, y = ctx.band_point(16)
            stamp_stroke(twigs, x, y, shared.random() * TAU, 18 + shared.random() * 26,
                         width + shared.random() * 1.5)
        for _ in range(count):
            length = 16 + rng.random() * 22
            x, y = ctx.interior_point(22 + length / 2)
            angle = rng.random() * TAU
            stamp_stroke(twigs, x - math.cos(angle) * length / 2, y - math.sin(angle) * length / 2,
                         angle, length, width + rng.random() * 1.5)
        rgb = mix(rgb, color, twigs, 0.85)
    return rgb, None


# Lava: warm, saturated, animated glow in channels between dark plates.

@recipe(name="lava", group="lava", label="Lava", profile="fractured",
        seam={"height": 4, "fringe": [214, 110, 40], "fringe_q8": 96, "fringe_width_q8": 512},
        palette={"crust_dark": (56, 24, 18), "crust": (70, 30, 22), "crust_warm": (120, 50, 26),
                 "glow": (210, 96, 30), "glow_bright": (250, 190, 60)},
        style=Style(luma=70, std=22, grain_max=16, match=0.8), phases=4, animation_ticks=8,
        preview=(196, 84, 34), minimap=(200, 80, 30),
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
    channel = band(edge, 0.04, 0.06)
    warm = band(edge, 0.10, 0.12)
    rgb = mix(rgb, p["crust_warm"], warm, 0.35)
    offsets = [rng.random() for _ in range(4)]
    pulse = [0.5 + 0.5 * math.sin(TAU * (ctx.t + offsets[i])) for i in ids]
    glow = [
        [a + (b - a) * q for q in pulse] for a, b in zip(p["glow"], p["glow_bright"])
    ]
    rgb = mix(rgb, glow, channel, 0.9)
    return rgb, None


@recipe(name="ember_field", group="lava", label="Ember field", profile="fractured",
        seam={"height": 4, "fringe": [160, 80, 40], "fringe_q8": 64, "fringe_width_q8": 384},
        palette={"dark": (50, 28, 24), "mid": (60, 34, 28), "light": (80, 44, 32),
                 "ember_dim": (120, 50, 30), "ember": (230, 120, 40), "ember_core": (250, 200, 80)},
        style=Style(luma=48, std=10, grain_max=9, match=0.7), phases=4, animation_ticks=8,
        preview=(110, 52, 36), minimap=(120, 48, 30),
        note="placeholder until the image-generated ember field lands")
def render_ember_field(ctx):
    p = RECIPES["ember_field"].palette
    rng = ctx.rng
    rgb, _ = ground(ctx, 5, 4, (p["dark"], p["mid"], p["light"]), warp=8)
    f1, f2, _ = worley(rng, 5)
    cracks = band([b - a for a, b in zip(f1, f2)], 0.04, 0.05)
    rgb = mix(rgb, p["dark"], cracks, 0.5)
    _, embers = scatter(ctx, 40, 2.4, 4.0)
    glow = [0.0] * (N * N)
    core = [0.0] * (N * N)
    for cx, cy, r, offset in embers:
        intensity = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(TAU * (ctx.t + offset)))
        stamp_disc(glow, cx, cy, r * 1.8, intensity * 0.6, 0.9)
        stamp_disc(core, cx, cy, r, intensity, 0.5)
    rgb = mix(rgb, p["ember_dim"], glow, 1.0)
    ember_color = [
        [a + (b - a) * c for c in core] for a, b in zip(p["ember"], p["ember_core"])
    ]
    rgb = mix(rgb, ember_color, core, 1.0)
    return rgb, None


# Catalogue order of the built-in types, as the engine enum lists them.
BUILTIN_ORDER = [
    "boulders", "hedge", "thicket", "ridge_rock", "outcrop", "dirt", "clay", "gravel",
    "flower_meadow", "mud", "marsh", "deep_snow", "scree", "dirt_track", "boardwalk", "lava",
    "ember_field", "loam", "moss", "spring_meadow", "deep_water", "dark_water", "void_hole", "chasm",
]
LEGACY_NAMES = ["water", "sand", "grass", "ice", "road"]
assert set(BUILTIN_ORDER) == set(RECIPES), "every built-in needs a recipe and vice versa"

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
        for other in LEGACY_NAMES + [n for n in RECIPES if n not in ("void_hole", "chasm")]
    ]
)


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------


def render_fields(name, variant, phase):
    rec = RECIPES[name]
    ctx = Ctx(name, variant, phase, rec.phases)
    return rec.render(ctx)


def render_phase(name, phase, hd=False):
    """Render the sixteen variants of one phase, before perimeter sharing.

    Variant 0 is rendered first; its border band replaces every other variant's
    and its 32-pixel statistics define the material's style transform.
    """
    rec = RECIPES[name]
    ref_rgb, ref_alpha = render_fields(name, 0, phase)
    small_rgb, _ = box_down_fields(ref_rgb, ref_alpha)
    transform = style_transform(small_rgb, rec.style.luma, rec.style.std, rec.style.match)
    tiles = []
    for variant in range(VARIANTS):
        if variant == 0:
            rgb, alpha = ref_rgb, ref_alpha
        else:
            rgb, alpha = render_fields(name, variant, phase)
            rgb, alpha = blend_border(rgb, ref_rgb, alpha, ref_alpha, rec.band)
        if hd:
            tiles.append(fields_to_image(apply_style(rgb, transform), alpha, N))
            continue
        rgb, alpha = box_down_fields(rgb, alpha)
        rgb = apply_style(rgb, transform)
        tiles.append(fields_to_image(rgb, alpha if ref_alpha is not None else None, TILE))
    return tiles


def _render_job(args):
    name, phase, hd = args
    tiles = render_phase(name, phase, hd)
    return name, phase, [tile.tobytes() for tile in tiles], tiles[0].size


def synthesize(names, jobs=None, hd=False):
    """Render and perimeter-share all variants: {name: {phase: [tiles]}}."""
    tasks = [(name, phase, hd) for name in names for phase in range(RECIPES[name].phases)]
    jobs = jobs or min(32, os.cpu_count() or 1)
    if jobs > 1 and len(tasks) > 1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(tasks))) as pool:
            rendered = list(pool.map(_render_job, tasks))
    else:
        rendered = [_render_job(task) for task in tasks]
    results = {}
    width = 2 * (N // TILE) if hd else 2
    for name, phase, data, size in rendered:
        tiles = [Image.frombytes("RGBA", size, d) for d in data]
        results.setdefault(name, {})[phase] = share_perimeter(tiles, width)
    return results


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
        "ocean": False,
        "preview": list(preview),
        "variants": [{"frame": i, "weight": 1} for i in range(VARIANTS)],
    }
    if rec.phases > 1:
        block["animation_frames"] = rec.phases
        block["animation_stride"] = VARIANTS
        block["animation_ticks"] = rec.animation_ticks
    block["minimap"] = list(minimap)
    block["seam"] = dict(rec.seam)
    return block


def catalog_fragment(results):
    names = [n for n in BUILTIN_ORDER if n in results]
    return {
        "materials": [material_block(name, results[name]) for name in names],
        "bindings": {name: name for name in names},
        "pair_treatments": [p for p in PAIR_TREATMENTS if p["a"] in results or p["b"] in results],
    }


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


def provenance_document(name, phases, hashes, root):
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
        "runtime_sha256": hashes,
    }


def write_material(name, phases, root):
    frames = frames_of(name, phases)
    hashes = write_frames(frames, f"{FRAME_PREFIX}{name}", 0, root)
    document = provenance_document(name, phases, hashes, root)
    path = root / "datasrc/gfx" / name / "provenance.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n")
    return hashes


def check_material(name, phases, root):
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
        return [f"{name}: provenance method is {provenance.get('method')!r}; run with placeholder_only"]
    recorded = provenance.get("runtime_sha256", {})
    expected_names = [f"{FRAME_PREFIX}{name}{i}.png" for i in range(VARIANTS * rec.phases)]
    if list(recorded) != expected_names:
        problems.append(f"{name}: provenance covers {len(recorded)} frames, expected {len(expected_names)}")
    for i, relative in enumerate(expected_names):
        path = root / relative
        if not path.exists():
            problems.append(f"{relative}: missing")
            continue
        with Image.open(path) as image:
            committed = pixel_sha256(image)
        if committed != fresh[i]:
            problems.append(f"{relative}: committed pixels differ from a fresh synthesis")
        if recorded.get(relative) != committed:
            problems.append(f"{relative}: provenance hash differs from committed pixels")
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
    """Every material: name and stats, 16 variants at 1x and 4x, a 2x2 random join."""
    scale = 4
    blocks = []
    legacy = {key: reference_tiles(key, root) for key in STYLE_REFERENCES}
    entries = [(name, results[name][0], RECIPES[name].group, True) for name in BUILTIN_ORDER if name in results]
    entries += [(name, tiles, "legacy", False) for name, tiles in legacy.items()]
    font = _font(13)
    small = _font(11)
    strip_w = VARIANTS * TILE
    grid_w = 8 * TILE * scale
    join_w = 2 * TILE * scale
    block_w = grid_w + 16 + join_w + 16
    block_h = 20 + 16 + TILE + 8 + 2 * TILE * scale + 8
    for name, tiles, group, generated in entries:
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
        if stats["alpha"] < 250:
            # Translucent materials are shown over a flat ocean blue so the tint reads.
            ocean = Image.new("RGBA", block.size, (24, 60, 120, 255))
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
    parser.add_argument("--hd", action="store_true", help="write 128x128 renders under artifacts/terrain/hd/")
    parser.add_argument("--write", action="store_true", help="also write frames when another mode is selected")
    parser.add_argument("--jobs", type=int, default=0, help="parallel render processes (default: CPUs)")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to read and write")
    args = parser.parse_args(argv)
    require_pillow()
    names = args.material or list(BUILTIN_ORDER)
    unknown = [n for n in names if n not in RECIPES]
    if unknown:
        parser.error(f"unknown material(s): {', '.join(unknown)}; known: {', '.join(BUILTIN_ORDER)}")
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
        results = synthesize(checked, jobs)
        problems = [p for name in checked for p in check_material(name, results[name], root)]
        skipped = [n for n in names if RECIPES[n].placeholder_only]
        for problem in problems:
            print("FAIL", problem)
        if skipped:
            print("skipped placeholder-only recipes:", ", ".join(skipped))
        if problems:
            raise SystemExit(1)
        print(f"PASS {len(checked)} procedural materials match their committed frames and provenance")
        return

    results = synthesize(names, jobs)
    modes = args.contact_sheet is not None or args.emit_catalog
    if args.contact_sheet is not None:
        args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        contact_sheet(results, root).save(args.contact_sheet)
        print(f"Wrote contact sheet {args.contact_sheet}")
    if args.emit_catalog:
        print(json.dumps(catalog_fragment(results), indent=2))
        print(presentation_initialisers(results))
    if not modes or args.write:
        for name in names:
            hashes = write_material(name, results[name], root)
            stats = style_stats(results[name][0])
            print(f"{name:<14} {len(hashes):3d} frames  luma {stats['luma']:5.1f} std {stats['std']:4.1f} "
                  f"grain {stats['grain']:4.1f} sat {stats['sat']:.2f}")
        print(f"Wrote {len(names)} materials under {root / 'data/gfx'} and {root / 'datasrc/gfx'}")


if __name__ == "__main__":
    main()
