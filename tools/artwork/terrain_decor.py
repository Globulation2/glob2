#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise the raised decor sprites drawn over obstacle terrain.

Obstacle terrain (boulders, hedge, thicket, ridge rock, outcrop) is drawn in
two layers, like forest: the ground material in the terrain compositor, then a
decor sprite per cell drawn with the resources in screen-row order, so objects
overlap their neighbours and occlude each other. This tool renders those
sprites from scratch.

Object sprites are 64x64 native (256x256 HD), centred on a 32x32 cell.
Connected foliage uses cropped 32x48 native frames (128x192 HD).
Objects stand on the lower part of their cell and rise up to sixteen native
pixels into the cell above, as trees do; drawn back to front, interior cells
overlap into one continuous mass. A frame
holds a small cluster of objects in the game's soft three-quarter view: a lit
top, a darker front face below it, top-left light and a soft contact shadow to
the lower right, drawn back to front. Frames come in two sizes: `full` for
cells inside an obstacle region and `edge` (smaller, pulled toward the cell
centre) for cells with open neighbours; the runtime picks by corner count.
Hedge and thicket additionally have connected corner-mask surfaces: their
interiors have no per-cell front face or ground shadow. Only the exposed
contour is shaded, and leaf strokes inherit soft overlapping crown lighting.

Usage (pinned encoder interpreter, as for terrain_synth.py):

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/terrain_decor.py                  # write all frames + HD
    "$ENC" tools/artwork/terrain_decor.py --sheet artifacts/terrain/decor.png
    "$ENC" tools/artwork/terrain_decor.py --check

All sets share one sprite, `data/gfx/terrain-decorN.png`, with set k at
frames 12k..12k+11 in `ORDER`, followed by connected hedge/thicket frames
(16 corner masks, a 4x4 continuous texture block each), so the runtime batches every decor draw like
resources. HD frames are registered through tools/artwork/highres_pack.py and
provenance is written to `datasrc/gfx/terrain-decor/provenance.json`.
`--write-catalog` adds the `decor` block to each material in tileset.json.
Pure Python 3 plus Pillow; no numpy.
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

import PIL
from PIL import Image, ImageDraw, ImageFilter, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parent))
from material_tiles import ROOT, fnv1a32, pixel_sha256  # noqa: E402

NATIVE = 64          # native frame size; the cell is the central 32x32
SCALE = 4
S = NATIVE * SCALE   # render size
CELL = 32 * SCALE
PAD = (S - CELL) // 2
FULL, EDGE = 8, 4    # frames per size class: full 0..7, edge 8..11
FRAMES = FULL + EDGE
PREFIX = "data/gfx/terrain-decor"
HD_PREFIX = "data/highres/v1/terrain-decor"
HD_CATEGORY = "procedural-materials"
HD_RECIPE = "procedural terrain decor v1"
LIGHT = (-0.62, -0.78)  # toward the upper left, screen space
SHADOW = (0.55, 0.42)   # contact shadow offset direction (lower right)


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def smoothstep(a, b, v):
    t = clamp((v - a) / (b - a)) if b != a else (1.0 if v >= b else 0.0)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def mix3(a, b, t):
    return tuple(lerp(x, y, t) for x, y in zip(a, b))


class Noise:
    """Small value-noise field over the render canvas (not periodic)."""

    def __init__(self, rng, cells):
        self.cells = cells
        self.grid = [[rng.random() for _ in range(cells + 2)] for _ in range(cells + 2)]

    def __call__(self, x, y):
        fx, fy = x / S * self.cells, y / S * self.cells
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
    """Straight RGBA float canvas composited back to front."""

    def __init__(self):
        self.rgb = [[0.0, 0.0, 0.0] for _ in range(S * S)]
        self.a = [0.0] * (S * S)

    def over(self, i, color, alpha):
        if alpha <= 0:
            return
        a0 = self.a[i]
        out = alpha + a0 * (1 - alpha)
        if out <= 0:
            return
        c0 = self.rgb[i]
        self.rgb[i] = [(color[k] * alpha + c0[k] * a0 * (1 - alpha)) / out for k in range(3)]
        self.a[i] = out

    def darken(self, i, amount):
        """Multiply what is already there (shadow cast onto earlier objects)."""
        if amount <= 0 or self.a[i] <= 0:
            return
        self.rgb[i] = [c * (1 - amount) for c in self.rgb[i]]

    def image(self):
        # Fade the outer few render pixels so soft shadows never end in a
        # hard line at the sprite frame.
        for i in range(S * S):
            x, y = i % S, i // S
            edge = min(x, y, S - 1 - x, S - 1 - y)
            if edge < 12:
                self.a[i] *= smoothstep(0, 12, edge)
        img = Image.new("RGBA", (S, S))
        img.putdata([
            (int(clamp(r / 255) * 255 + 0.5), int(clamp(g / 255) * 255 + 0.5),
             int(clamp(b / 255) * 255 + 0.5), int(clamp(a) * 255 + 0.5))
            for (r, g, b), a in zip(self.rgb, self.a)
        ])
        return img


# --------------------------------------------------------------------------
# Object primitives. Each returns a list of (index, height in 0..1) for its
# silhouette; shading is shared.
# --------------------------------------------------------------------------


def lumpy_outline(rng, harmonics=4, amount=0.12):
    lumps = [(h, amount * rng.uniform(0.3, 1.0) / h ** 0.6, rng.uniform(0, math.tau))
             for h in range(2, 2 + harmonics)]
    return lambda angle: 1 + sum(a * math.sin(h * angle + p) for h, a, p in lumps)


def dome(cx, cy, rx, ry, outline, power=0.6):
    """Height field of a lumpy elliptical dome; returns [(i, h, nx, ny)]."""
    out = []
    reach = 1.35
    for y in range(max(0, int(cy - ry * reach)), min(S, int(cy + ry * reach) + 1)):
        for x in range(max(0, int(cx - rx * reach)), min(S, int(cx + rx * reach) + 1)):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            d = math.hypot(dx, dy) / outline(math.atan2(dy, dx))
            if d < 1:
                h = (1 - d * d) ** power
                out.append((y * S + x, h, dx, dy, d))
    return out


def polygon_mask(points, blur=0.0):
    """Supersampled polygon coverage as {index: coverage}."""
    img = Image.new("L", (S * 2, S * 2), 0)
    ImageDraw.Draw(img).polygon([(x * 2, y * 2) for x, y in points], fill=255)
    if blur:
        img = img.filter(ImageFilter.GaussianBlur(blur * 2))
    img = img.resize((S, S), Image.Resampling.BOX)
    data = img.getdata()
    return {i: v / 255 for i, v in enumerate(data) if v}


def cast_shadow(canvas, footprint, strength, spread, offset):
    """Soft contact shadow of a footprint {i: coverage}, offset to the lower right."""
    img = Image.new("L", (S, S), 0)
    img.putdata([int(footprint.get(i, 0) * 255) for i in range(S * S)])
    img = img.transform(img.size, Image.Transform.AFFINE,
                        (1, 0, -offset * SHADOW[0], 0, 1, -offset * SHADOW[1]))
    img = img.filter(ImageFilter.GaussianBlur(spread))
    for i, v in enumerate(img.getdata()):
        if v:
            a = strength * v / 255
            if canvas.a[i] > 0:
                canvas.darken(i, a * 0.8)
            canvas.over(i, (12, 14, 10), a * 0.85)


def ground_shadow(canvas, bx, by, rx, ry, strength=0.45):
    """Soft elliptical shadow on the ground at an object's base, thrown to the
    lower right (light from the upper left)."""
    img = Image.new("L", (S, S), 0)
    cx, cy = bx + rx * 0.35, by + ry * 0.25
    ImageDraw.Draw(img).ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=255)
    img = img.filter(ImageFilter.GaussianBlur(max(2.0, ry * 0.35)))
    for i, v in enumerate(img.getdata()):
        if v:
            a = strength * v / 255
            if canvas.a[i] > 0:
                canvas.darken(i, a * 0.6)
            else:
                canvas.over(i, (14, 16, 12), a * 0.8)


def shade_dome(canvas, pixels, color_at, *, light=0.55, rim=0.35, front=0.0, face_color=None,
               edge_soft=0.08):
    """Lambert-ish shading of a dome from its implicit normal, painted over."""
    lx, ly = LIGHT
    for i, h, dx, dy, d in pixels:
        nz = max(h, 0.05)
        nx, ny = dx * (1 - h) * 1.4, dy * (1 - h) * 1.4
        norm = math.sqrt(nx * nx + ny * ny + nz * nz)
        lam = (-(nx * lx + ny * ly) + 0.55 * nz) / norm
        x, y = i % S, i // S
        base = color_at(x, y, h)
        value = 0.62 + light * lam
        if front and dy > 0:
            # Three-quarter view: the lower face turns toward the viewer and
            # away from the light, reading as the object's side.
            t = smoothstep(0.15, 0.85, dy) * front
            base = mix3(base, face_color or base, t)
            value *= 1 - 0.35 * t
        # Edge darkening only on the side turned away from the light, so an
        # object never sits in a dark ring.
        away = max(0.0, (dx * -lx + dy * -ly) / max(1e-6, math.hypot(dx, dy)))
        value *= 1 - rim * smoothstep(0.8, 1.0, d) * away
        alpha = 1 - smoothstep(1 - edge_soft, 1.0, d)
        canvas.over(i, tuple(c * value for c in base), alpha)


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


@spec("boulders", "rounded weathered boulders, three-quarter view")
def render_boulders(rng, edge):
    canvas = Canvas()
    count = rng.choice((1, 2, 2)) if edge else rng.choice((2, 3, 3, 4))
    items = layout(rng, count, (34, 48), edge, extent=(1.4, 1.0))
    grit = fbm(rng, 12, 3)
    for bx, by, r in items:
        outline = lumpy_outline(rng, 4, 0.1)
        rx, ry = r * rng.uniform(1.0, 1.15), r * rng.uniform(1.05, 1.25)
        cy = by - ry * 0.75  # the body rises above its base
        ground_shadow(canvas, bx, by, rx * 1.05, ry * 0.45)
        pixels = dome(bx, cy, rx, ry, outline, power=0.5)
        tone = stone_palette(rng)
        dark = tuple(c * 0.55 for c in tone)
        warm = (tone[0] + 14, tone[1] + 6, tone[2] - 6)

        def color(x, y, h, tone=tone, warm=warm):
            g = grit(x, y)
            c = mix3(tone, warm, smoothstep(0.55, 0.8, g) * 0.6)
            return tuple(v * (0.82 + 0.36 * g) for v in c)

        shade_dome(canvas, pixels, color, light=0.65, rim=0.35, front=0.7, face_color=dark)
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
        twig_mask = {i: v / 255 for i, v in enumerate(twigs.getdata()) if v}

        bark = (58 + rng.uniform(-6, 6), 44 + rng.uniform(-5, 5), 32)
        for i, v in twig_mask.items():
            canvas.over(i, bark, v)
        o = rng.uniform(-8, 8)
        foliage(canvas, rng, cx, cy + r * 0.45, r, r * 1.05, base=(104 + o, 122 + o, 52), light=(176 + o, 182 + o, 92),
                dark=(42, 48, 22), clump=(6, 9), density=48, lumps=0.2, shadow=0.5)
    return canvas


def slab(rng, cx, cy, w, h, angle, jag):
    """An angular slab outline: a jittered rotated quadrilateral-ish polygon."""
    points = []
    corners = [(-w, -h), (w, -h), (w, h), (-w, h)]
    for k in range(4):
        x0, y0 = corners[k]
        x1, y1 = corners[(k + 1) % 4]
        for t in (0.0, 0.35, 0.7):
            x, y = lerp(x0, x1, t), lerp(y0, y1, t)
            x += rng.uniform(-jag, jag)
            y += rng.uniform(-jag, jag)
            c, s = math.cos(angle), math.sin(angle)
            points.append((cx + x * c - y * s, cy + x * s + y * c))
    return points


def render_rock_slabs(rng, edge, *, count, size, elongation, palette, lichen, strata, tall=1.0):
    canvas = Canvas()
    grit = fbm(rng, 14, 3)
    items = layout(rng, count, size, edge, extent=(elongation * 1.15 + 0.6, 1.9))
    for cx, cy, r in items:
        w, h = r * elongation, r * rng.uniform(0.55, 0.8)
        angle = rng.uniform(-0.7, 0.7)
        thickness = r * rng.uniform(0.7, 1.1) * tall
        top = slab(rng, cx, cy - thickness, w, h * 0.75, angle, r * 0.18)
        side = [(x, y + thickness) for x, y in top]
        top_mask = polygon_mask(top)
        body_mask = polygon_mask(top + side[::-1])
        hull = dict(body_mask)
        for i, v in polygon_mask(side).items():
            hull[i] = max(hull.get(i, 0), v)
        for i, v in top_mask.items():
            hull[i] = max(hull.get(i, 0), v)
        ground_shadow(canvas, cx, cy + h * 0.2, w * 0.95, h * 0.55, 0.5)
        tone = mix3(palette[0], palette[1], rng.random())
        face = tuple(c * 0.55 for c in tone)
        # Front faces first: everything of the hull below the top surface.
        for i, v in hull.items():
            x, y = i % S, i // S
            g = grit(x, y)
            band = 0.0
            if strata:
                band = 0.08 * math.sin((y + 0.4 * x) / 5.0 + g * 3)
            canvas.over(i, tuple(c * (0.85 + 0.25 * g + band) for c in face), v)
        # Lit top with a bevel: brighter near the upper-left rim.
        blurred = Image.new("L", (S, S), 0)
        blurred.putdata([int(top_mask.get(i, 0) * 255) for i in range(S * S)])
        blurred = list(blurred.filter(ImageFilter.GaussianBlur(r * 0.12)).getdata())
        for i, v in top_mask.items():
            x, y = i % S, i // S
            gx = (blurred[i + 1] if x + 1 < S else 0) - (blurred[i - 1] if x > 0 else 0)
            gy = (blurred[i + S] if i + S < S * S else 0) - (blurred[i - S] if i >= S else 0)
            slope = -(gx * LIGHT[0] + gy * LIGHT[1]) / 255
            g = grit(x, y)
            c = tuple(t * (0.74 + 0.42 * g + 1.1 * slope) for t in tone)
            if lichen and g > 0.78:
                c = mix3(c, (128, 134, 98), smoothstep(0.78, 0.9, g) * 0.35)
            canvas.over(i, c, v)
    return canvas


def render_fins(rng, edge, *, count, width, height, palette):
    """Upright jagged rock fins: a lit left face, a shaded right face, a
    ragged crest and vertical cracks, standing on the cell floor."""
    canvas = Canvas()
    grit = fbm(rng, 16, 3)
    fins = []
    for _ in range(count):
        w = rng.uniform(*width) * (0.9 if edge else 1.0)
        h = rng.uniform(*height) * (0.85 if edge else 1.0)
        bx = clamp(S / 2 + rng.uniform(-1, 1) * CELL * (0.18 if edge else 0.3), w * 0.8 + 8, S - w * 0.8 - 8)
        by = S / 2 + CELL * 0.3 + rng.uniform(-1, 1) * CELL * 0.08
        fins.append((bx, by, w, h, rng.uniform(-0.25, 0.25)))
    for bx, by, w, h, lean in sorted(fins, key=lambda f: f[1]):
        ground_shadow(canvas, bx, by, w * 0.75, w * 0.3, 0.5)
        # Outline: base, then a ragged crest from the left shoulder to the right.
        steps = 7
        crest = []
        for k in range(steps + 1):
            t = k / steps
            peak = 1 - abs(t - rng.uniform(0.35, 0.65)) * 1.3
            y = by - h * clamp(0.45 + 0.55 * peak + rng.uniform(-0.12, 0.12), 0.25, 1.0)
            x = bx - w / 2 + w * t + lean * (by - y)
            crest.append((x, y))
        outline = [(bx - w / 2, by)] + crest + [(bx + w / 2, by + w * 0.12)]
        mask = polygon_mask(outline)
        tone = mix3(palette[0], palette[1], rng.random())
        split = rng.uniform(0.35, 0.5)  # where the lit face turns into the shaded one
        for i, v in mask.items():
            x, y = i % S, i // S
            u = (x - (bx - w / 2) - lean * (by - y)) / w
            g = grit(x, y)
            lit = 1.08 if u < split else 0.62
            crack = 0.82 if abs(math.sin(x / 7.0 + g * 5)) < 0.08 else 1.0
            height = clamp((by - y) / h)
            c = tuple(t * lit * crack * (0.82 + 0.3 * g) * (0.86 + 0.18 * height) for t in tone)
            canvas.over(i, c, v)
    return canvas


@spec("ridge_rock", "upright jagged rock fins and shards")
def render_ridge_rock(rng, edge):
    return render_fins(rng, edge, count=rng.randint(1, 2) if edge else rng.randint(2, 3), width=(38, 60),
                       height=(96, 140), palette=((124, 122, 118), (148, 144, 136)))


@spec("outcrop", "one or two tall irregular bedrock massifs with sparse lichen")
def render_outcrop(rng, edge):
    return render_rock_slabs(rng, edge, count=1 if edge else rng.choice((1, 1, 2)),
                             size=(40, 52) if not edge else (32, 40), elongation=rng.uniform(0.95, 1.2),
                             palette=((134, 112, 92), (154, 132, 106)), lichen=True, strata=False,
                             tall=1.5)


# Connected foliage is a raised surface over its painted footprint, not a set of
# individually lit domes. Crowns are anchored on map vertices: every vertex has
# one of two seeded colours (cornerColours in TerrainMaterials.cpp), and each
# colour owns a cluster of crowns that spreads into the four cells around that
# vertex. A cell's frame therefore depends only on its corner mask and its four
# corner colours, and neighbouring frames agree wherever they meet, so large
# patches continue without seams and without a periodic repeat.
CONNECTED = ("hedge", "thicket")
CANOPY_VARIANTS = 16          # one per combination of the four corner colours
CANOPY_INTERIORS = 8          # interior crown arrangements of a filled cell
BAND = 20                     # vertex crowns centre this close to a cell edge
RAISE = 24                    # HD pixels the surface stands above its footprint
HALO = 22                     # height field kept around the cell
REACH = CELL - HALO - 4       # a cluster never reaches another cell's halo
CANOPY_TOP = 32               # frame rows above the cell (8 native)
CANOPY_LIGHT = (-0.50, -0.62, 0.60)
CANOPY_STYLE = {
    # Shadow, mid and highlight ramps keep the vegetation brighter and more
    # saturated than the flat grass at its lit edges, and cooler in crevices.
    "hedge": dict(
        ramp=((0.0, (8, 34, 26)), (0.28, (18, 72, 38)), (0.52, (36, 116, 46)),
              (0.76, (84, 162, 58)), (1.0, (168, 212, 98))),
        big=(3, 21, 25), medium=(4, 12, 17), sprigs=(3, 8, 11),
        tint=((0.88, (0, 0, 4)), (1.08, (8, 6, -6))),
        lumps=(7, 0.10), dab=(4, 7, 0.05, 40), amplitude=0.30, wood=0),
    "thicket": dict(
        ramp=((0.0, (30, 34, 16)), (0.28, (66, 76, 26)), (0.52, (112, 128, 38)),
              (0.76, (164, 172, 58)), (1.0, (222, 214, 118))),
        big=(3, 19, 22), medium=(4, 11, 16), sprigs=(4, 7, 10),
        tint=((0.88, (-4, 0, 2)), (1.08, (10, 4, -8))),
        lumps=(5, 0.22), dab=(4, 8, 0.07, 50), amplitude=0.32, wood=14),
}


CONNECTED_FRAMES = 16 * CANOPY_VARIANTS + CANOPY_VARIANTS * (CANOPY_INTERIORS - 1)


def connected_first(name):
    return len(ORDER) * FRAMES + CONNECTED.index(name) * CONNECTED_FRAMES


def frame_count(name):
    return FRAMES + (CONNECTED_FRAMES if name in CONNECTED else 0)


def connected_frame(index):
    """(mask, colours, interior) of a connected frame index past FRAMES.

    Mask-major frames with interior variant 0 come first, then the extra
    interior variants of the filled mask, colour-major."""
    if index < 16 * CANOPY_VARIANTS:
        return index // CANOPY_VARIANTS, index % CANOPY_VARIANTS, 0
    index -= 16 * CANOPY_VARIANTS
    return 15, index // (CANOPY_INTERIORS - 1), index % (CANOPY_INTERIORS - 1) + 1


def frame_number(name, index):
    return (first_frame(name) + index if index < FRAMES else
            connected_first(name) + index - FRAMES)


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
    peak = max(lumps(i * math.tau / 720) for i in range(720)) - 1
    scale = min(1.0, amount / peak) if peak > 0 else 1.0
    outline = lambda angle: 1 + (lumps(angle) - 1) * scale
    dab_lo, dab_hi, dab_amp, dab_area = style["dab"]
    half = int(radius * (1 + amount)) + dab_hi + 2
    n = half * 2
    values = [0.0] * (n * n)

    def surface(dx, dy):
        d = math.hypot(dx, dy) / radius / outline(math.atan2(dy, dx))
        return base + (top - base) * (1 - d * d) ** 0.55 if d < 1 else None

    for y in range(n):
        for x in range(n):
            h = surface(x + 0.5 - half, y + 0.5 - half)
            if h is not None:
                values[y * n + x] = h
    for _ in range(int(radius * radius / dab_area) + 3):
        angle, rho = rng.uniform(0, math.tau), math.sqrt(rng.random()) * radius * 1.02
        cx, cy = math.cos(angle) * rho, math.sin(angle) * rho
        under = surface(cx * 0.9, cy * 0.9)
        if under is None:
            continue
        r, amp = rng.uniform(dab_lo, dab_hi), dab_amp * rng.uniform(0.7, 1.2)
        for y in range(max(0, int(cy + half - r)), min(n, int(cy + half + r) + 2)):
            for x in range(max(0, int(cx + half - r)), min(n, int(cx + half + r) + 2)):
                q = 1 - ((x + 0.5 - half - cx) ** 2 + (y + 0.5 - half - cy) ** 2) / (r * r)
                if q > 0:
                    i = y * n + x
                    values[i] = max(values[i], under + amp * q ** 0.6)
    image = Image.new("L", (n, n))
    image.putdata([int(clamp(v) * 255) for v in values])
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
    hp, wp = heights.load(), wood.load()
    tints = [style["tint"][(colours >> k) & 1] for k in range(4)]
    broad = heights.filter(ImageFilter.GaussianBlur(8)).load()
    lx, ly, lz = CANOPY_LIGHT
    norm_l = math.sqrt(lx * lx + ly * ly + lz * lz)
    image = Image.new("RGB", (size, size))
    pix = image.load()
    for y in range(size):
        for x in range(size):
            h = hp[x, y] / 255
            gx = (hp[min(x + 2, size - 1), y] - hp[max(x - 2, 0), y]) / 255 * 18
            gy = (hp[x, min(y + 2, size - 1)] - hp[x, max(y - 2, 0)]) / 255 * 18
            lam = (-gx * lx - gy * ly + lz) / (math.sqrt(gx * gx + gy * gy + 1) * norm_l)
            # Crevices between crowns, and the low gaps, fall into shade.
            occlusion = clamp(1 + (h - broad[x, y] / 255) * 3.2) * (0.45 + 0.55 * smoothstep(0.05, 0.5, h))
            t = clamp((0.10 + 0.95 * lam) * occlusion + 0.18 * (h - 0.6))
            colour = ramp(style["ramp"], t)
            if style["wood"] and h < 0.28:
                gap = 1 - smoothstep(0.10, 0.28, h)
                colour = mix3(colour, (50, 40, 24), gap * 0.75)
                if wp[x, y]:
                    colour = mix3(colour, (104, 78, 46) if wp[x, y] > 200 else (78, 60, 36),
                                  gap * wp[x, y] / 255)
            # Each vertex colour also tints its corner; the bilinear blend gives
            # mottling larger than a cell without a repeated feature.
            u, v = clamp((x - HALO + 0.5) / CELL), clamp((y - HALO + 0.5) / CELL)
            weights = ((1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v)
            scale = sum(w * tints[k][0] for k, w in enumerate(weights))
            shift = [sum(w * tints[k][1][c] for k, w in enumerate(weights)) for c in range(3)]
            pix[x, y] = tuple(int(clamp(colour[c] * scale + shift[c], 0, 255)) for c in range(3))
    return image, heights


@functools.lru_cache(maxsize=16)
def canopy_field(mask):
    """Bilinear corner field over the cell and halo, clamped to the cell edge."""
    bits = [(mask >> k) & 1 for k in range(4)]
    size = CELL + 2 * HALO
    values = []
    for y in range(size):
        v = clamp((y - HALO + 0.5) / CELL)
        for x in range(size):
            u = clamp((x - HALO + 0.5) / CELL)
            value = lerp(lerp(bits[0], bits[1], u), lerp(bits[2], bits[3], u), v)
            if mask in (6, 9):
                # Substantial joins avoid hourglass pinholes at diagonal corners.
                value += 0.24 * math.sin(u * math.pi) ** 2 * math.sin(v * math.pi) ** 2
            values.append(int(clamp(value) * 255))
    image = Image.new("L", (size, size))
    image.putdata(values)
    return image


def shifted(image, dx, dy):
    """The image moved by (dx, dy), with uncovered pixels left empty."""
    out = Image.new(image.mode, image.size)
    out.paste(image, (dx, dy))
    return out


def render_connected(name, mask, colours, interior=0):
    """A 32x48 native frame: ground shadow, front wall, then the raised top.

    The frame owns the canopy whose footprint lies inside its cell. The top is
    drawn RAISE pixels up and the front wall fills down to the footprint, so the
    next row's frame covers this row's wall wherever the canopy continues.
    """
    frame = Image.new("RGBA", (CELL, CELL + 64))
    if not mask:
        return frame
    style = CANOPY_STYLE[name]
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
    cell = (HALO, HALO, HALO + CELL, HALO + CELL)
    own = Image.new("L", coverage.size)
    own.paste(coverage.crop(cell), cell[:2])

    # Soft contact shadow on the ground inside this cell, from all canopy nearby.
    shadow = shifted(coverage, 8, 10).filter(ImageFilter.GaussianBlur(4)).point(lambda v: int(v * 0.42))
    layer = Image.new("RGBA", (CELL, CELL), (6, 26, 10, 255))
    layer.putalpha(shadow.crop(cell))
    frame.alpha_composite(layer, (0, CANOPY_TOP))

    # Front wall from the footprint up to the raised top: brightest just under
    # the top edge, darkening to the ground.
    wall_alpha, wall_height = Image.new("L", own.size), Image.new("L", own.size)
    solid = own.point(lambda v: 255 if v > 127 else 0)
    for d in range(RAISE + 1):
        wall_alpha = ImageChops.lighter(wall_alpha, shifted(own, 0, -d))
        wall_height = ImageChops.lighter(wall_height, shifted(solid, 0, -d).point(lambda v, d=d: v and int(d * 255 / RAISE)))
    texture = shifted(surface, 0, -RAISE)
    wall = Image.composite(texture.point(lambda v: int(v * 0.66)), texture.point(lambda v: int(v * 0.30)), wall_height)
    region = (HALO, HALO - RAISE, HALO + CELL, HALO + CELL)
    layer = wall.crop(region).convert("RGBA")
    layer.putalpha(wall_alpha.crop(region))
    frame.alpha_composite(layer, (0, CANOPY_TOP - RAISE))

    # Raised top. Its silhouette is lit where it faces the light and shaded on
    # the far side, so the edge reads as a rounded rim rather than a cut-out.
    lit = ImageChops.subtract(falloff, shifted(falloff, 4, 4)).point(lambda v: min(255, v * 2))
    dark = ImageChops.subtract(falloff, shifted(falloff, -4, -4)).point(lambda v: min(255, v * 2))
    top = Image.composite(surface.point(lambda v: min(255, int(v * 1.28 + 14))), surface, lit)
    top = Image.composite(surface.point(lambda v: int(v * 0.62)), top, dark)
    layer = top.crop(cell).convert("RGBA")
    layer.putalpha(own.crop(cell))
    frame.alpha_composite(layer, (0, CANOPY_TOP - RAISE))
    return frame


# --------------------------------------------------------------------------
# Frames, provenance and CLI
# --------------------------------------------------------------------------


def render_frame(name, index):
    rng = random.Random(fnv1a32(f"decor:{name}:{index}"))
    hd = (render_connected(name, *connected_frame(index - FRAMES)) if index >= FRAMES else
          SPECS[name].render(rng, index >= FULL).image())
    native = hd.resize((hd.width // SCALE, hd.height // SCALE), Image.Resampling.BOX)
    return native, hd


def _job(name):
    # Keep a material on one worker so all masks reuse its four textures.
    return [(name, index, native.tobytes(), hd.tobytes())
            for index in range(frame_count(name))
            for native, hd in (render_frame(name, index),)]


def synthesize(names, jobs=None):
    jobs = jobs or min(8, os.cpu_count() or 1)
    with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(names))) as pool:
        rendered = list(pool.map(_job, names))
    out = {}
    for group in rendered:
        for name, index, native, hd in group:
            out.setdefault(name, [None] * frame_count(name))[index] = (
                Image.frombytes("RGBA", (32, 48) if index >= FRAMES else (NATIVE, NATIVE), native),
                Image.frombytes("RGBA", (128, 192) if index >= FRAMES else (S, S), hd))
    return out


ORDER = ["boulders", "hedge", "thicket", "ridge_rock", "outcrop"]
assert sorted(ORDER) == sorted(SPECS)


def first_frame(name):
    return ORDER.index(name) * FRAMES


def generator_sha():
    return hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def provenance_path(root):
    return root / "datasrc/gfx/terrain-decor/provenance.json"


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
            "runtime_sha256": hashes,
            "highres_sha256": hd_hashes,
        }
    register_frames(hd_frames, HD_CATEGORY, root)
    document.update({
        "generator": "tools/artwork/terrain_decor.py",
        "method": "procedural",
        "generator_sha256": generator_sha(),
        "native_size": NATIVE,
        "render_size": S,
        "connected_native_size": [32, 48],
        "connected_corners": True,
        "pillow": PIL.__version__,
    })
    document["sets"] = {name: document["sets"][name] for name in ORDER if name in document["sets"]}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n")


def check(results, root):
    path = provenance_path(root)
    if not path.exists():
        return ["missing datasrc/gfx/terrain-decor/provenance.json"]
    provenance = json.loads(path.read_text())
    problems = []
    if provenance.get("generator_sha256") != generator_sha():
        problems.append("generator changed; re-run terrain_decor.py")
    for name, frames in results.items():
        recorded = provenance["sets"].get(name)
        if recorded is None:
            problems.append(f"{name}: missing from provenance")
            continue
        for i, (native, hd) in enumerate(frames):
            n = frame_number(name, i)
            for relative, image, table in ((f"{PREFIX}{n}.png", native, recorded["runtime_sha256"]),
                                           (f"{HD_PREFIX}{n}.png", hd, recorded["highres_sha256"])):
                target = root / relative
                if not target.exists():
                    problems.append(f"{relative}: missing")
                    continue
                with Image.open(target) as committed:
                    digest = pixel_sha256(committed)
                if digest != pixel_sha256(image) or table.get(relative) != digest:
                    problems.append(f"{relative}: differs from a fresh synthesis")
    return problems


def write_catalog(root):
    """Add or refresh each obstacle material's decor block in tileset.json."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from terrain_profile_curves import dump_catalog

    path = root / "data/terrain/tileset.json"
    document = json.loads(path.read_text())
    # The canopy owns foliage irregularity and depth. A quiet underlay must
    # not expose the old brush profile's spines below the raised surface.
    canopy_profile = {"key": "canopy", "shape": "patch", "roughness_q8": 0,
                      "amplitude_q8": 0, "feather_q8": 128, "speckle_q8": 0,
                      "bridge_q8": 512, "contours_q12": [[0] * 9 for _ in range(32)]}
    document["profiles"] = [p for p in document["profiles"] if p["key"] != "canopy"] + [canopy_profile]
    for pair in document["pair_treatments"]:
        if pair["a"] in CONNECTED or pair["b"] in CONNECTED:
            pair["profile"] = "canopy"
    for material in document["materials"]:
        if material["key"] in SPECS:
            first = first_frame(material["key"])
            material["decor"] = {
                "sprite": PREFIX,
                "full": list(range(first, first + FULL)),
                "edge": list(range(first + FULL, first + FRAMES)),
            }
        if material["key"] in CONNECTED:
            start = connected_first(material["key"])
            material["decor"]["connected"] = list(range(start, start + 16 * CANOPY_VARIANTS))
            material["decor"]["connected_interior"] = list(range(start + 16 * CANOPY_VARIANTS,
                                                                 start + CONNECTED_FRAMES))
            material["decor"].pop("connected_grid", None)
            material["decor"]["connected_corners"] = True
            material["profile"] = "canopy"
    path.write_text(dump_catalog(document))


def connected_sheet(results):
    """Before/after patches, strips, corners, isolated vertices and a hole.

    Reproduce row painter order and corner masks rather than arranging isolated
    swatches. This makes grid stripes and directional joining errors visible.
    """
    width, height = 16, 12
    panel_w, panel_h = width * 32, height * 32
    out = Image.new("RGBA", (panel_w * 2, (panel_h + 28) * len(results)), (32, 35, 30, 255))
    labels = ImageDraw.Draw(out)
    grass = Image.open(ROOT / "data/gfx/terrain0.png").convert("RGBA")
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
                    patch.alpha_composite(frame, (x * 32 - (frame.width - 32) // 2, y * 32 - (frame.height - 32) // 2))
            oy = row * (panel_h + 28)
            out.paste(patch, (int(after) * panel_w, oy + 28))
            labels.text((int(after) * panel_w + 8, oy + 7), f"{name}: {'connected canopy' if after else 'previous bushes'}", fill="white")
    return out


def sheet(results, ground=None):
    """Review sheet: each frame over its ground tile (or a neutral green), at 2x,
    plus a 4x3 field of full frames drawn in row order with overlaps."""
    names = list(results)
    cell = NATIVE * 2
    width = FRAMES * cell + 32 + 4 * 64 + 16
    out = Image.new("RGBA", (width, len(names) * (cell + 3 * 64 // 2 + 24)), (36, 36, 40, 255))
    y = 0
    for name in names:
        frames = results[name]
        bg = (ground or {}).get(name)
        for i, (native, _) in enumerate(frames[:FRAMES]):
            tile = Image.new("RGBA", (NATIVE, NATIVE), (82, 120, 64, 255))
            if bg:
                for oy in range(0, NATIVE, 32):
                    for ox in range(0, NATIVE, 32):
                        tile.paste(bg, (ox - 8, oy - 8))
            tile.alpha_composite(native)
            out.paste(tile.resize((cell, cell), Image.Resampling.NEAREST), (i * cell, y))
        # Field: 4 x 3 cells, drawn back to front like the runtime.
        field_img = Image.new("RGBA", (4 * 32 + 16, 3 * 32 + 16), (82, 120, 64, 255))
        if bg:
            for fy in range(0, field_img.height, 32):
                for fx in range(0, field_img.width, 32):
                    field_img.paste(bg, (fx, fy))
        rng = random.Random(fnv1a32(name))
        for fy in range(3):
            for fx in range(4):
                field_img.alpha_composite(frames[rng.randrange(FULL)][0], (fx * 32 + 8 - 8, fy * 32 + 8 - 8))
        out.paste(field_img.resize((field_img.width * 2, field_img.height * 2), Image.Resampling.NEAREST),
                  (FRAMES * cell + 32, y))
        y += cell + 3 * 64 // 2 + 24
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--decor", action="append", help="decor name (repeatable; default all)")
    parser.add_argument("--sheet", type=Path, help="write a review sheet and nothing else")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-catalog", action="store_true", help="also add decor blocks to tileset.json")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    names = args.decor or list(ORDER)
    root = args.root.resolve()
    results = synthesize(names)
    if args.sheet:
        ground = {}
        for name in names:
            path = root / f"data/gfx/terrain-{name}0.png"
            if path.exists():
                ground[name] = Image.open(path).convert("RGBA")
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        (connected_sheet(results) if all(n in CONNECTED for n in names) else sheet(results, ground)).save(args.sheet)
        print(f"Wrote {args.sheet}")
        return
    if args.check:
        problems = check(results, root)
        for p in problems:
            print("FAIL", p)
        if problems:
            raise SystemExit(1)
        print(f"PASS {len(names)} decor sets match their committed frames and provenance")
        return
    write(results, root)
    if args.write_catalog:
        write_catalog(root)
    print(f"Wrote {len(names)} decor sets ({sum(frame_count(n) for n in names)} frames)")


if __name__ == "__main__":
    main()
