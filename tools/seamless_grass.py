#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the 16 grass variants data/gfx/terrain0..15 tile without seams.

Builds one toroidal master tile from terrain0 blended with its half-offset
copy, so the master's edges come from the continuous interior, then fades
every variant into that master over its outer four pixels. Any two variants
meet with interior-level continuity while their centres stay distinct, and
the script prints the mean interior and cross-tile pixel steps to confirm it.

Run from the repository root; reads and overwrites data/gfx/terrain{0..15}.png.
--src DIR reads the input tiles from elsewhere, --dst DIR writes elsewhere.
"""
import argparse
import numpy as np
from PIL import Image

N_VARIANTS = 16
SIZE = 32
MASTER_FADE = 8.0   # px over which master edges come from the offset copy
BORDER_FADE = 4.0   # px over which each variant fades into the master border


def ring_distance(n):
    idx = np.arange(n)
    edge = np.minimum(idx, n - 1 - idx)
    return np.minimum(edge[:, None], edge[None, :])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default='data/gfx', help='directory with the original terrain PNGs')
    ap.add_argument('--dst', default='data/gfx', help='directory to write the seamless PNGs to')
    args = ap.parse_args()

    tiles = [np.asarray(Image.open(f'{args.src}/terrain{i}.png').convert('RGB')).astype(float)
             for i in range(N_VARIANTS)]
    d = ring_distance(SIZE)[..., None]

    base = tiles[0]
    offset = np.roll(np.roll(base, SIZE // 2, axis=0), SIZE // 2, axis=1)
    a = np.clip(1 - d / MASTER_FADE, 0, 1)
    master = a * offset + (1 - a) * base

    w = np.clip(1 - d / BORDER_FADE, 0, 1)
    for i, t in enumerate(tiles):
        out = w * master + (1 - w) * t
        Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8)).save(f'{args.dst}/terrain{i}.png')

    def step(t, axis):
        return np.abs(np.diff(t, axis=axis)).mean()
    outs = [np.asarray(Image.open(f'{args.dst}/terrain{i}.png')).astype(float) for i in range(N_VARIANTS)]
    cross = np.mean([np.abs(p[:, -1] - q[:, 0]).mean() for p in outs for q in outs])
    print(f'interior step {np.mean([step(t, 1) for t in outs]):.2f}, cross-tile step {cross:.2f}')


if __name__ == '__main__':
    main()
