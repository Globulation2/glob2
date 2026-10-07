#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise the raised decor sprites drawn over obstacle terrain.

Obstacle terrain (boulders, hedge, thicket, ridge rock, outcrop) is drawn in
two layers, like forest: the ground material in the terrain compositor, then a
decor sprite per cell drawn with the resources in screen-row order, so objects
overlap their neighbours and occlude each other. This tool renders those
sprites from scratch.

Each sprite is a 64x64 native frame (256x256 HD) centred on its 32x32 cell.
Objects stand on the lower part of their cell and rise up to sixteen native
pixels into the cell above, as trees do; drawn back to front, interior cells
overlap into one continuous mass. A frame
holds a small cluster of objects in the game's soft three-quarter view: a lit
top, a darker front face below it, top-left light and a soft contact shadow to
the lower right, drawn back to front. Frames come in two sizes: `full` for
cells inside an obstacle region and `edge` (smaller, pulled toward the cell
centre) for cells with open neighbours; the runtime picks by neighbour count.

Usage (pinned encoder interpreter, as for terrain_synth.py):

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/terrain_decor.py                  # write all frames + HD
    "$ENC" tools/artwork/terrain_decor.py --sheet artifacts/terrain/decor.png
    "$ENC" tools/artwork/terrain_decor.py --check

All sets share one sprite, `data/gfx/terrain-decorN.png`, with set k at
frames 12k..12k+11 in `ORDER`, so the runtime can batch every decor draw like
resources. HD frames are registered through tools/artwork/highres_pack.py and
provenance is written to `datasrc/gfx/terrain-decor/provenance.json`.
`--write-catalog` adds the `decor` block to each material in tileset.json.
Pure Python 3 plus Pillow; no numpy.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import random
import sys
from dataclasses import dataclass, field
from pathlib import Path

import PIL
from PIL import Image, ImageDraw, ImageFilter

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


# --------------------------------------------------------------------------
# Frames, provenance and CLI
# --------------------------------------------------------------------------


def render_frame(name, index):
    rng = random.Random(fnv1a32(f"decor:{name}:{index}"))
    hd = SPECS[name].render(rng, index >= FULL).image()
    native = hd.resize((NATIVE, NATIVE), Image.Resampling.BOX)
    return native, hd


def _job(args):
    name, index = args
    native, hd = render_frame(name, index)
    return name, index, native.tobytes(), hd.tobytes()


def synthesize(names, jobs=None):
    tasks = [(n, i) for n in names for i in range(FRAMES)]
    jobs = jobs or min(32, os.cpu_count() or 1)
    with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(tasks))) as pool:
        rendered = list(pool.map(_job, tasks))
    out = {}
    for name, index, native, hd in rendered:
        out.setdefault(name, [None] * FRAMES)[index] = (
            Image.frombytes("RGBA", (NATIVE, NATIVE), native), Image.frombytes("RGBA", (S, S), hd))
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
            n = first_frame(name) + i
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
            n = first_frame(name) + i
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
    for material in document["materials"]:
        if material["key"] in SPECS:
            first = first_frame(material["key"])
            material["decor"] = {
                "sprite": PREFIX,
                "full": list(range(first, first + FULL)),
                "edge": list(range(first + FULL, first + FRAMES)),
            }
    path.write_text(dump_catalog(document))


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
        for i, (native, _) in enumerate(frames):
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
        sheet(results, ground).save(args.sheet)
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
    print(f"Wrote {len(names)} decor sets of {FRAMES} frames")


if __name__ == "__main__":
    main()
