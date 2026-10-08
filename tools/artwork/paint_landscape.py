#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Paint the landscape resource sprites (the `landscape-resources` experiment).

Every resource gets one frame per stock level, written as
`data/gfx/resource-<key><frame>.png`, plus a 4x HD frame registered through
tools/artwork/highres_pack.py. Trees use the stock trees' 40x44 frame and two
variants per level; fish add FISH_FRAMES animation frames per level.

The look follows the classic sprites rather than realism: soft sphere-shaded
blobs in saturated pastel ramps, top-left light, a dark rim and a contact
shadow to the lower right. Painting happens at 16x supersampling and is
box-filtered down. Every random choice comes from a fixed per-resource seed,
so rerunning the tool unchanged reproduces the committed pixels.

    python3 tools/artwork/paint_landscape.py --sheet artifacts/landscape.png
    python3 tools/artwork/paint_landscape.py --sheet artifacts/water.png --ground terrain-water0 fish rice
    python3 tools/artwork/paint_landscape.py                 # write frames + HD
    python3 tools/artwork/package_runtime.py --check

Needs NumPy, SciPy and Pillow. The resource registry and simulation are
untouched; `FRAMES` below documents the frame layout the registry must use.
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
SS = 16
HD_CATEGORY = "procedural-materials"
HD_RECIPE = "procedural resource painter v1"
LIGHT = np.array([-0.55, -0.75, 0.9])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


class Canvas:
    """A supersampled RGBA painting surface in classic-pixel coordinates.

    The origin is the frame's top-left corner; a 40x44 tree frame is drawn
    centred on the 32x32 cell, so the cell spans x 4..36 and y 6..38."""

    def __init__(self, w=32, h=32, zoom=1.0):
        """`zoom` > 1 enlarges everything drawn about the frame centre."""
        self.w, self.h = w, h
        self.yy, self.xx = np.mgrid[0:h * SS, 0:w * SS].astype(np.float32) / SS
        self.xx = w / 2 + (self.xx - w / 2) / zoom
        self.yy = h / 2 + (self.yy - h / 2) / zoom
        self.rgb = np.zeros((h * SS, w * SS, 3), np.float32)
        self.alpha = np.zeros((h * SS, w * SS), np.float32)

    def put(self, mask, col):
        col = np.asarray(col, np.float32)
        m = np.clip(mask, 0, 1)[..., None]
        self.rgb = self.rgb * (1 - m) + col * m
        self.alpha = np.maximum(self.alpha, m[..., 0])

    def blob(self, cx, cy, rx, ry, stops, amb=0.42, gloss=0.0):
        """A sphere-shaded ellipse coloured through a brightness ramp."""
        dx, dy = (self.xx - cx) / rx, (self.yy - cy) / ry
        d = np.hypot(dx, dy)
        m = np.clip((1 - d) * SS * min(rx, ry) / 1.5, 0, 1)
        nz = np.sqrt(np.clip(1 - d * d, 0, 1))
        sh = np.clip(amb + 0.7 * (LIGHT[0] * dx + LIGHT[1] * dy + LIGHT[2] * nz), 0, 1.3)
        col = ramp(sh, stops)
        if gloss:
            hl = np.clip(1 - np.hypot(dx + 0.38, dy + 0.42) / 0.32, 0, 1) ** 2
            col = col * (1 - gloss * hl[..., None]) + 255 * gloss * hl[..., None]
        self.put(m, col)
        return m

    def cone(self, cx, top, base, half, stops):
        """A shaded upright triangle (conifer tier) from `top` y to `base` y."""
        t = np.clip((self.yy - top) / (base - top), 0, 1)
        hw = half * t
        u = (self.xx - cx) / np.maximum(hw, 1e-3)
        inside = (self.yy >= top) & (self.yy <= base)
        edge = np.clip((hw - np.abs(self.xx - cx)) * SS / 1.2, 0, 1)
        bottom = np.clip((base - self.yy) * SS / 1.2, 0, 1)
        m = edge * bottom * inside
        sh = np.clip(0.95 - 0.55 * u - 0.25 * t + 0.1 * (1 - np.abs(u)), 0, 1.3)
        self.put(m, ramp(sh, stops))

    def line(self, p0, p1, w0, col, w1=None):
        """A tapered antialiased stroke from width w0 to w1."""
        w1 = w0 if w1 is None else w1
        (x0, y0), (x1, y1) = p0, p1
        dx, dy = x1 - x0, y1 - y0
        t = np.clip(((self.xx - x0) * dx + (self.yy - y0) * dy) / (dx * dx + dy * dy + 1e-9), 0, 1)
        d = np.hypot(self.xx - (x0 + t * dx), self.yy - (y0 + t * dy))
        w = w0 + (w1 - w0) * t
        self.put(np.clip((w - d) * SS / 1.2, 0, 1), col)

    def poly(self, pts, col):
        img = Image.new("L", (self.w * SS, self.h * SS), 0)
        x0, y0 = self.xx[0, 0], self.yy[0, 0]
        sx = (self.xx[0, 1] - x0) * SS
        sy = (self.yy[1, 0] - y0) * SS
        ImageDraw.Draw(img).polygon([((x - x0) / sx * SS, (y - y0) / sy * SS) for x, y in pts], fill=255)
        self.put(np.asarray(img, np.float32) / 255, col)

    def finish(self, outline=(40, 22, 60), outline_k=0.5, shadow=0.4):
        """Darken the silhouette rim and add the lower-right contact shadow."""
        a = self.alpha
        d = ndimage.distance_transform_edt(a > 0.5) / SS
        r = np.clip(1 - d / 0.9, 0, 1)[..., None]
        rgb = self.rgb * (1 - r * outline_k) + np.array(outline, np.float32) * r * outline_k
        s = ndimage.shift(a, (1.3 * SS, 1.6 * SS), order=1)
        s = ndimage.gaussian_filter(s, 1.1 * SS) * shadow * (1 - a)
        total = a + s
        col = np.where(total[..., None] > 0, rgb * a[..., None] / np.maximum(total[..., None], 1e-6), 0)
        return np.dstack([col, total])


def ramp(t, stops):
    t = np.clip(t, 0, stops[-1][0])
    out = np.zeros(t.shape + (3,), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        m = (t >= p0) & (t <= p1)
        f = ((t - p0) / max(p1 - p0, 1e-6))[..., None]
        out[m] = (np.array(c0) * (1 - f) + np.array(c1) * f)[m]
    return out


def shade(c, k):
    return tuple(min(255, v * k) for v in c)


def pal(dark, mid, light, hi=(255, 250, 245)):
    """Four-stop brightness ramp in the classic sprites' pastel style."""
    return [(0.0, dark), (0.45, mid), (0.85, light), (1.3, hi)]


# Cell placement slots inside a 32x32 cell, back to front, for growth stages.
SLOTS5 = [(16, 17), (9, 11), (23, 12), (10, 24), (23, 24)]


def stage_positions(rng, count, slots=SLOTS5, jitter=1.2):
    pts = [(x + rng.uniform(-jitter, jitter), y + rng.uniform(-jitter, jitter)) for x, y in slots[:count]]
    return sorted(pts, key=lambda p: p[1])


# ------------------------------------------------------------------------- trees

JUNGLE_LEAF = [pal((18, 70, 66), (40, 168, 128), (150, 236, 170)),
               pal((26, 60, 92), (52, 150, 168), (160, 230, 220)),
               pal((40, 80, 40), (110, 190, 70), (210, 250, 150))]
BARK = (92, 58, 70)


def tree_frame(level, variant, kind):
    """One tree growth stage. `level` 1..5 is the stock; size and count grow."""
    c = Canvas(40, 44)
    rng = np.random.default_rng({"jungle": 101, "pine": 202}[kind] + variant * 17 + level)
    count = [1, 2, 3, 4, 5][level - 1]
    scale = [0.75, 0.95, 1.12, 1.28, 1.42][level - 1]
    slots = [(20, 24), (12, 19), (28, 20), (13, 29), (27, 29)] if variant == 0 else \
            [(20, 23), (28, 18), (12, 20), (25, 30), (12, 30)]
    pts = sorted([(x + rng.uniform(-1, 1), y + rng.uniform(-1, 1)) for x, y in slots[:count]], key=lambda p: p[1])
    for i, (x, y) in enumerate(pts):
        s = scale * rng.uniform(0.85, 1.05) * (1.0 if i == len(pts) - 1 or count < 3 else 0.9)
        if kind == "jungle":
            jungle_tree(c, x, y + 6 * s, s, rng)
        else:
            pine_tree(c, x, y + 6 * s, s, rng)
    return c.finish(outline=(20, 30, 40))


def jungle_tree(c, x, base, s, rng):
    """A curved trunk with a broad, lumpy, layered canopy and pink blossoms."""
    lean = rng.uniform(-2.5, 2.5) * s
    top = base - 13 * s
    c.line((x, base), (x + lean * 0.5, base - 7 * s), 1.6 * s, BARK, 1.1 * s)
    c.line((x + lean * 0.5, base - 7 * s), (x + lean, top + 2 * s), 1.1 * s, shade(BARK, 1.15), 0.8 * s)
    leaf = JUNGLE_LEAF[rng.integers(len(JUNGLE_LEAF))]
    lumps = [(0, 0, 7.5, 5.0), (-5.5, 2.0, 5.0, 3.8), (5.5, 1.5, 5.2, 3.8), (-2.5, -3.5, 5.0, 3.6), (3.0, -3.8, 4.8, 3.4)]
    for dx, dy, rx, ry in sorted(lumps, key=lambda q: -q[1]):
        c.blob(x + lean + dx * s, top + dy * s, rx * s, ry * s, leaf)
    for _ in range(int(3 + 4 * s)):
        a = rng.uniform(0, math.tau)
        r = rng.uniform(0.2, 0.8)
        c.blob(x + lean + math.cos(a) * 7 * s * r, top + math.sin(a) * 4.5 * s * r - 1 * s,
               0.9 * s + 0.3, 0.8 * s + 0.3, pal((150, 40, 110), (240, 110, 190), (255, 200, 235)), gloss=0.3)


PINE = [pal((14, 40, 70), (40, 110, 140), (130, 210, 220), (235, 255, 255)),
        pal((20, 50, 52), (40, 130, 110), (140, 225, 190), (240, 255, 245))]


def pine_tree(c, x, base, s, rng):
    """A short trunk under three stacked conifer tiers with frosted tips."""
    c.line((x, base), (x, base - 4 * s), 1.2 * s, BARK, 1.0 * s)
    leaf = PINE[rng.integers(len(PINE))]
    h = 20 * s
    tiers = [(base - 3 * s, 7.0), (base - 3 * s - h * 0.3, 5.6), (base - 3 * s - h * 0.55, 4.2)]
    for i, (b, half) in enumerate(tiers):
        top = b - h * (0.55 - 0.08 * i)
        c.cone(x, top, b, half * s, leaf)


# ------------------------------------------------------------------- dead trees

DEAD = pal((60, 50, 70), (128, 116, 130), (196, 186, 190), (240, 236, 232))


def dead_frame(level):
    """Bare silvered snags; depleting removes branches, then the trunk."""
    c = Canvas(40, 44, zoom=1.15)
    rng = np.random.default_rng(303 + level)
    wood = (150, 136, 146)
    dark = (86, 74, 90)
    if level >= 1:  # fallen log, always present
        c.line((9, 36), (22, 38), 2.6, dark)
        c.line((9, 35.5), (22, 37.5), 1.8, wood)
        c.blob(9, 35.7, 2.3, 2.5, DEAD)
    if level >= 2:  # broken stump
        c.line((28, 37), (28.5, 29), 2.8, dark, 2.4)
        c.line((27.6, 37), (28.1, 29), 1.8, wood, 1.4)
        c.poly([(26.6, 29.5), (28.2, 26.5), (29.2, 28.4), (30.4, 27.5), (30.4, 29.5)], wood)
    if level >= 3:  # full snag with branches
        trunk = [(19, 34), (19.5, 22), (18.5, 12)]
        c.line(trunk[0], trunk[1], 3.0, dark, 2.4)
        c.line(trunk[1], trunk[2], 2.4, dark, 1.1)
        c.line((18.4, 34), (18.9, 22), 2.0, wood, 1.6)
        c.line((18.9, 22), (18.1, 12.5), 1.5, wood, 0.6)
        for (sx, sy), (ex, ey) in [((19.3, 24), (12, 17)), ((19.2, 20), (25, 13)), ((12.8, 18), (11, 13)),
                                   ((18.9, 16), (14, 11)), ((23.5, 14.5), (26.5, 10))]:
            c.line((sx, sy), (ex + rng.uniform(-0.5, 0.5), ey), 1.4, dark, 0.55)
            c.line((sx - 0.3, sy - 0.2), (ex - 0.3, ey), 0.8, wood, 0.3)
    return c.finish(outline=(30, 24, 40), outline_k=0.45)


# ----------------------------------------------------------- scavenge sites

STONE = pal((52, 36, 82), (110, 92, 145), (172, 154, 202), (220, 210, 238))
PLANK = pal((70, 40, 30), (140, 92, 56), (196, 150, 98), (236, 206, 160))
RUST = pal((70, 30, 30), (150, 70, 52), (205, 128, 92), (240, 200, 170))
GOLD = pal((110, 70, 10), (220, 170, 40), (255, 228, 120), (255, 255, 230))


def block(c, x, y, w, h, stops, top_k=1.18):
    """A three-quarter-view cuboid: lit top face, front and right faces."""
    d = h * 0.35
    c.poly([(x, y), (x + w, y), (x + w + d * 0.6, y - d), (x + d * 0.6, y - d)], ramp(np.full((1, 1), 1.0), stops)[0, 0] * top_k)
    c.poly([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], ramp(np.full((1, 1), 0.72), stops)[0, 0])
    c.poly([(x + w, y), (x + w + d * 0.6, y - d), (x + w + d * 0.6, y + h - d), (x + w, y + h)], ramp(np.full((1, 1), 0.38), stops)[0, 0])


def ruins_frame(total):
    """Broken lavender walls, a toppled column, beams and a rusted plate."""
    c = Canvas(zoom=1.35)
    parts = [
        lambda: (block(c, 5, 9, 4, 13, STONE), block(c, 9, 13, 4, 9, STONE)),      # back wall
        lambda: c.line((14, 10), (25, 15), 1.6, PLANK[1][1]),                       # leaning beam
        lambda: block(c, 21, 15, 6, 7, STONE),                                     # wall stub
        lambda: (c.blob(9, 26, 5.5, 2.4, STONE), c.blob(15, 26.5, 2.4, 2.4, STONE)),  # toppled column
        lambda: c.poly([(19, 24), (26, 23), (27.5, 27.5), (20.5, 28.5)], RUST[1][1]),  # iron plate
        lambda: c.line((3, 29), (13, 30.5), 1.5, PLANK[2][1]),                      # plank
    ]
    for p in parts[:total]:
        p()
    return c.finish(outline=(36, 24, 54))


TENT = pal((120, 60, 90), (232, 150, 170), (252, 220, 220), (255, 250, 245))


def camp_frame(total):
    """A pink tent, a log pile, a cooking fire and provision sacks."""
    c = Canvas(zoom=1.15)
    steps = [
        lambda: (c.line((19, 27), (27, 28), 1.1, PLANK[1][1]), c.line((20, 25.5), (27.5, 26.5), 1.0, PLANK[2][1])),
        lambda: (c.blob(8, 25, 3.0, 2.6, pal((80, 60, 30), (180, 150, 90), (236, 214, 160))),
                 c.blob(12, 26.5, 2.6, 2.2, pal((80, 60, 30), (170, 140, 84), (230, 206, 150)))),
        lambda: (c.blob(16, 29, 3, 1.3, STONE), c.blob(16, 27.3, 1.6, 2.2, pal((170, 40, 30), (250, 140, 40), (255, 230, 120)), gloss=0.4)),
        lambda: c.line((22, 23), (28, 24), 1.0, PLANK[1][1]),
        lambda: (c.poly([(4, 21), (12, 6), (20, 21)], TENT[1][1]), c.poly([(12, 6), (20, 21), (24, 19), (16, 5.5)], TENT[0][1]),
                 c.poly([(10.5, 21), (12, 13), (13.5, 21)], (90, 40, 70))),
    ]
    order = [4, 2, 0, 1, 3]  # the tent goes last as stock runs out
    for i in sorted(order[:total], key=lambda k: [4, 0, 1, 2, 3].index(k)):
        steps[i]()
    return c.finish(outline=(40, 24, 50))


GLOW = pal((70, 30, 110), (150, 90, 220), (210, 180, 255), (255, 250, 255))


def debris_frame(total):
    """Fragments of an alien monolith in dark violet stone with gold seams."""
    c = Canvas(zoom=1.15)
    rng = np.random.default_rng(404)
    shards = [(15, 6, 21, 5.5, -6), (6, 15, 24, 4.0, 18), (23, 14, 25, 4.0, -20), (11, 22, 29, 4.5, 60), (22, 23, 29, 4.0, -70)]
    for i, (x, top, base, hw, tilt) in enumerate(shards[:total]):
        t = math.radians(tilt)
        pts = [(x - hw, base), (x + hw, base), (x + hw * 0.7 + math.sin(t) * (base - top), top + 1),
               (x + math.sin(t) * (base - top), top - 0.5), (x - hw * 0.7 + math.sin(t) * (base - top), top + 1.5)]
        c.poly(pts, (58, 40, 96))
        c.poly([pts[0], pts[4], pts[3], ((pts[0][0] + pts[1][0]) / 2, base)], (110, 86, 160))
        mx = (pts[0][0] + pts[3][0]) / 2
        c.line((mx + rng.uniform(-1, 1), base - 1), (mx + math.sin(t) * (base - top) * 0.5, (top + base) / 2), 0.7, GOLD[2][1])
        c.blob(mx + 1, (top + base) / 2 + 2, 1.0, 1.0, GLOW if i % 2 else GOLD, gloss=0.6)
    return c.finish(outline=(26, 16, 44))


# ------------------------------------------------------------- undergrowth

SCRUB = [[(0.0, (26, 52, 20)), (0.5, (64, 116, 40)), (0.9, (128, 170, 60)), (1.3, (210, 230, 140))],
         [(0.0, (40, 60, 34)), (0.5, (96, 120, 70)), (0.9, (150, 170, 110)), (1.3, (220, 230, 190))]]


def scrub_frame(level):
    c = Canvas()
    rng = np.random.default_rng(505 + level)
    spots = [(16, 17), (8, 10), (24, 9), (8, 25), (24, 24)][: 3 + 2 * (level - 1)]
    for x, y in sorted(spots, key=lambda p: p[1]):
        stops = SCRUB[1] if rng.random() < 0.35 else SCRUB[0]
        for _ in range(2):
            c.line((x + rng.uniform(-1, 1), y + 3), (x + rng.uniform(-5, 5), y - rng.uniform(3, 5)), 0.6, (110, 70, 60), 0.25)
        for _ in range(6):
            c.blob(x + rng.uniform(-3.4, 3.4), y + rng.uniform(-2.8, 1.5), rng.uniform(2.6, 3.4), rng.uniform(2.0, 2.7), stops)
        for _ in range(2):
            c.blob(x + rng.uniform(-3, 3), y + rng.uniform(-2.5, 0.5), 0.7, 0.7, pal((140, 40, 40), (230, 90, 80), (255, 190, 170)), gloss=0.4)
    return c.finish(outline=(30, 40, 30), outline_k=0.4, shadow=0.3)


def tall_grass_frame():
    c = Canvas()
    rng = np.random.default_rng(606)
    cols = [(150, 200, 110), (196, 220, 130), (230, 210, 150), (200, 170, 210)]
    cols = [(110, 190, 60), (170, 214, 80), (226, 214, 120), (196, 150, 220)]
    pts = sorted([(rng.uniform(3, 29), rng.uniform(10, 30)) for _ in range(34)], key=lambda p: p[1])
    for x, y in pts:
        h = rng.uniform(8, 13)
        bend = rng.uniform(-3.5, 3.5)
        col = cols[rng.integers(1, len(cols))] if rng.random() < 0.3 else cols[0]
        c.line((x, y), (x + bend, y - h), 0.85, shade(col, 0.65 + 0.4 * (y / 30)), 0.15)
        if rng.random() < 0.35:
            c.blob(x + bend, y - h + 0.6, 0.8, 1.7, pal(shade(col, 0.5), col, shade(col, 1.25)))
    return c.finish(outline=(40, 50, 30), outline_k=0.3, shadow=0.2)


# -------------------------------------------------------------------- crops

def maize_frame(level):
    """Sprout rows that grow into tall stalks with yellow cobs and tassels."""
    c = Canvas()
    rng = np.random.default_rng(707 + level)
    h = [5, 9, 13, 16][level - 1]
    for x, y in sorted([(7, 14), (17, 13), (26, 14), (5, 23), (15, 22), (25, 23), (10, 31), (21, 31)], key=lambda p: p[1]):
        x += rng.uniform(-0.6, 0.6)
        c.line((x, y), (x, y - h), 1.0, (60, 140, 50), 0.6)
        for k in range(max(1, h // 3)):
            yy = y - 1.0 - k * 3.0
            side = 1 if k % 2 else -1
            c.line((x, yy), (x + side * 3.4, yy - 1.8), 0.85, (120, 200, 70), 0.2)
        if level >= 3:
            c.blob(x + 1.2, y - h * 0.5, 1.3, 2.6, pal((150, 100, 10), (245, 200, 50), (255, 240, 150)), gloss=0.2)
        if level >= 4:
            c.line((x, y - h), (x + 1.2, y - h - 2.5), 0.5, (240, 200, 120), 0.15)
            c.line((x, y - h), (x - 1.2, y - h - 2.3), 0.5, (240, 200, 120), 0.15)
    return c.finish(outline=(30, 50, 20), outline_k=0.35, shadow=0.25)


def potato_frame(level):
    """Low leafy mounds; flowers appear, then tubers show at the soil line."""
    c = Canvas()
    rng = np.random.default_rng(808 + level)
    r = [2.4, 3.2, 3.9, 4.4, 4.8][level - 1]
    for x, y in sorted([(8, 9), (22, 9), (15, 18), (7, 26), (23, 26)], key=lambda p: p[1]):
        x += rng.uniform(-0.8, 0.8)
        if level >= 5:
            for k in range(2):
                c.blob(x - 3 + 5 * k, y + 2.6, 1.8, 1.4, pal((90, 50, 30), (190, 124, 70), (240, 196, 136)), gloss=0.15)
        for k in range(6):
            c.blob(x + rng.uniform(-r * 0.6, r * 0.6), y + rng.uniform(-r * 0.4, r * 0.2), r * 0.55, r * 0.45,
                   [(0.0, (14, 54, 30)), (0.5, (36, 120, 56)), (0.9, (90, 180, 80)), (1.3, (180, 236, 150))])
        if level >= 3:
            for k in range(3):
                c.blob(x + rng.uniform(-r * 0.6, r * 0.6), y - r * 0.4 + rng.uniform(-0.8, 0.5), 1.0, 1.0,
                       pal((110, 60, 140), (200, 150, 240), (250, 240, 255)), gloss=0.3)
    return c.finish(outline=(20, 40, 24), outline_k=0.4, shadow=0.3)


def rice_frame(level):
    """Bright green clumps whose heads ripen to drooping gold."""
    c = Canvas()
    rng = np.random.default_rng(909 + level)
    h = [5, 7, 9, 11, 12][level - 1]
    for x, y in sorted([(8, 13), (22, 12), (15, 21), (6, 30), (25, 30)], key=lambda p: p[1]):
        for k in range(8):
            bend = rng.uniform(-3.2, 3.2)
            tip = (x + bend, y - h * rng.uniform(0.8, 1.05))
            green = (70, 200, 90) if level < 4 else (160, 210, 80)
            c.line((x + rng.uniform(-0.8, 0.8), y), tip, 0.8, shade(green, rng.uniform(0.7, 1.1)), 0.15)
            if level >= 4 and k % 2 == 0:
                c.line(tip, (tip[0] + (2.0 if bend > 0 else -2.0), tip[1] + 2.4), 0.8, (245, 205, 90), 0.35)
    return c.finish(outline=(30, 60, 30), outline_k=0.3, shadow=0.25)


# --------------------------------------------------------------------- fish

KOI = [pal((150, 50, 20), (250, 140, 60), (255, 210, 150)), pal((140, 40, 90), (240, 120, 180), (255, 210, 235)),
       pal((40, 80, 150), (110, 180, 240), (220, 245, 255))]


FISH_FRAMES = 12
WATER_TINT = np.array([70, 110, 200.0])


def fish_frame(level, phase):
    """Koi-like fish seen through the water. Over FISH_FRAMES frames each fish
    swims one slow loop with its body flexing side to side, so the animation
    cycles seamlessly. The fish are tinted toward the water colour, softened and
    partly transparent, with no outline or shadow, so they read as submerged;
    small white glints sit on the surface above them."""
    c = Canvas(zoom=1.3)
    rng = np.random.default_rng(1001)
    homes = [(16, 16, 2.6), (8, 8, 2.0), (24, 24, 2.0), (24, 8, 2.0), (8, 24, 2.0)]
    starts = [rng.uniform(0, math.tau) for _ in homes]
    turn = [1, -1, 1, -1, 1]
    for i, (hx, hy, rad) in enumerate(homes[:level]):
        a = starts[i] + turn[i] * phase * math.tau / FISH_FRAMES
        x, y = hx + math.cos(a) * rad, hy + math.sin(a) * rad * 0.8
        heading = a + turn[i] * math.pi / 2
        ux, uy = math.cos(heading), math.sin(heading)
        px, py = -uy, ux
        stops = KOI[i % len(KOI)]
        wave = phase * math.tau * 2 / FISH_FRAMES + i
        # spine from head (k=0) to tail (k=1); the bend grows toward the tail
        def spine(k):
            bend = math.sin(wave - k * 2.4) * 0.8 * k
            return x + ux * (2.6 - 6.0 * k) + px * bend, y + uy * (2.6 - 6.0 * k) + py * bend
        tx, ty = spine(1.0)
        bx, by = spine(0.82)
        fx, fy = (tx - bx), (ty - by)
        n = math.hypot(fx, fy) or 1
        fx, fy = fx / n, fy / n
        c.poly([(bx, by), (tx + fx * 1.6 - fy * 2.0, ty + fy * 1.6 + fx * 2.0),
                (tx + fx * 0.6, ty + fy * 0.6), (tx + fx * 1.6 + fy * 2.0, ty + fy * 1.6 - fx * 2.0)], stops[2][1])
        for k, r in [(0.75, 1.0), (0.55, 1.5), (0.35, 1.9), (0.15, 2.0), (0.0, 1.6)]:
            sx, sy = spine(k)
            c.blob(sx, sy, r, r, stops)
    img = c.finish(outline_k=0.0, shadow=0.0)
    # Submerge: soften, tint toward the water and make translucent.
    for ch in range(4):
        img[..., ch] = ndimage.gaussian_filter(img[..., ch], 0.3 * SS)
    img[..., :3] = img[..., :3] * 0.7 + WATER_TINT * 0.3
    img[..., 3] *= 0.72
    # Surface glints near each fish, twinkling with the phase.
    g = Canvas()
    glint = np.zeros_like(g.alpha)
    for i, (hx, hy, _) in enumerate(homes[:level]):
        for j in range(2):
            gx, gy = hx + rng.uniform(-5, 5), hy + rng.uniform(-5, 5)
            on = 0.5 + 0.5 * math.sin(phase * math.tau * 2 / FISH_FRAMES + i * 1.7 + j * 2.3)
            g.alpha[:] = 0
            g.line((gx - 1.2, gy), (gx + 1.2, gy - 0.3), 0.35, (255, 255, 255))
            glint = np.maximum(glint, g.alpha * (0.2 + 0.6 * on))
    a0 = img[..., 3]
    out_a = glint + a0 * (1 - glint)
    img[..., :3] = np.where(out_a[..., None] > 0, (255 * glint[..., None] + img[..., :3] * (a0 * (1 - glint))[..., None])
                            / np.maximum(out_a[..., None], 1e-6), 0)
    img[..., 3] = out_a
    return img


# ---------------------------------------------------------------- catalog

def _levels(fn, n):
    return [fn(i + 1) for i in range(n)]


# key -> (frame size, list of frame builders). Frame index i is the file suffix.
FRAMES = {
    "jungle-trees": [lambda l=l, v=v: tree_frame(l, v, "jungle") for v in (0, 1) for l in range(1, 6)],
    "pine-trees": [lambda l=l, v=v: tree_frame(l, v, "pine") for v in (0, 1) for l in range(1, 6)],
    "dead-trees": [lambda l=l: dead_frame(l) for l in range(1, 4)],
    "ruins": [lambda t=t: ruins_frame(t) for t in range(1, 7)],
    "camp-site": [lambda t=t: camp_frame(t) for t in range(1, 6)],
    "ancient-debris": [lambda t=t: debris_frame(t) for t in range(1, 6)],
    "scrub": [lambda l=l: scrub_frame(l) for l in range(1, 3)],
    "tall-grass": [tall_grass_frame],
    "maize": [lambda l=l: maize_frame(l) for l in range(1, 5)],
    "potatoes": [lambda l=l: potato_frame(l) for l in range(1, 6)],
    "rice": [lambda l=l: rice_frame(l) for l in range(1, 6)],
    "fish": [lambda l=l, p=p: fish_frame(l, p) for l in range(1, 6) for p in range(FISH_FRAMES)],
}


def downsample(img, w, h):
    fy, fx = img.shape[0] // h, img.shape[1] // w
    pm = img.copy()
    pm[..., :3] *= pm[..., 3:4]
    pm = pm.reshape(h, fy, w, fx, 4).mean(axis=(1, 3))
    a = pm[..., 3:4]
    pm[..., :3] = np.where(a > 1e-4, pm[..., :3] / np.maximum(a, 1e-4), 0)
    return pm


def to_image(img):
    out = img.copy()
    out[..., 3] = np.clip(out[..., 3], 0, 1) * 255
    out = np.clip(np.round(out), 0, 255).astype(np.uint8)
    out[out[..., 3] < 6] = 0
    return Image.fromarray(out, "RGBA")


def render(build):
    """Paint one frame and return (classic, HD 4x) PIL images."""
    img = build()
    h, w = img.shape[0] // SS, img.shape[1] // SS
    classic = downsample(img, w, h)
    blur = ndimage.gaussian_filter(classic[..., :3], (0.7, 0.7, 0))
    classic[..., :3] += (classic[..., :3] - blur) * 0.3
    return to_image(classic), to_image(downsample(img, w * 4, h * 4))


def sheet(rendered, ground, scale=3):
    """Preview: each resource's frames in a row, every frame on a ground tile."""
    tile = Image.open(ROOT / f"data/gfx/{ground}.png").convert("RGBA")
    cell = 48
    width = max(len(f) for f in rendered.values()) * cell + 8
    out = Image.new("RGBA", (width, len(rendered) * cell + 8), (30, 30, 30, 255))
    for row, frames in enumerate(rendered.values()):
        for col, (classic, _) in enumerate(frames):
            patch = Image.new("RGBA", (cell, cell))
            for y in range(0, cell, 32):
                for x in range(0, cell, 32):
                    patch.alpha_composite(tile, (x, y))
            patch.alpha_composite(classic, ((cell - classic.width) // 2, (cell - classic.height) // 2))
            out.alpha_composite(patch, (4 + col * cell, 4 + row * cell))
    return out.resize((out.width * scale, out.height * scale), Image.NEAREST)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*", help=f"subset of {', '.join(FRAMES)}")
    parser.add_argument("--sheet", type=Path, help="write a preview sheet instead of the frames")
    parser.add_argument("--ground", default="terrain0", help="terrain frame under the preview")
    args = parser.parse_args(argv)
    names = args.names or list(FRAMES)
    unknown = set(names) - set(FRAMES)
    if unknown:
        parser.error(f"unknown resource {', '.join(sorted(unknown))}")
    rendered = {name: [render(b) for b in FRAMES[name]] for name in names}
    if args.sheet:
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet(rendered, args.ground).save(args.sheet)
        print(f"Wrote {args.sheet}")
        return
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from highres_pack import register_frames, source_record

    hd_frames = []
    for name, frames in rendered.items():
        for i, (classic, hd) in enumerate(frames):
            classic.save(ROOT / f"data/gfx/resource-{name}{i}.png", optimize=True)
            hd_frames.append({"id": f"resource-{name}{i}", "image": hd, "recipe": f"{HD_RECIPE}: {name}",
                              "sources": [source_record(Path(__file__).resolve(), ROOT)]})
    register_frames(hd_frames, HD_CATEGORY)
    print(f"Wrote {len(hd_frames)} landscape resource frames with HD frames")


if __name__ == "__main__":
    main()
