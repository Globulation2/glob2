#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the prototype ice and cobblestone tiles from the game's own terrain art.

Writes data/gfx/terrain272..527:
  272..287  ice, whole tiles           (16 variants)
  288..303  cobblestone, whole tiles   (16 variants)
  304..415  ice edges: 14 corner shapes x 8 variants, drawn over the neighbouring terrain
  416..527  cobblestone edges, likewise
(see Map::ICE_TILE_FIRST and friends). The textures are the water tiles (terrain256..271)
recoloured to pale ice with a few cracks, and the sand tiles' grain (terrain128..143) pressed
into rounded cobbles, so both share the existing tiles' grain and noise. An edge tile is its
texture cut to the alpha of the sand-over-water edge tile of the same corner shape, so ice and
cobblestone meet other terrain with the same ragged outline as a beach.

Run from the repository root; --dst DIR writes elsewhere. Output is deterministic.
"""
import argparse
import numpy as np
from PIL import Image

SIZE = 32
ICE_FIRST, COBBLESTONE_FIRST = 272, 288
ICE_EDGE_FIRST, COBBLESTONE_EDGE_FIRST = 304, 416
EDGE_VARIANTS = 8

# The sand-over-water tiles by the corners that are sand, as a mask with tl=8, tr=4, bl=2,
# br=1 (Map::lookup's table, S/E entries); each id starts 8 variants.
SAND_OVER_WATER = {
    0b1110: 208, 0b1101: 216, 0b1100: 176, 0b1011: 232, 0b1010: 192, 0b1001: 240, 0b1000: 160,
    0b0111: 224, 0b0110: 248, 0b0101: 200, 0b0100: 168, 0b0011: 184, 0b0010: 152, 0b0001: 144,
}


def load(src, i, mode='RGB'):
    return np.asarray(Image.open(f'{src}/terrain{i}.png').convert(mode)).astype(float)


def ice_tile(src, v, rng):
    water = load(src, 256 + v)
    lum = water.mean(axis=2)
    lum = (lum - lum.min()) / max(1e-6, lum.max() - lum.min())
    # Pale, slightly blue ice keeping the water's mottling.
    dark, light = np.array([150, 190, 214]), np.array([226, 242, 250])
    img = dark + (light - dark) * (0.35 + 0.65 * lum)[..., None]
    # A few thin, faint cracks that wrap, so tiles stay seamless.
    for _ in range(rng.integers(0, 2)):
        x, y = rng.integers(0, SIZE, 2)
        dx, dy = rng.choice([-1, 1], 2)
        for _ in range(rng.integers(6, 14)):
            img[y % SIZE, x % SIZE] = img[y % SIZE, x % SIZE] * 0.9
            x += dx if rng.random() < 0.7 else 0
            y += dy if rng.random() < 0.7 else 0
    return img


def cobblestone_tile(src, v, rng):
    sand = load(src, 128 + v)
    grain = sand.mean(axis=2)
    grain = (grain - grain.mean()) / max(1e-6, grain.std())
    # Rounded cobbles on a jittered 8-pixel lattice that wraps on 32.
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    cells = [((cx * 8 + 4 + rng.integers(-1, 2)) % SIZE, (cy * 8 + 4 + (4 if cx % 2 else 0) + rng.integers(-1, 2)) % SIZE,
              rng.integers(-14, 15)) for cx in range(4) for cy in range(4)]
    near = np.full((SIZE, SIZE), 1e9)
    second = np.full((SIZE, SIZE), 1e9)
    tone = np.zeros((SIZE, SIZE))
    for cx, cy, t in cells:
        ddx = np.minimum(abs(xs - cx), SIZE - abs(xs - cx))
        ddy = np.minimum(abs(ys - cy), SIZE - abs(ys - cy))
        d = np.hypot(ddx, ddy)
        closer = d < near
        second = np.where(closer, near, np.minimum(second, d))
        tone = np.where(closer, t, tone)
        near = np.where(closer, d, near)
    edge = np.clip((second - near) / 2.0, 0, 1)  # 0 on the joint between stones, 1 inside
    base = np.array([146, 132, 112]) + tone[..., None]
    joint = np.array([84, 74, 62])
    img = joint + (base - joint) * edge[..., None]
    # Light from the top-left: stones lighter towards their upper-left.
    img *= (1.0 + 0.10 * np.clip(-(ys % 8 - 4) / 4, -1, 1))[..., None]
    return img * (1.0 + 0.07 * grain)[..., None]


def save(pixels, dst, i, alpha=None):
    rgb = np.clip(pixels, 0, 255).astype(np.uint8)
    if alpha is None:
        Image.fromarray(rgb, 'RGB').save(f'{dst}/terrain{i}.png')
    else:
        rgba = np.dstack([rgb, alpha.astype(np.uint8)])
        Image.fromarray(rgba, 'RGBA').save(f'{dst}/terrain{i}.png')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default='data/gfx')
    ap.add_argument('--dst', default='data/gfx')
    args = ap.parse_args()
    rng = np.random.default_rng(20260916)
    for first, edge_first, draw in ((ICE_FIRST, ICE_EDGE_FIRST, ice_tile),
                                    (COBBLESTONE_FIRST, COBBLESTONE_EDGE_FIRST, cobblestone_tile)):
        tiles = [draw(args.src, v, rng) for v in range(16)]
        for v, tile in enumerate(tiles):
            save(tile, args.dst, first + v)
        for mask in range(1, 15):
            for v in range(EDGE_VARIANTS):
                alpha = load(args.src, SAND_OVER_WATER[mask] + v, 'RGBA')[..., 3]
                save(tiles[v], args.dst, edge_first + (mask - 1) * EDGE_VARIANTS + v, alpha)


if __name__ == '__main__':
    main()
