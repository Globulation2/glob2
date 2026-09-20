#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Decode helpers shared by the policy server and PPO. Pure numpy.

Anchor detection lives here because the naive version -- "a cell is an anchor
if it is marked and the cells above and to its left are not" -- is wrong
whenever two buildings of the same type touch: the second one's top-left has
a marked neighbour and is never found. The review caught this; the reconciler's
own nearby-placement makes adjacency common, and a missed anchor undercounts
the budget, is never restaffed, and for a 1x1 flag reads as an orphan the
reconciler then moves.

Buildings are axis-aligned rectangles whose size depends on type AND level
(swarm 4x4; inn 2x2 then 3x3 at level 2; racetrack 4x4 then 6x6; ...). So:
scan row-major, and when a marked cell is not yet claimed and its left/up
neighbours are either unmarked or already claimed by a different building,
it is an anchor -- claim its whole W x H block (with toroidal wrap). Adjacent
same-type buildings are separated because the first one's block is claimed
before the second one's top-left is reached; a building wrapping the map edge
is handled because its wrapped tail at column 0 has an unclaimed marked left
neighbour (column W-1) and is skipped until the true anchor is reached.
"""
from __future__ import annotations

import numpy as np

# IntBuildingType order: swarm, inn, hospital, racetrack, swimmingpool,
# barracks, school, defencetower, explorationflag, warflag, clearingflag,
# stonewall, market. Footprint width (== height) per level 0/1/2, from
# src/game/entities/BuildingTypes*.cpp.
FOOTPRINT = np.array([
    [4, 4, 4],   # swarm
    [2, 2, 3],   # inn
    [2, 2, 2],   # hospital
    [4, 6, 6],   # racetrack
    [4, 6, 6],   # swimmingpool
    [4, 4, 4],   # barracks
    [2, 2, 2],   # school
    [2, 2, 2],   # defencetower
    [1, 1, 1],   # explorationflag
    [1, 1, 1],   # warflag
    [1, 1, 1],   # clearingflag
    [1, 1, 1],   # stonewall
    [3, 3, 3],   # market
], dtype=np.int64)

# DP_MY_BUILDING_LEVEL stores level * 60 in a byte (NeuroticaObservation.cpp).
LEVEL_SCALE = 60.0


def anchors(my_buildings: np.ndarray, level_plane: np.ndarray):
    """my_buildings: (13, H, W) in [0, 1]; level_plane: (H, W) in [0, 1].

    Returns (anchor_mask (13, H, W) bool, counts (13,) int64).
    """
    T, H, W = my_buildings.shape
    marked = my_buildings > 0.5
    level = np.clip(np.rint(level_plane * 255.0 / LEVEL_SCALE), 0, 2).astype(np.int64)
    anchor = np.zeros_like(marked)
    claimed = np.zeros((H, W), dtype=np.int64)     # 0 = free, else building id
    counts = np.zeros(T, dtype=np.int64)
    next_id = 1
    for t in range(T):
        ys, xs = np.nonzero(marked[t])
        while np.any(marked[t] & (claimed == 0)):
            before = int(counts[t])
            for y, x in zip(ys, xs):                   # nonzero is row-major
                if claimed[y, x]:
                    continue
                up = marked[t, (y - 1) % H, x] and not claimed[(y - 1) % H, x]
                left = marked[t, y, (x - 1) % W] and not claimed[y, (x - 1) % W]
                if up or left:
                    continue                           # continuation of an unclaimed run
                size = int(FOOTPRINT[t, level[y, x]])
                yy = (y + np.arange(size)) % H
                xx = (x + np.arange(size)) % W
                claimed[np.ix_(yy, xx)] = next_id
                anchor[t, y, x] = True
                counts[t] += 1
                next_id += 1
            if counts[t] == before:
                raise ValueError("ambiguous footprint anchors; use engine entity identities")
    return anchor, counts


if __name__ == "__main__":
    # Self-test: the cases the naive rule gets wrong.
    H = W = 16
    mb = np.zeros((13, H, W), dtype=np.float32)
    lv = np.zeros((H, W), dtype=np.float32)

    def put(t, x, y, size, level=0):
        for dy in range(size):
            for dx in range(size):
                mb[t, (y + dy) % H, (x + dx) % W] = 1.0
                lv[(y + dy) % H, (x + dx) % W] = level * LEVEL_SCALE / 255.0

    put(1, 2, 2, 2)        # inn A
    put(1, 4, 2, 2)        # inn B, touching A on the right  (naive rule misses)
    put(1, 2, 4, 2)        # inn C, touching A below         (naive rule misses)
    put(0, 8, 8, 4)        # swarm
    put(6, 15, 15, 2)      # school wrapping both map edges  (naive rule mis-anchors)
    put(1, 10, 1, 3, 2)    # level-2 inn, 3x3
    a, c = anchors(mb, lv)
    got = {t: sorted(zip(*np.nonzero(a[t])[::-1])) for t in range(13) if c[t]}
    exp = {1: [(2, 2), (2, 4), (4, 2), (10, 1)], 0: [(8, 8)], 6: [(15, 15)]}
    ok = all(got.get(t) == sorted(v) for t, v in exp.items()) and c[1] == 4 and c[0] == 1 and c[6] == 1
    print("anchors:", {t: [tuple(map(int, p)) for p in v] for t, v in got.items()})
    print("counts :", {t: int(c[t]) for t in range(13) if c[t]})
    print("SELF-TEST", "PASS" if ok else "FAIL")
    raise SystemExit(0 if ok else 1)
