#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The fit behind MapGeneration::bestFarmRows (src/map/generator/shared/Farmland.h).

A farm is rows of grass and water at some angle. Wheat and wood regrow on a tile when a probe
offset by the difference of two draws of 0..15 on each axis lands on pure water and the opposite
probe does not land on pure sand (Map::growResources). This script lays rows of every width pair
over a torus, applies the beach rule (every land vertex beside water turns to sand) and the
corner rule (a tile is pure grass, sand or water only when all four corners are), and sums each
pure-grass tile's exact chance of passing that test. The best pair per angle, and how flat the
optimum is, are printed; bestFarmRows is a rough line through them.

Needs numpy. Takes about half a minute.
"""
import math

import numpy as np

N = 180
WEIGHT = np.array([16 - abs(d) for d in range(-15, 16)], float) / 256.0


def row_yield(grass, water, theta):
    ys, xs = np.mgrid[0:N, 0:N]
    phase = np.mod(xs * math.cos(theta) + ys * math.sin(theta), grass + water)
    wet = phase < water
    near = np.zeros_like(wet)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            near |= np.roll(np.roll(wet, dy, 0), dx, 1)
    vertex = np.where(wet, 2, np.where(near, 1, 0))
    corners = [vertex, np.roll(vertex, -1, 1), np.roll(vertex, -1, 0),
               np.roll(np.roll(vertex, -1, 0), -1, 1)]
    grass_tiles = np.all([c == 0 for c in corners], axis=0)
    sand_tiles = np.all([c == 1 for c in corners], axis=0).astype(float)
    water_tiles = np.all([c == 2 for c in corners], axis=0).astype(float)
    chance = np.zeros((N, N))
    for i, dy in enumerate(range(-15, 16)):
        w_rows = np.roll(water_tiles, -dy, 0)
        s_rows = np.roll(sand_tiles, dy, 0)
        for j, dx in enumerate(range(-15, 16)):
            chance += WEIGHT[i] * WEIGHT[j] * np.roll(w_rows, -dx, 1) * (1 - np.roll(s_rows, dx, 1))
    return (chance * grass_tiles).sum() / (N * N)


def main():
    for degrees in (0, 11.25, 22.5, 33.75, 45):
        theta = math.radians(degrees)
        scores = {(g, w): row_yield(g, w, theta) for g in range(4, 25, 2) for w in range(2, 15)}
        best = max(scores, key=scores.get)
        for g in (best[0] - 1, best[0] + 1):
            scores[(g, best[1])] = row_yield(g, best[1], theta)
        best = max(scores, key=scores.get)
        top = sorted(scores.items(), key=lambda kv: -kv[1])[:5]
        print(f"{degrees:6}: grass {best[0]}, water {best[1]}, yield {scores[best]:.4f}; "
              f"next best {[(k, round(v, 4)) for k, v in top[1:]]}")


if __name__ == '__main__':
    main()
