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
        # Coverage by object bodies alone, so a shadow can tell an object it
        # darkens from earlier shadow on the ground it should deepen.
        self.body = [0.0] * (S * S)

    def over(self, i, color, alpha, body=True):
        if alpha <= 0:
            return
        if body:
            self.body[i] = alpha + self.body[i] * (1 - alpha)
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
            b = canvas.body[i]
            canvas.darken(i, a * 0.8 * b)
            canvas.over(i, (12, 14, 10), a * 0.85 * (1 - b), body=False)


def ground_shadow(canvas, bx, by, rx, ry, strength=0.45):
    """Soft elliptical shadow on the ground at an object's base, thrown to the
    lower right (light from the upper left)."""
    img = Image.new("L", (S, S), 0)
    cx, cy = bx + rx * 0.35, by + ry * 0.25
    ImageDraw.Draw(img).ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=255)
    img = img.filter(ImageFilter.GaussianBlur(max(2.0, ry * 0.35)))
    for i, v in enumerate(img.getdata()):
        if v:
            # Darken the objects it falls on; deepen shadow already on the
            # ground rather than stopping at its faint rim.
            a = strength * v / 255
            b = canvas.body[i]
            canvas.darken(i, a * 0.6 * b)
            canvas.over(i, (14, 16, 12), a * 0.8 * (1 - b), body=False)


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


LIGHT3 = tuple(c / math.hypot(LIGHT[0], LIGHT[1], 0.9) for c in (LIGHT[0], LIGHT[1], 0.9))


def facet_rock(rng, cx, cy, r, squash=0.8, height=0.95):
    """A convex faceted rock: the soft minimum of jittered sloped planes and a
    tilted top, so the outline is angular and each facet catches the light on
    its own. Planes facing the viewer (down-screen) are steeper, giving the
    rock a narrow front face in the three-quarter view.

    Returns [(i, alpha, nx, ny, nz, rim)] for its silhouette, where rim is
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
    out = []
    for y in range(max(0, int(cy - ry * 1.4)), min(S, int(cy + ry * 1.4) + 1)):
        v = (y + 0.5 - cy) / ry
        for x in range(max(0, int(cx - r * 1.4)), min(S, int(cx + r * 1.4) + 1)):
            u = (x + 0.5 - cx) / r
            hs = [(s * (1 - (u * pu + v * pv)), -s * pu, -s * pv) for s, pu, pv in planes]
            hs.append((top + tilt[0] * u + tilt[1] * v, tilt[0], tilt[1]))
            m = min(h for h, _, _ in hs)
            if m <= 0:
                continue
            ws = [math.exp(-(h - m) / soft) for h, _, _ in hs]
            total = sum(ws)
            h = m - soft * math.log(total)
            if h <= 0:
                continue
            gu = sum(w * g for w, (_, g, _) in zip(ws, hs)) / total
            gv = sum(w * g for w, (_, _, g) in zip(ws, hs)) / total
            nx, ny, nz = -height * gu, -height * gv / squash, 1.0
            n = math.sqrt(nx * nx + ny * ny + nz * nz)
            out.append((y * S + x, clamp(h / 0.03), nx / n, ny / n, nz / n, 1 - smoothstep(0, 0.14, h)))
    return out


def place_rocks(rng, sizes, edge):
    """Base points for rocks of the given radii, largest first, around a
    cluster centre that wanders off the cell centre so neighbouring cells do
    not line up in rows. Sorted back to front by base."""
    spread = 0.24 if edge else 0.42
    wander = 0.12 if edge else 0.2
    ox = S / 2 + rng.uniform(-1, 1) * CELL * wander
    oy = S / 2 + CELL * 0.14 + rng.uniform(-1, 1) * CELL * wander * 1.2
    items = []
    for r, kind in sorted(sizes, key=lambda t: -t[0]):
        ry = r * 0.8
        lo_x, hi_x = r * 1.45 + 6, S - r * 1.45 - 6
        lo_y, hi_y = ry * 2.0 + 8, S - ry * 0.9 - 16
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


@spec("boulders", "faceted weathered boulders and rubble, three-quarter view")
def render_boulders(rng, edge):
    canvas = Canvas()
    grit = fbm(rng, 12, 3)
    lichen = fbm(rng, 18, 2)
    family = boulder_palette(rng)  # one rock type per frame, varied per stone
    owner = [-1] * (S * S)
    rocks = place_rocks(rng, boulder_sizes(rng, edge), edge)
    for k, (bx, by, r, kind) in enumerate(rocks):
        squash = rng.uniform(0.72, 0.86)
        cy = by - r * squash * 0.55  # the body rises above its base
        ground_shadow(canvas, bx + r * 0.1, by, r * 1.05, r * squash * 0.42, 0.55)
        j = rng.uniform(-9, 9)
        tone = tuple(c + j + rng.uniform(-2, 2) for c in family)
        face = (tone[0] * 0.5, tone[1] * 0.5, tone[2] * 0.52)
        speck = random.Random(rng.random())
        for i, alpha, nx, ny, nz, rim in facet_rock(rng, bx, cy, r, squash,
                                                    0.7 if kind == "rubble" else 0.95):
            x, y = i % S, i // S
            lam = max(0.0, nx * LIGHT3[0] + ny * LIGHT3[1] + nz * LIGHT3[2])
            g = grit(x, y)
            c = mix3(tone, face, smoothstep(0.25, 0.75, ny) * 0.6)
            if nz > 0.8 and lichen(x, y) > 0.72:
                c = mix3(c, (128, 128, 92), smoothstep(0.72, 0.82, lichen(x, y)) * 0.3)
            value = (0.4 + 0.8 * lam) * (0.86 + 0.28 * g) * speck.uniform(0.96, 1.04)
            # A dark line on the outline turned from the light separates stones.
            away = clamp(0.5 + 0.5 * (nx * -LIGHT[0] + ny * -LIGHT[1]) / max(1e-6, math.hypot(nx, ny)))
            value *= 1 - 0.35 * rim * away
            canvas.over(i, tuple(v * value for v in c), alpha)
            if alpha > 0.5:
                owner[i] = k
    # Crevices where stones meet, and each stone's shade on the others.
    seam = Image.new("L", (S, S), 0)
    seam.putdata([255 if o >= 0 and ((i % S + 1 < S and owner[i + 1] not in (-1, o)) or
                                     (i + S < S * S and owner[i + S] not in (-1, o))) else 0
                  for i, o in enumerate(owner)])
    seam = list(seam.filter(ImageFilter.GaussianBlur(3.5)).getdata())
    shade = [0.0] * (S * S)
    for k, (bx, by, r, _) in enumerate(rocks):
        mask = Image.new("L", (S, S), 0)
        mask.putdata([255 if o == k else 0 for o in owner])
        d = r * 0.3
        mask = mask.transform(mask.size, Image.Transform.AFFINE,
                              (1, 0, -d * SHADOW[0], 0, 1, -d * SHADOW[1]))
        for i, v in enumerate(mask.filter(ImageFilter.GaussianBlur(r * 0.12)).getdata()):
            if v and owner[i] not in (-1, k):
                shade[i] = max(shade[i], v / 255)
    for i in range(S * S):
        if owner[i] >= 0:
            canvas.darken(i, min(0.7, seam[i] / 255 * 2.2 * 0.6 + shade[i] * 0.35))
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


LIGHT3 = (-0.5, -0.6, 0.62)  # toward the upper left and up; x right, y south, z up
VIEW = (0.0, 0.7071, 0.7071)  # toward the viewer in the three-quarter view


def face_mask(points, inset=0.0):
    """Supersampled coverage of a polygon as {index: coverage}, cropped to its
    bounding box; `inset` shrinks it toward its centroid by that many pixels."""
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
        return {}
    w, h = x1 - x0, y1 - y0
    img = Image.new("L", (w * 2, h * 2), 0)
    ImageDraw.Draw(img).polygon([((x - x0) * 2, (y - y0) * 2) for x, y in points], fill=255)
    data = img.resize((w, h), Image.Resampling.BOX).getdata()
    return {(y0 + k // w) * S + x0 + k % w: v / 255 for k, v in enumerate(data) if v}


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
        for i, v in face_mask(rock.silhouette()).items():
            canvas.over(i, tuple(c * 0.3 for c in rock.tone), v)
        xs = [x for x, _, _ in rock.ring]
        fissures = [rng.uniform(min(xs), max(xs)) for _ in range(rng.randint(1, 3))]
        for poly, normal, kind in rock.faces():
            value = facet_value(normal) * rng.uniform(0.94, 1.06)
            inner = face_mask(poly, inset=1.6)
            for i, v in face_mask(poly).items():
                x, y = i % S, i // S
                g = grit(x, y)
                shade = value * (0.84 + 0.3 * g + 0.08 * (fine(x, y) - 0.5))
                if kind == "side":
                    # Darker toward the foot, faint bedding lines across the wall.
                    foot = smoothstep(ground - height, ground + 4, y)
                    shade *= 1 - 0.28 * foot * foot
                    if strata:
                        shade *= 1 - strata * smoothstep(0.75, 0.95, math.sin((y - 0.35 * x) / 4.5 + g * 4))
                    # Vertical joints that wander a little down the face.
                    if min(abs(x - f - (g - 0.5) * 10) for f in fissures) < 1.4:
                        shade *= 0.72
                color = tuple(c * shade for c in rock.tone)
                if lichen and kind == "top":
                    m = moss(x, y)
                    color = mix3(color, lichen, smoothstep(0.68, 0.82, m) * 0.35 * (0.6 + 0.4 * value))
                seam = (1 - inner.get(i, 0)) * (0.22 if kind == "side" else 0.05)
                if seam > 0:
                    color = tuple(c * (1 - seam) for c in color)
                canvas.over(i, color, v)
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
