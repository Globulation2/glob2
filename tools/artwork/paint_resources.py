#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Paint the foundation resource sprites: gold ore, iron ore, silica, cotton.

Each sprite is one 32x32 frame (`data/gfx/resource-<name>0.png`) with a 128x128
HD frame registered through tools/artwork/highres_pack.py. Frames are painted at
16x supersampling and box-filtered down, so each object fills its cell like the
stock rocks instead of floating as a small pile in a padded square.

Ore is a cluster of faceted boulders shaded as a height field with top-left
light, crevices where boulders meet, a soft rim and a contact shadow to the
lower right. Gold puts thick bright veins and loose nuggets on the lavender
stock-rock stone; iron uses darker stone, rust-red strata and dark metallic
hematite chunks; silica is white-blue quartz points on a pale bed, a spiky
silhouette that stays distinct from sand ground; cotton is a leafy bush with
fluffy white bolls in brown star-shaped husks.

Painting is deterministic: every random choice comes from a fixed per-resource
seed, so rerunning the tool unchanged reproduces the committed pixels. Edit the
layout tables and palettes in each `paint_*` function, preview, then write:

    python3 tools/artwork/paint_resources.py --sheet artifacts/resources.png
    python3 tools/artwork/paint_resources.py --sheet artifacts/dirt.png --ground terrain-dirt0
    python3 tools/artwork/paint_resources.py                 # write frames + HD
    python3 tools/artwork/package_runtime.py --check

Needs NumPy, SciPy and Pillow (not the pinned encoder interpreter). Writing
replaces `data/gfx/resource-<name>0.png` and upserts the HD frame in both pack
copies; the resource registry and simulation are untouched.
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]

SS = 16           # supersamples per logical (classic) pixel
N = 32 * SS       # working canvas size for one 32x32 frame
HD_CATEGORY = "procedural-materials"
HD_RECIPE = "procedural resource painter v1"  # package_runtime.py classifies by this prefix

# Top-left light shared with the stock sprites; y grows downward on screen.
LIGHT = np.array([-0.55, -0.75, 0.9])
LIGHT = LIGHT / np.linalg.norm(LIGHT)

# Per-pixel logical coordinates; all layouts below are in classic pixels (0-32).
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / SS


def fbm(seed, scale, octaves=4):
    """Smooth value noise in [0, 1]; `scale` is the base feature size in classic pixels."""
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N), np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = max(2, int(32 / scale * 2 ** o))
        g = rng.random((cells + 1, cells + 1)).astype(np.float32)
        up = ndimage.zoom(g, N / (cells + 1), order=3)[:N, :N]
        out += amp * up
        total += amp
        amp *= 0.5
    out /= total
    return (out - out.min()) / (np.ptp(out) + 1e-6)


def ramp(t, stops):
    """Piecewise-linear colour ramp; stops = [(pos, (r,g,b)), ...]."""
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        m = (t >= p0) & (t <= p1)
        f = ((t - p0) / max(p1 - p0, 1e-6))[..., None]
        out[m] = (np.array(c0) * (1 - f) + np.array(c1) * f)[m]
    return out


class Layer:
    """Height-field scene: union of faceted boulders, with per-pixel owner id.

    `parts[id]` holds each boulder's material tag so painters can colour whole
    boulders (nuggets, hematite) differently from the stone around them.
    """

    def __init__(self):
        self.h = np.full((N, N), -1.0, np.float32)
        self.id = np.full((N, N), -1, np.int32)
        self.parts = []

    def boulder(self, cx, cy, r, height, facets, rng, squash=0.85, top=0.8, mat=0):
        """Add a rock: the intersection of `facets` jittered sloped planes and a
        flat top, i.e. a convex faceted dome. `squash` < 1 flattens it vertically
        on screen for the three-quarter view."""
        dx, dy = xx - cx, (yy - cy) / squash
        base = rng.random() * math.tau
        h = np.full((N, N), np.inf, np.float32)
        for i in range(facets):
            a = base + i * math.tau / facets + rng.normal(0, 0.25)
            rr = r * rng.uniform(0.82, 1.12)
            d = dx * math.cos(a) + dy * math.sin(a)
            h = np.minimum(h, height * (1 - d / rr))
        h = np.minimum(h, height * top + (dx * -0.12 + dy * -0.10))  # tilted top facet
        pid = len(self.parts)
        self.parts.append(mat)
        win = (h > 0) & (h > self.h)
        self.h[win] = h[win]
        self.id[win] = pid
        return pid

    def normals(self):
        """Surface normals of the height field, lightly blurred so facet edges
        antialias; the 0.75 factor softens slopes toward the stock rocks' look."""
        h = np.where(self.id >= 0, self.h, 0)
        h = ndimage.gaussian_filter(h, 0.6 * SS / 4)
        gy, gx = np.gradient(h)
        gy, gx = gy * SS, gx * SS
        n = np.dstack([-gx * 0.75, -gy * 0.75, np.ones_like(h)])
        n /= np.linalg.norm(n, axis=2, keepdims=True)
        return n


def lambert(n, amb=0.38, k=0.75):
    """Diffuse brightness; values above 1 feed each palette's highlight stop."""
    return np.clip(amb + k * (n @ LIGHT), 0, 1.3)


def spec(n, power=24):
    """Blinn-Phong highlight for the metallic materials (gold, hematite)."""
    v = np.array([0, 0, 1.0])
    hv = LIGHT + v
    hv /= np.linalg.norm(hv)
    return np.clip(n @ hv, 0, 1) ** power


def seams(ids, width=0.55):
    """Dark crevice mask where two boulders touch."""
    edge = np.zeros(ids.shape, bool)
    for sx, sy in ((1, 0), (0, 1), (1, 1), (-1, 1)):
        o = np.roll(np.roll(ids, sx, 1), sy, 0)
        edge |= (o != ids) & (o >= 0) & (ids >= 0)
    d = ndimage.distance_transform_edt(~edge) / SS
    return np.clip(1 - d / width, 0, 1)


def rim(alpha, width=0.9):
    """Distance from silhouette inward (logical px), for a soft outline."""
    d = ndimage.distance_transform_edt(alpha > 0.5) / SS
    return np.clip(1 - d / width, 0, 1)


def shadow(alpha, dx=1.6, dy=1.3, blur=1.1, strength=0.42):
    """Soft contact shadow cast to the lower right, opposite the light."""
    s = ndimage.shift(alpha, (dy * SS, dx * SS), order=1)
    s = ndimage.gaussian_filter(s, blur * SS)
    return s * strength


def compose(rgb, alpha, shadow_strength=0.42, outline=(40, 22, 60), outline_k=0.55):
    """Finish a painted object: darken its silhouette rim toward `outline` so it
    separates from any ground, then add the contact shadow underneath. Returns
    straight (non-premultiplied) RGBA with alpha in [0, 1]."""
    r = rim(alpha)[..., None]
    rgb = rgb * (1 - r * outline_k) + np.array(outline, np.float32) * r * outline_k
    sh = shadow(alpha, strength=shadow_strength) * (1 - alpha)
    a = alpha + sh
    col = np.where(a[..., None] > 0, (rgb * alpha[..., None]) / np.maximum(a[..., None], 1e-6), 0)
    return np.dstack([col, a])


def downsample(img, size):
    """Premultiplied box filter to size x size."""
    f = img.shape[0] // size
    pm = img.copy()
    pm[..., :3] *= pm[..., 3:4]
    pm = pm.reshape(size, f, size, f, 4).mean(axis=(1, 3))
    a = pm[..., 3:4]
    pm[..., :3] = np.where(a > 1e-4, pm[..., :3] / np.maximum(a, 1e-4), 0)
    return pm


def sharpen(img, amount=0.35):
    """Mild unsharp mask for the 32px frame, which the 16x box filter softens."""
    rgb = img[..., :3]
    blur = ndimage.gaussian_filter(rgb, (0.7, 0.7, 0))
    img = img.copy()
    img[..., :3] = rgb + (rgb - blur) * amount
    return img


def to_image(img):
    """Quantise to 8-bit RGBA and clear near-invisible fringe pixels."""
    out = img.copy()
    out[..., 3] *= 255
    out = np.clip(np.round(out), 0, 255).astype(np.uint8)
    out[out[..., 3] < 6] = 0
    return Image.fromarray(out, "RGBA")


# --------------------------------------------------------------------------- ore

# Palette stops are (brightness, rgb). STONE matches the stock rocks' lavender.
STONE = [(0.0, (52, 36, 82)), (0.35, (98, 80, 130)), (0.65, (145, 125, 175)), (0.9, (182, 164, 210)), (1.1, (215, 202, 235))]


def ore_cluster(rng, layout):
    """Build a Layer from (cx, cy, radius, height, facets, material) rows."""
    L = Layer()
    for cx, cy, r, ht, f, mat in layout:
        L.boulder(cx, cy, r, ht, f, rng, mat=mat)
    return L


def paint_gold(seed=7):
    rng = np.random.default_rng(seed)
    # (cx, cy, radius, height, facets, material: 0 stone, 1 gold nugget)
    layout = [
        (12.5, 13.0, 8.2, 9.0, 7, 0),
        (21.0, 15.5, 7.0, 7.5, 6, 0),
        (9.0, 21.5, 6.0, 6.0, 6, 0),
        (17.5, 23.0, 6.6, 6.8, 7, 0),
        (25.5, 22.5, 3.6, 3.6, 5, 0),
        (13.0, 27.0, 3.0, 3.6, 5, 1),
        (23.0, 27.5, 2.6, 3.2, 5, 1),
        (4.5, 15.5, 2.6, 3.0, 5, 1),
        (27.5, 12.0, 2.3, 2.8, 5, 1),
    ]
    L = ore_cluster(rng, layout)
    alpha = (L.id >= 0).astype(np.float32)
    n = L.normals()
    lit = lambert(n)
    mats = np.array(L.parts)[np.maximum(L.id, 0)]
    stone = ramp(lit, STONE)
    # Gold covers stone two ways: veins along the mid-level contour of one noise
    # field (width varied by a second), and larger patches where a third peaks.
    v = fbm(seed + 1, 7, 4)
    w = fbm(seed + 2, 4, 3)
    vein = np.clip((1 - np.abs(v - 0.5) / (0.05 + 0.07 * w)) * 2.5, 0, 1)
    blobs = np.clip((fbm(seed + 3, 3.0, 3) - 0.58) * 10, 0, 1)
    gold_mask = np.clip(np.maximum(vein, blobs), 0, 1) * (mats == 0)
    gold_mask = np.maximum(gold_mask, (mats == 1).astype(np.float32))
    GOLD = [(0.0, (120, 66, 6)), (0.35, (204, 128, 12)), (0.65, (250, 190, 24)), (0.9, (255, 222, 80)), (1.15, (255, 246, 190))]
    gold = ramp(lit * 1.05, GOLD) + spec(n, 18)[..., None] * 120
    rgb = stone * (1 - gold_mask[..., None]) + gold * gold_mask[..., None]
    s = seams(L.id)[..., None]
    rgb = rgb * (1 - 0.55 * s)
    # A few star glints on fully gold pixels sell "metal" at small sizes.
    rgb = sparkle(rgb, alpha * gold_mask, rng, count=4, size=1.6)
    return compose(np.clip(rgb, 0, 255), alpha)


def sparkle(rgb, where, rng, count, size):
    """Paint `count` four-point glints centred on random pixels of `where`."""
    ys, xs = np.nonzero(where > 0.9)
    if not len(ys):
        return rgb
    pick = rng.choice(len(ys), count, replace=False)
    for i in pick:
        cy, cx = ys[i] / SS, xs[i] / SS
        d = np.abs(xx - cx) * np.abs(yy - cy)
        cross = np.clip(1 - d / (0.06 * size), 0, 1) * np.clip(1 - np.hypot(xx - cx, yy - cy) / size, 0, 1)
        rgb = rgb * (1 - cross[..., None]) + np.array([255, 255, 240]) * cross[..., None]
    return rgb


def paint_iron(seed=11):
    rng = np.random.default_rng(seed)
    # angular slabs (fewer facets, flatter tops); material 2 = hematite chunk
    layout = [
        (12.0, 12.5, 8.4, 8.0, 5, 0),
        (21.5, 14.0, 7.2, 7.0, 5, 2),
        (8.5, 21.5, 6.2, 5.6, 5, 2),
        (18.0, 22.5, 7.0, 6.4, 6, 0),
        (26.0, 22.0, 3.8, 3.6, 4, 0),
        (12.5, 27.0, 2.5, 2.6, 4, 2),
        (24.0, 27.5, 2.0, 2.2, 4, 0),
        (4.5, 14.5, 2.0, 2.2, 4, 2),
    ]
    L = ore_cluster(rng, layout)
    alpha = (L.id >= 0).astype(np.float32)
    n = L.normals()
    lit = lambert(n)
    mats = np.array(L.parts)[np.maximum(L.id, 0)]
    IRON_STONE = [(0.0, (40, 30, 44)), (0.35, (80, 66, 84)), (0.65, (118, 104, 120)), (0.9, (156, 142, 156)), (1.1, (192, 182, 194))]
    stone = ramp(lit, IRON_STONE)
    RUST = [(0.0, (62, 18, 12)), (0.35, (122, 38, 22)), (0.65, (176, 64, 34)), (0.9, (212, 104, 58)), (1.1, (238, 156, 110))]
    rust = ramp(lit, RUST)
    HEM = [(0.0, (22, 20, 30)), (0.35, (48, 46, 60)), (0.65, (78, 78, 96)), (0.9, (110, 112, 132)), (1.1, (150, 155, 175))]
    hem = ramp(lit, HEM) + spec(n, 30)[..., None] * np.array([150, 170, 210])
    # Rust follows tilted sedimentary bands warped by noise, then thresholded so
    # only the strongest bands show; it is fainter on the metallic chunks.
    band = fbm(seed + 4, 5, 4)
    strata = 0.5 + 0.5 * np.sin((yy * 0.9 + xx * 0.35) * 1.3 + band * 7)
    rust_mask = np.clip((strata * 0.6 + band * 0.7 - 0.74) * 5, 0, 1)
    on_hem = (mats == 2)
    rgb = np.where(on_hem[..., None], hem, stone)
    rmask = rust_mask * np.where(on_hem, 0.55, 1.0)
    rgb = rgb * (1 - rmask[..., None]) + rust * rmask[..., None]
    s = seams(L.id)[..., None]
    rgb = rgb * (1 - 0.6 * s)
    return compose(np.clip(rgb, 0, 255), alpha, outline=(36, 18, 22))


# ------------------------------------------------------------------------ silica

def poly(pts):
    """Filled polygon mask from classic-pixel points."""
    img = Image.new("L", (N, N), 0)
    ImageDraw.Draw(img).polygon([(x * SS, y * SS) for x, y in pts], fill=255)
    return np.asarray(img, np.float32) / 255


def paint_silica(seed=5):
    rng = np.random.default_rng(seed)
    # a low pale stone bed with quartz points rising from it
    L = Layer()
    for cx, cy, r, ht, f in [(15.5, 22.5, 10.5, 4.5, 7), (8.0, 24.5, 5.0, 3.0, 6), (24.5, 24.0, 5.5, 3.2, 6)]:
        L.boulder(cx, cy, r, ht, f, rng, squash=0.55)
    alpha = (L.id >= 0).astype(np.float32)
    n = L.normals()
    BED = [(0.0, (88, 78, 92)), (0.35, (140, 130, 140)), (0.65, (190, 182, 186)), (0.9, (222, 216, 214)), (1.1, (240, 236, 232))]
    rgb = ramp(lambert(n), BED)
    rgb *= (1 - 0.45 * seams(L.id)[..., None])
    # Crystals: (base x, base y, length, width, lean angle deg), back to front.
    # Each is a prism drawn as four flat faces: lit left side, shaded right side
    # and a two-faced pyramidal tip, with a white ridge line between the sides.
    crystals = [
        (13.0, 19.5, 15.5, 4.6, -14),
        (19.0, 19.0, 13.0, 4.2, 16),
        (9.5, 21.5, 9.5, 3.6, -34),
        (23.5, 21.0, 9.0, 3.4, 34),
        (16.0, 22.0, 17.5, 5.2, 2),
        (11.0, 25.0, 6.5, 3.0, -55),
        (21.5, 25.5, 6.0, 2.9, 50),
        (16.5, 26.5, 5.0, 2.8, 12),
    ]
    LIGHT_FACE = np.array([236, 246, 255.0])
    MID_FACE = np.array([178, 210, 236.0])
    DARK_FACE = np.array([112, 142, 186.0])
    TIP = np.array([255, 255, 255.0])
    for bx, by, length, wdt, lean in crystals:
        a = math.radians(lean)
        ux, uy = math.sin(a), -math.cos(a)      # axis (up the crystal)
        px, py = math.cos(a), math.sin(a)       # perpendicular (to the right)
        hw = wdt / 2
        body = length * 0.72
        L0 = (bx - px * hw, by - py * hw)
        R0 = (bx + px * hw, by + py * hw)
        L1 = (L0[0] + ux * body, L0[1] + uy * body)
        R1 = (R0[0] + ux * body, R0[1] + uy * body)
        C0 = (bx + px * hw * 0.15, by + py * hw * 0.15)
        C1 = (C0[0] + ux * body, C0[1] + uy * body)
        T = (bx + ux * length, by + uy * length)
        faces = [
            ([L0, C0, C1, L1], LIGHT_FACE),
            ([C0, R0, R1, C1], DARK_FACE),
            ([L1, C1, T], TIP),
            ([C1, R1, T], MID_FACE),
        ]
        for pts, col in faces:
            m = poly(pts)
            # subtle vertical gradient: darker toward the base, like translucent quartz
            t = np.clip(((xx - bx) * ux + (yy - by) * uy) / length, 0, 1)
            shade = col * (0.78 + 0.22 * t[..., None])
            rgb = rgb * (1 - m[..., None]) + shade * m[..., None]
            alpha = np.maximum(alpha, m)
        # bright edge line along the lit ridge
        ridge = line_mask(C0, C1, 0.28)
        rgb = rgb * (1 - ridge[..., None] * 0.7) + TIP * ridge[..., None] * 0.7
    return compose(np.clip(rgb, 0, 255), alpha, outline=(40, 46, 78), outline_k=0.6)


def line_mask(p0, p1, w):
    """Antialiased segment mask of half-width `w` (classic pixels)."""
    x0, y0 = p0
    x1, y1 = p1
    dx, dy = x1 - x0, y1 - y0
    t = np.clip(((xx - x0) * dx + (yy - y0) * dy) / (dx * dx + dy * dy), 0, 1)
    d = np.hypot(xx - (x0 + t * dx), yy - (y0 + t * dy))
    return np.clip(1 - d / w, 0, 1)


# ------------------------------------------------------------------------ cotton

def paint_cotton(seed=3):
    rng = np.random.default_rng(seed)
    rgb = np.zeros((N, N, 3), np.float32)
    alpha = np.zeros((N, N), np.float32)

    # Painter's algorithm: stems, then leaves, then bolls on top.
    def put(mask, col):
        nonlocal rgb, alpha
        rgb = rgb * (1 - mask[..., None]) + col * mask[..., None]
        alpha = np.maximum(alpha, mask)

    # Woody stems from a common base to each boll: (cx, cy, radius).
    base = (16.0, 29.0)
    bolls = [(10.0, 9.0, 4.3), (21.0, 8.0, 4.5), (24.5, 17.5, 4.0), (7.0, 18.5, 3.9), (15.5, 15.0, 4.7), (17.0, 23.5, 3.7)]
    STEM = np.array([96, 62, 36.0])
    for bx, by, r in bolls:
        mid = ((base[0] + bx) / 2 + rng.uniform(-1.5, 1.5), (base[1] + by) / 2 + 1)
        put(line_mask(base, mid, 0.75) * 1.0, STEM)
        put(line_mask(mid, (bx, by + r * 0.4), 0.6), STEM * 1.1)
    # Leaves: (cx, cy, half-length, rotation) ovals with a midrib, mostly on the
    # bush's outline so green frames the white bolls.
    LEAF_D = np.array([34, 78, 30.0])
    LEAF_L = np.array([104, 160, 62.0])
    leaves = [(15.5, 5.0, 4.0, 1.4), (27.5, 10.5, 4.0, 0.7), (3.5, 11.5, 4.0, -0.7), (28.5, 23.0, 3.8, 0.9),
              (10.5, 23.5, 4.2, -0.4), (22.5, 27.5, 3.8, 0.3), (4.0, 25.0, 3.8, -1.0), (12.0, 15.5, 3.2, -0.2), (21.5, 13.0, 3.2, 0.5)]
    for lx, ly, lr, rot in leaves:
        c, s = math.cos(rot), math.sin(rot)
        u = (xx - lx) * c + (yy - ly) * s
        v = -(xx - lx) * s + (yy - ly) * c
        m = np.clip((1 - np.hypot(u / lr, v / (lr * 0.62))) * SS * lr / 2.5, 0, 1)
        t = np.clip(0.5 - (u * 0.6 + v * 0.8) / (lr * 2.2), 0, 1)
        col = LEAF_D * (1 - t[..., None]) + LEAF_L * t[..., None]
        vein = np.clip(1 - np.abs(v) / 0.22, 0, 1) * (np.abs(u) < lr * 0.8)
        col = col * (1 - 0.35 * vein[..., None])
        put(m, col)
    # Bolls: a five-pointed brown husk (bract) behind four shaded white lobes.
    # The husk is what makes it read as cotton rather than a white flower.
    BRACT = np.array([132, 86, 46.0])
    BRACT_D = np.array([70, 42, 24.0])
    for bx, by, r in bolls:
        ang = np.arctan2(yy - by, xx - bx)
        rad = np.hypot(xx - bx, yy - by)
        star = r * (0.92 + 0.42 * np.clip(np.cos(5 * (ang + rng.uniform(0, 1))), 0, 1) ** 4)
        m = np.clip((star - rad) * SS / 2, 0, 1)
        t = np.clip(rad / (r * 1.3), 0, 1)
        put(m, BRACT_D * (1 - t[..., None]) + BRACT * t[..., None])
        lobes = []
        k = 4
        base_a = rng.uniform(0, math.tau)
        for i in range(k):
            a = base_a + i * math.tau / k + rng.normal(0, 0.15)
            lobes.append((bx + math.cos(a) * r * 0.36, by + math.sin(a) * r * 0.30 - r * 0.08, r * rng.uniform(0.50, 0.58)))
        lobes.sort(key=lambda q: q[1])  # back to front
        for lx, ly, lr in lobes:
            d = np.hypot(xx - lx, (yy - ly) / 0.92)
            m = np.clip((lr - d) * SS / 2, 0, 1)
            # soft sphere shading, cool shadows, warm-white light
            nz = np.sqrt(np.clip(1 - (d / lr) ** 2, 0, 1))
            nx = (xx - lx) / lr
            ny = (yy - ly) / lr
            sh = np.clip(0.42 + 0.62 * (-0.55 * nx - 0.75 * ny + 0.9 * nz) / 1.3, 0, 1.05)
            fluff = fbm(seed + int(lx * 10), 1.2, 3)
            sh = sh * (0.9 + 0.12 * fluff)
            col = ramp(sh, [(0.0, (128, 136, 168)), (0.45, (200, 204, 222)), (0.75, (240, 240, 246)), (1.05, (255, 255, 252))])
            put(m, col)
    return compose(np.clip(rgb, 0, 255), alpha, shadow_strength=0.38, outline=(40, 40, 52), outline_k=0.5)


PAINTERS = {"gold-ore": paint_gold, "iron-ore": paint_iron, "silica": paint_silica, "cotton": paint_cotton}


def render(name):
    """Paint `name` and return (classic 32px, HD 128px) PIL images."""
    img = PAINTERS[name]()
    hd = downsample(img, 128)
    classic = sharpen(downsample(img, 32), 0.3)
    classic[..., 3] = np.clip(classic[..., 3], 0, 1)
    return to_image(np.clip(classic, 0, 255)), to_image(np.clip(hd, 0, 255))


def sheet(frames, ground):
    """Preview: each sprite on a 3x3 patch of `ground`, classic frames at 3x."""
    tile = Image.open(ROOT / f"data/gfx/{ground}.png").convert("RGBA")
    cells = [(0, 0), (1, 0), (0, 1), (2, 1), (1, 2)]
    out = Image.new("RGBA", (len(frames) * 300, 300 + 140), (30, 30, 30, 255))
    for i, (name, (classic, hd)) in enumerate(frames.items()):
        patch = Image.new("RGBA", (96, 96))
        for y in range(3):
            for x in range(3):
                patch.alpha_composite(tile, (x * 32, y * 32))
        for x, y in cells:
            patch.alpha_composite(classic, (x * 32, y * 32))
        out.alpha_composite(patch.resize((288, 288), Image.NEAREST), (i * 300 + 6, 6))
        out.alpha_composite(hd, (i * 300 + 86, 306))
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*", help=f"subset of {', '.join(PAINTERS)}")
    parser.add_argument("--sheet", type=Path, help="write a preview sheet instead of the frames")
    parser.add_argument("--ground", default="terrain0", help="terrain frame under the preview")
    args = parser.parse_args(argv)
    names = args.names or list(PAINTERS)
    unknown = set(names) - set(PAINTERS)
    if unknown:
        parser.error(f"unknown resource {', '.join(sorted(unknown))}")
    frames = {name: render(name) for name in names}
    if args.sheet:
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet(frames, args.ground).save(args.sheet)
        print(f"Wrote {args.sheet}")
        return
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from highres_pack import register_frames, source_record

    hd_frames = []
    for name, (classic, hd) in frames.items():
        classic.save(ROOT / f"data/gfx/resource-{name}0.png", optimize=True)
        hd_frames.append({"id": f"resource-{name}0", "image": hd, "recipe": f"{HD_RECIPE}: {name}",
                          "sources": [source_record(Path(__file__).resolve(), ROOT)]})
    register_frames(hd_frames, HD_CATEGORY)
    print(f"Wrote {len(frames)} resource sprites with HD frames")


if __name__ == "__main__":
    main()
