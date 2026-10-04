# SPDX-License-Identifier: GPL-3.0-or-later
"""Geometry/paint acceptance checks shared by generated and installed assets."""

import math
import struct
from collections import Counter


def validate_surface(data, contract):
    _, count, index_count, frames, _ = struct.unpack_from("<4sIIII", data)
    regions = contract["regions"]
    assert len(regions) == count and set(regions) == {-1, 0, 1, 2, 3}
    uv = list(struct.iter_unpack("<2f", data[20 : 20 + count * 8]))
    start = 20 + count * 8
    triangles = list(struct.iter_unpack("<3I", data[start : start + index_count * 4]))
    edges = Counter()
    for triangle in triangles:
        assert len(set(triangle)) == 3
        limb_regions = {regions[i] for i in triangle} - {-1}
        assert len(limb_regions) <= 1, "triangle bridges different limbs"
        for a, b in zip(triangle, (*triangle[1:], triangle[0])):
            edges[tuple(sorted((a, b)))] += 1
    assert set(edges.values()) == {2}, "open or non-manifold socket/limb surface"
    # A single welded surface, rather than detached limbs or coincident shells.
    adjacency = [[] for _ in regions]
    for a, b in edges:
        adjacency[a].append(b)
        adjacency[b].append(a)
    seen, pending = {0}, [0]
    while pending:
        for neighbour in adjacency[pending.pop()]:
            if neighbour not in seen:
                seen.add(neighbour)
                pending.append(neighbour)
    assert len(seen) == count, "disconnected limb"
    triangle_set = {tuple(sorted(t)) for t in triangles}
    reflections = contract["reflections"]
    for name in ("frontBack", "topBottom"):
        pairs = reflections[name]
        assert len(pairs) == count and sorted(pairs) == list(range(count))
        assert {
            tuple(sorted(pairs[i] for i in t)) for t in triangles
        } == triangle_set, "reflection changes triangle interpolation"
        for i, j in enumerate(pairs):
            assert pairs[j] == i, "reflection is not an involution"
            assert math.dist(uv[i], uv[j]) < 1e-7, "paint symmetry mismatch"
    fb, tb = reflections["frontBack"], reflections["topBottom"]
    assert all(fb[tb[i]] == tb[fb[i]] for i in range(count))
    assert all(
        regions[tb[i]] == ({0: 2, 1: 3, 2: 0, 3: 1}.get(region, -1))
        for i, region in enumerate(regions)
    ), "flip does not exchange matching limbs"
    stride = count * 24
    start += index_count * 4
    for direction in range(8):
        poses = [
            list(
                struct.iter_unpack(
                    "<6f",
                    data[
                        start
                        + (direction * 32 + phase) * stride : start
                        + (direction * 32 + phase + 1) * stride
                    ],
                )
            )
            for phase in range(32)
        ]
        for pose in poses:
            for a, b, c in triangles:
                ab = [pose[b][k] - pose[a][k] for k in range(3)]
                ac = [pose[c][k] - pose[a][k] for k in range(3)]
                cross = (
                    ab[1] * ac[2] - ab[2] * ac[1],
                    ab[2] * ac[0] - ab[0] * ac[2],
                    ab[0] * ac[1] - ab[1] * ac[0],
                )
                assert sum(v * v for v in cross) > 1e-18, "collapsed joint triangle"

        def displacement(a, b, permutation):
            return (
                sum(math.dist(a[i][:3], b[j][:3]) for i, j in enumerate(permutation))
                / count
            )

        identity = list(range(count))
        step = max(displacement(a, b, identity) for a, b in zip(poses, poses[1:]))
        loop = min(
            displacement(poses[-1], poses[0], permutation)
            for permutation in (identity, fb, tb, [fb[i] for i in tb])
        )
        assert loop <= step * 2.5, "flip-loop discontinuity"
