# SPDX-License-Identifier: GPL-3.0-or-later
"""Object decor of the Outcrops looks drawn by terrain_decor.py.

Outcrops are ground strewn with separate obstacles about a cell across, drawn
one cluster per cell in screen-row order, like rock spires and basalt columns.
A cluster stays inside its own cell's footprint, so row order draws it
correctly; it may rise into the cell above, which is drawn earlier.

Each set is a height field in the game's three-quarter view (screen y = ground
y - height), lit from the upper left, with ray-marched shadows on itself and on
the ground. It renders at twice the HD frame size and is downsampled with
premultiplied alpha.
"""
import math

import numpy as np
from PIL import Image
from scipy import ndimage

R = 512                    # render size: a 64x64 native frame at 8x
CELL_R = 256               # the cell, centred in the frame
BASE_Y = R / 2 + CELL_R * 0.2
LIGHT = np.array([-0.62, -0.78, 0.9]) / np.linalg.norm([-0.62, -0.78, 0.9])
VIEW = np.array([0.0, 0.7071, 0.7071])


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def mix(c0, c1, t):
    t = np.asarray(t, float)[..., None]
    return np.asarray(c0, float) * (1 - t) + np.asarray(c1, float) * t


def value_noise(rng, shape, cells):
    h, w = shape
    cy, cx = max(1, round(cells * h / max(h, w))), max(1, round(cells * w / max(h, w)))
    grid = rng.random((cy + 4, cx + 4))
    ys, xs = 1 + np.arange(h) * cy / h, 1 + np.arange(w) * cx / w
    out = ndimage.map_coordinates(grid, np.meshgrid(ys, xs, indexing="ij"), order=3, mode="nearest")
    return np.clip((out - 0.5) * 1.2 + 0.5, 0, 1)


def fbm(rng, shape, cells, octaves=3):
    total, norm = np.zeros(shape), 0.0
    for o in range(octaves):
        total += 0.5 ** o * value_noise(rng, shape, cells * 2 ** o)
        norm += 0.5 ** o
    return total / norm


def grid():
    yy, xx = np.mgrid[0:R, 0:R].astype(float)
    return xx, yy


def rot(xx, yy, cx, cy, ang):
    c, s = math.cos(ang), math.sin(ang)
    dx, dy = xx - cx, yy - cy
    return dx * c + dy * s, -dx * s + dy * c


def spots(rng, count, rx, ry, gap):
    """Object centres inside the cell's footprint, spread apart."""
    out = []
    for _ in range(count * 30):
        if len(out) == count:
            break
        x, y = R / 2 + rng.uniform(-rx, rx), BASE_Y - 20 + rng.uniform(-ry, ry)
        if all(math.hypot(x - a, y - b) > gap for a, b in out):
            out.append((x, y))
    return out


def render(H, colour_fn, shadow_strength=0.45, ao=0.25, rim_light=0.15):
    """Draw height field H (ground coordinates, render pixels) in three-quarter view.

    Each ground row is painted back to front as a vertical span; colour_fn gets
    the source pixel, its height, normal, a wall flag and how shadowed it is."""
    Hs = ndimage.gaussian_filter(H, 1.0)
    gy, gx = np.gradient(Hs)
    n = np.stack([-gx, -gy, np.ones_like(H)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    yy, xx = np.mgrid[0:R, 0:R]
    dirxy = LIGHT[:2] / np.linalg.norm(LIGHT[:2])

    def shadowed(z0):
        hit = np.zeros(H.shape, bool)
        for t in range(2, 260, 2):
            sx = np.clip((xx + dirxy[0] * t).astype(int), 0, R - 1)
            sy = np.clip((yy + dirxy[1] * t).astype(int), 0, R - 1)
            hit |= H[sy, sx] > z0 + 1.25 * t
        return hit
    top_shadow = ndimage.gaussian_filter(shadowed(H + 0.5).astype(float), 1.5)
    ground_shadow = ndimage.gaussian_filter(shadowed(np.zeros_like(H)).astype(float), 4)
    cavity = np.clip((ndimage.gaussian_filter(H, 10) - H) / 20, 0, 1)

    src = np.full((R, R), -1, np.int64)
    zmap = np.zeros((R, R))
    cols = np.arange(R)
    for y in range(R):
        row = H[y]
        live = row > 0.5
        if not live.any():
            continue
        xs, hv = cols[live], row[live]
        for k in range(int(hv.max()) + 2):
            sy = (y - hv + k).astype(int)
            ok = (k <= hv + 0.5) & (sy >= 0) & (sy < R)
            src[sy[ok], xs[ok]] = y * R + xs[ok]
            zmap[sy[ok], xs[ok]] = np.maximum(hv[ok] - k, 0)
    painted = src >= 0
    sidx = src[painted]
    syy, sxx = sidx // R, sidx % R
    zmap = np.where(painted, ndimage.gaussian_filter(zmap, (0, 1.2)), zmap)
    z = zmap[painted]
    top = H[syy, sxx]
    # Steep sources are wall all the way up; otherwise each row's top pixel draws a stripe.
    is_wall = (z < top - 1.5) | (np.hypot(gx, gy)[syy, sxx] > 1.6)
    normal = n[syy, sxx].copy()
    wn = np.stack([-gx[syy, sxx], -gy[syy, sxx] + 0.4, np.zeros_like(z)], -1)
    wn /= np.linalg.norm(wn, axis=-1, keepdims=True) + 1e-9
    normal[is_wall] = wn[is_wall]
    sh = top_shadow[syy, sxx]
    ty = np.clip(syy + np.round(top - z).astype(int), 0, R - 1)
    rgb = colour_fn(ty, sxx, z, normal, is_wall, sh)
    lam = normal @ LIGHT
    value = np.where(is_wall, 0.6 + 0.28 * smooth(-0.6, 0.5, lam), 0.9 + 0.2 * np.clip(lam, 0, 1))
    wall_screen = np.zeros((R, R), bool)
    wall_screen[painted] = is_wall
    below = np.zeros((R, R), bool)
    below[:-2] = wall_screen[1:-1] | wall_screen[2:]
    rim = below[painted] & ~is_wall
    value = value * (1 + rim_light * rim * (1 - sh))
    value *= 1 - 0.1 * smooth(0, 1, 1 - z / 18.0) * is_wall
    value *= 1 - 0.3 * sh
    value *= 1 - min(ao, 0.25) * cavity[syy, sxx]
    rgb = rgb * value[:, None]

    img = np.zeros((R, R, 4))
    foot = ndimage.gaussian_filter((H > 0.5).astype(float), 6)
    img[..., :3] = np.array([12, 14, 10])
    img[..., 3] = np.clip(shadow_strength * ground_shadow + 0.15 * foot, 0, 0.6)
    img[painted, :3] = rgb
    img[painted, 3] = 1.0
    edge = np.minimum.reduce([xx, yy, R - 1 - xx, R - 1 - yy])
    img[..., 3] *= smooth(0, 24, edge)
    a = img[..., 3:4]
    pre = np.concatenate([img[..., :3] * a, a], -1).reshape(R // 2, 2, R // 2, 2, 4).mean((1, 3))
    a = pre[..., 3:4]
    out = np.concatenate([np.clip(np.where(a > 1e-6, pre[..., :3] / np.maximum(a, 1e-6), 0), 0, 255), a * 255], -1)
    return Image.fromarray(out.round().astype(np.uint8), "RGBA")


# --------------------------------------------------------------------------
# Penitentes: fields of sun-sculpted snow-ice blades, all facing one way.
# --------------------------------------------------------------------------

def penitentes(rng, edge):
    xx, yy = grid()
    H = np.zeros((R, R))
    n = rng.integers(3, 5) if edge else rng.integers(6, 9)
    for x, y in spots(rng, n, 60 if edge else 92, 40 if edge else 68, 44):
        length, width = rng.uniform(24, 34), rng.uniform(10, 14)
        hmax = rng.uniform(80, 110) if edge else rng.uniform(115, 175)
        u, v = rot(xx, yy, x, y, rng.uniform(-0.15, 0.15))
        prof = np.clip(1 - np.abs(u) / length, 0, 1) ** 1.3 * np.clip(1 - np.abs(v) / width, 0, 1) ** 0.7
        H = np.maximum(H, hmax * prof)
    H[H < 2] = 0
    tone = fbm(rng, (R, R), 6, 2)
    local_top = ndimage.maximum_filter(H, 24)

    def colour(sy, sx, z, nrm, wall, sh):
        depth = 1 - np.clip(z / np.maximum(local_top[sy, sx], 1), 0, 1)
        rgb = mix((252, 254, 255), (184, 216, 238), smooth(0.2, 1.0, depth) * 0.8) * 1.12
        rgb = rgb * (0.96 + 0.06 * tone[sy, sx])[:, None]
        rgb = mix(rgb, (150, 150, 156), smooth(8, 0, z) * 0.35)       # grit melted out at the foot
        refl = 2 * (nrm @ LIGHT)[:, None] * nrm - LIGHT
        return rgb + (np.clip(refl @ VIEW, 0, 1) ** 20 * (1 - sh))[:, None] * np.array([40, 40, 40])
    return render(H, colour, ao=0.2, rim_light=0.12)


# --------------------------------------------------------------------------
# Termite mounds: tall, fluted red-earth mounds with chimney turrets.
# --------------------------------------------------------------------------

def termite_mounds(rng, edge):
    xx, yy = grid()
    H = np.zeros((R, R))
    count = 1 if edge or rng.random() < 0.7 else 2
    for x, y in spots(rng, count, 50, 30, 120):
        rb = rng.uniform(62, 76) if edge else rng.uniform(84, 104) / (1 if count == 1 else 1.25)
        hmax = rng.uniform(80, 110) if edge else rng.uniform(130, 180) / (1 if count == 1 else 1.2)
        th = np.arctan2(yy - y, xx - x)
        flute = 1 + 0.07 * np.sin(9 * th + 3 * fbm(rng, (R, R), 4, 2))
        r = np.hypot(xx - x, (yy - y) * 1.25) / flute
        knob = 1 + 0.08 * (fbm(rng, (R, R), 14, 2) - 0.5)
        H = np.maximum(H, hmax * np.clip(1 - r / rb, 0, 1) ** 0.75 * knob)
        for _ in range(rng.integers(1, 4)):
            a = rng.uniform(-math.pi, 0)
            cx, cy = x + math.cos(a) * rb * 0.35, y + math.sin(a) * rb * 0.3
            rc, hc = rng.uniform(12, 22), hmax * rng.uniform(0.9, 1.25)
            rr = np.hypot(xx - cx, (yy - cy) * 1.25)
            H = np.maximum(H, hc * np.clip(1 - rr / rc, 0, 1) ** 0.5 * (rr < rc))
    H[H < 2] = 0
    grain, tone = value_noise(rng, (R, R), 70), fbm(rng, (R, R), 5, 2)

    def colour(sy, sx, z, nrm, wall, sh):
        rgb = mix((156, 88, 56), (198, 132, 82), tone[sy, sx])
        rgb = mix(rgb, (212, 160, 104), smooth(40, 150, z) * 0.45)      # sun-baked upper mound
        rgb = rgb * (0.9 + 0.18 * grain[sy, sx])[:, None]
        return mix(rgb, (120, 76, 50), smooth(10, 0, z) * 0.4)
    return render(H, colour, ao=0.25)


# --------------------------------------------------------------------------
# Hoodoos: banded pink-orange pillars, waisted by weathering, each wearing a
# darker caprock, on a talus apron.
# --------------------------------------------------------------------------

def hoodoos(rng, edge):
    xx, yy = grid()
    H = np.zeros((R, R))
    count = 1 if edge else rng.integers(2, 4)
    caps = np.zeros((R, R))
    radius = np.zeros((R, R))
    for x, y in spots(rng, count, 70, 45, 70):
        r0 = rng.uniform(28, 34) if edge else rng.uniform(32, 44)
        hmax = rng.uniform(95, 125) if edge else rng.uniform(125, 185)
        rr = np.hypot(xx - x, (yy - y) * 1.2)
        H = np.maximum(H, 26 * np.clip(1 - rr / (r0 * 2.0), 0, 1) ** 1.5)
        H = np.maximum(H, np.where(rr < r0 * (1 + 0.08 * (fbm(rng, (R, R), 10, 2) - 0.5)), hmax - 18, 0))
        cap = rr < r0 * rng.uniform(1.45, 1.7)
        H = np.where(cap, np.maximum(H, hmax), H)
        caps = np.where(cap, hmax, caps)
        radius = np.where(rr < r0 * 1.7, r0, radius)
    H = ndimage.gaussian_filter(H, 0.8) * (H > 1)
    H[H < 2] = 0
    warp = fbm(rng, (R, R), 4, 2)

    def colour(sy, sx, z, nrm, wall, sh):
        band = 0.5 + 0.5 * np.sin(z / 11.0 + 2 * warp[sy, sx])
        rgb = mix((226, 152, 106), (246, 202, 158), band) * 1.06
        # A weathered waist: the softer middle beds recede into shade.
        cap_z = np.maximum(caps[sy, sx], 1)
        waist = smooth(0.25, 0.5, z / cap_z) * smooth(0.8, 0.55, z / cap_z) * wall
        rgb = rgb * (1 - 0.22 * waist)[:, None]
        is_cap = (caps[sy, sx] > 0) & (z > caps[sy, sx] - 20)
        rgb = np.where(is_cap[:, None], mix((112, 92, 80), (136, 116, 100), band), rgb)
        return mix(rgb, (212, 172, 128), smooth(26, 4, z) * 0.6)
    return render(H, colour, ao=0.2)


# --------------------------------------------------------------------------
# Petrified stumps: snapped stone stumps with agatised growth rings, stony
# bark and splayed roots.
# --------------------------------------------------------------------------

def petrified_stumps(rng, edge):
    xx, yy = grid()
    H = np.zeros((R, R))
    count = rng.integers(1, 3) if edge else rng.integers(2, 4)
    ring = np.zeros((R, R, 3))
    for x, y in spots(rng, count, 70, 45, 85):
        r0 = rng.uniform(34, 42) if edge else rng.uniform(42, 62)
        hmax = rng.uniform(50, 70) if edge else rng.uniform(65, 105)
        th = np.arctan2(yy - y, xx - x)
        bark = 1 + 0.06 * np.sin(14 * th + rng.uniform(0, 6))
        rr = np.hypot(xx - x, (yy - y) * 1.15) / bark
        tilt = rng.uniform(0, math.tau)
        top = hmax + 14 * ((xx - x) * math.cos(tilt) + (yy - y) * math.sin(tilt)) / r0
        top = top + 6 * (fbm(rng, (R, R), 16, 2) - 0.5)
        inside = rr < r0
        H = np.where(inside, np.maximum(H, top), H)
        roots = np.clip(1 - rr / (r0 * 1.6), 0, 1) * (0.5 + 0.5 * np.cos(5 * th + 1.3)) ** 3
        H = np.maximum(H, 22 * roots)
        ring[..., 0] = np.where(inside, x, ring[..., 0])
        ring[..., 1] = np.where(inside, y, ring[..., 1])
        ring[..., 2] = np.where(inside, r0, ring[..., 2])
    H[H < 2] = 0
    tone, glint = fbm(rng, (R, R), 6, 2), value_noise(rng, (R, R), 90)
    streak = value_noise(rng, (2, R), 110)[0]

    def colour(sy, sx, z, nrm, wall, sh):
        cx, cy = ring[sy, sx, 0], ring[sy, sx, 1]
        rr = np.hypot(sx - cx, (sy - cy) * 1.15)
        rings = 0.5 + 0.5 * np.sin(rr / 2.6 + 2 * tone[sy, sx])
        top = mix((176, 150, 116), (214, 190, 150), rings)
        top = mix(top, (150, 112, 82), smooth(0.85, 1.0, rr / np.maximum(ring[sy, sx, 2], 1)) * 0.7)
        side = mix((136, 124, 110), (176, 162, 142), streak[sx])
        rgb = np.where(wall[:, None], side, top)
        return mix(rgb, (236, 220, 196), smooth(0.9, 0.95, glint[sy, sx]) * 0.6)
    return render(H, colour, ao=0.25)


SETS = {
    "penitentes": (penitentes, "fields of sun-sculpted snow-ice blades, all facing one way"),
    "termite_mounds": (termite_mounds, "tall fluted red-earth termite mounds with chimney turrets"),
    "hoodoos": (hoodoos, "banded sandstone pillars, waisted by weathering, under dark caprocks"),
    "petrified_stumps": (petrified_stumps, "snapped stone stumps with agatised growth rings and splayed roots"),
}


def render_object(name, seed, edge):
    """The HD 256x256 frame of an Outcrops object set."""
    return SETS[name][0](np.random.default_rng(seed), edge)
