#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the placeholder ice and cobblestone tiles, data/gfx/terrain272..303.

Ice is 272..287 and cobblestone 288..303 (see Map::ICE_TILE_FIRST). Each range holds
16 variants of a flat, seamless 32x32 tile with no transition art: a prototype
stand-in until real artwork replaces the files under the same names.

Run from the repository root; --dst DIR writes elsewhere. Output is deterministic.
"""
import argparse
import numpy as np
from PIL import Image

SIZE = 32
N_VARIANTS = 16
ICE_FIRST = 272
COBBLESTONE_FIRST = 288


def periodic_noise(rng, cells):
    """Smooth noise that tiles on SIZE: bilinear interpolation of a wrapped lattice."""
    lattice = rng.random((cells, cells))
    t = np.arange(SIZE) * cells / SIZE
    i0 = np.floor(t).astype(int)
    f = t - i0
    i1 = (i0 + 1) % cells
    a = lattice[i0][:, i0] * (1 - f)[None, :] + lattice[i0][:, i1] * f[None, :]
    b = lattice[i1][:, i0] * (1 - f)[None, :] + lattice[i1][:, i1] * f[None, :]
    return a * (1 - f)[:, None] + b * f[:, None]


def ice_tile(rng):
    shade = 0.6 * periodic_noise(rng, 4) + 0.4 * periodic_noise(rng, 8)
    base = np.array([196, 226, 240], float)
    img = base[None, None, :] * (0.88 + 0.16 * shade[..., None])
    # A few thin cracks, wrapped so the tile stays seamless.
    for _ in range(3):
        x, y = rng.integers(0, SIZE, 2)
        dx, dy = rng.choice([-1, 0, 1], 2)
        if dx == 0 and dy == 0:
            dx = 1
        for _ in range(rng.integers(8, 20)):
            img[y % SIZE, x % SIZE] = [150, 185, 205]
            x += dx if rng.random() < 0.8 else rng.choice([-1, 1])
            y += dy if rng.random() < 0.8 else rng.choice([-1, 1])
    return img


def cobblestone_tile(rng):
    img = np.empty((SIZE, SIZE, 3))
    mortar = np.array([92, 88, 80], float)
    img[:] = mortar
    # 8x8 stones in running bond: odd rows shift by half a stone; both wrap on 32.
    for row in range(4):
        shift = 4 if row % 2 else 0
        for col in range(4):
            tone = 128 + rng.integers(-18, 19)
            stone = np.array([tone, tone - 4, tone - 12], float)
            for y in range(row * 8 + 1, row * 8 + 7):
                for x in range(col * 8 + shift + 1, col * 8 + shift + 7):
                    light = 1.08 if (y - row * 8) < 3 else 0.95
                    img[y % SIZE, x % SIZE] = stone * light
    noise = periodic_noise(rng, 8)
    return img * (0.94 + 0.12 * noise[..., None])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dst', default='data/gfx')
    args = ap.parse_args()
    rng = np.random.default_rng(20260916)
    for first, draw in ((ICE_FIRST, ice_tile), (COBBLESTONE_FIRST, cobblestone_tile)):
        for v in range(N_VARIANTS):
            pixels = np.clip(draw(rng), 0, 255).astype(np.uint8)
            Image.fromarray(pixels, 'RGB').save(f'{args.dst}/terrain{first + v}.png')


if __name__ == '__main__':
    main()
