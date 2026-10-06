#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate the small shared GSR1 conformance fixture (no Blender required).

Expected poses use an analytic rotating/scaling child of a translating root,
independent of the production matrix/interpolation implementation.
"""

import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]


def fixture():
    data = bytearray(b"GSR1")

    def words(*values):
        data.extend(struct.pack("<" + "I" * len(values), *values))

    def floats(*values):
        data.extend(struct.pack("<" + "f" * len(values), *values))

    def transform(x=0, y=0, angle=0, scale=1, sign=1):
        floats(x, y, 0, 0, 0, sign * math.sin(angle / 2), sign * math.cos(angle / 2), scale)

    words(3, 3, 2, 2, 38, 0)
    rest = [(0, 0, 0), (2, 0, 0), (1, 1, 0)]
    for v, p in enumerate(rest):
        floats(*p, math.sqrt(0.5), math.sqrt(0.5), 0, v / 2, 0.5)
        words(0, 1, 0, 0)
        weight = [0, 0.75, 1][v]
        floats(1 - weight, weight, 0, 0)
    words(0, 1, 2)
    words(0xFFFFFFFF)
    transform()
    transform()
    words(0)
    transform(x=1)
    transform(x=-1)
    clip_offsets = []
    for clip in range(2):
        clip_offsets.append(len(data))
        words(clip, 4)
        floats(2)
        floats(0.1, 0, 0, 0, 0, 0.1, 0, 0, 0, 0, -0.01, 0, 0, 0, 0, 1)
        floats(1, 0, 0, 0, 1, 0, 0, 0, 1)
        floats(0, 0, 0, 3)
        for frame in range(256):
            direction, phase = divmod(frame, 32)
            # Alternating headings retain alternating halves of a 64-sample gait.
            floats(-direction * math.pi / 4, ((direction % 2) * 32 + phase) / 32)
        for key in range(4):
            transform(y=[0, 0.5, 0, -0.5][key])
            transform(
                x=1,
                angle=key * math.pi / 2,
                scale=[1, 2, 1, 2][key],
                sign=-1 if clip and key % 2 else 1,
            )
    struct.pack_into("<I", data, 24, len(data) - 28)
    # Read serialized frame values, so expectations include float32 time/angle.
    expected = []
    for frame in range(256):
        heading, time = struct.unpack_from("<ff", data, clip_offsets[0] + 128 + frame * 8)
        key = int(time * 2)
        fraction = time * 2 - key
        scale = [1, 2, 1, 2][key] * (1 - fraction) + [1, 2, 1, 2][(key + 1) % 4] * fraction
        y = [0, 0.5, 0, -0.5][key] * (1 - fraction) + [0, 0.5, 0, -0.5][(key + 1) % 4] * fraction
        a = time * math.pi
        pose = []
        for v, p in enumerate(rest):
            w = [0, 0.75, 1][v]
            child = (
                1 + scale * (math.cos(a) * (p[0] - 1) - math.sin(a) * p[1]),
                y + scale * (math.sin(a) * (p[0] - 1) + math.cos(a) * p[1]),
            )
            x = (1 - w) * p[0] + w * child[0]
            yy = (1 - w) * (p[1] + y) + w * child[1]
            nx = (1 - w) * math.sqrt(0.5) + w / scale * math.cos(a + math.pi / 4)
            ny = (1 - w) * math.sqrt(0.5) + w / scale * math.sin(a + math.pi / 4)
            norm = math.hypot(nx, ny)
            pose.extend(
                [
                    0.1 * (math.cos(heading) * x - math.sin(heading) * yy),
                    0.1 * (math.sin(heading) * x + math.cos(heading) * yy),
                    0,
                    (math.cos(heading) * nx - math.sin(heading) * ny) / norm,
                    (math.sin(heading) * nx + math.cos(heading) * ny) / norm,
                    0,
                ]
            )
        expected.append(pose)

    def bits(v):
        return struct.unpack("<I", struct.pack("<f", v))[0]

    clip = clip_offsets[0]
    malformed = [
        ("magic", 0, 0),
        ("vertices", 4, 8193),
        ("index count", 8, 4),
        ("bones", 12, 33),
        ("clips", 16, 9),
        ("canvas", 20, 129),
        ("payload", 24, 0),
        ("nonfinite rest", 28, 0x7FC00000),
        ("normal", 40, 0),
        ("UV", 52, bits(2)),
        ("influence", 60, 2),
        ("negative weight", 76, bits(-1)),
        ("weight sum", 76, bits(0.5)),
        ("index", 220, 3),
        ("parent order", 300, 1),
        ("inverse bind", 336, bits(0)),
        ("sample count", clip + 4, 257),
        ("duration", clip + 8, 0),
        ("singular camera", clip + 12, 0),
        ("perspective camera", clip + 60, bits(1)),
        ("normal camera", clip + 76, bits(2)),
        ("radius", clip + 124, 0),
        ("heading", clip + 128, bits(7)),
        ("frame time", clip + 132, bits(2)),
        ("track quaternion", clip + 2176 + 24, 0),
        ("track scale", clip + 2176 + 28, bits(-1)),
        ("duplicate clip", clip_offsets[1], 0),
    ]
    # Adjacent serialized floats make endpoint behavior a shared contract, not
    # an accident of C++ float versus JavaScript double arithmetic.
    boundaries = []

    def boundary(name, accepted, patches):
        boundaries.append(
            {
                "name": name,
                "accepted": accepted,
                "patches": [{"offset": o, "word": w} for o, w in patches],
            }
        )

    duration = bits(0.0001)
    zero_times = [(clip + 132 + frame * 8, 0) for frame in range(256)]
    for delta, accepted in [(-1, False), (0, True), (1, True)]:
        boundary(
            f"duration endpoint {delta:+d}", accepted, [(clip + 8, duration + delta), *zero_times]
        )
    for delta, accepted in [(-1, True), (0, True), (1, False)]:
        boundary(f"heading endpoint {delta:+d}", accepted, [(clip + 128, bits(6.283186) + delta)])
    # Root animation scales at the lower endpoint exercise both transform and
    # hierarchy validation. The child retains unit scale to avoid multiplying
    # otherwise valid lower endpoints into an unsupported deformation scale.
    for delta, accepted in [(-1, False), (0, True), (1, True)]:
        patches = []
        for key in range(4):
            patches.extend(
                [
                    (clip + 2176 + key * 64 + 28, bits(0.0001) + delta),
                    (clip + 2176 + key * 64 + 60, bits(1)),
                ]
            )
        boundary(f"scale endpoint {delta:+d}", accepted, patches)

    # Unit-normal, unit-quaternion and normalized-weight tolerance edges.
    # Find the adjacent binary32 values straddling each exact binary64 check.
    def f32(word):
        return struct.unpack("<f", struct.pack("<I", word))[0]

    def tolerance_edge(name, offset, estimate, valid):
        word = bits(estimate)
        while valid(f32(word)):
            word += 1
        while not valid(f32(word - 1)):
            word -= 1
        boundary(name + " inside", True, [(offset, word - 1)])
        boundary(name + " outside", False, [(offset, word)])

    half_normal = f32(bits(math.sqrt(0.5)))
    tolerance_edge(
        "normal tolerance",
        40,
        math.sqrt(1.0001 - half_normal**2),
        lambda v: abs(v * v + half_normal**2 - 1) < 0.0001,
    )
    tolerance_edge("weight tolerance", 80, 0.0001, lambda v: abs(1 + v - 1) < 0.0001)
    tolerance_edge(
        "quaternion tolerance",
        clip + 2176 + 24,
        math.sqrt(1.0001),
        lambda v: abs(v * v - 1) <= 0.0001,
    )
    tolerance_edge(
        "normal camera tolerance", clip + 76, math.sqrt(1.0001), lambda v: abs(v * v - 1) < 0.0001
    )
    # Four legal normalized binary32 weights sum to 0.9999999403953552
    # when accumulated in binary32. Camera translation must use homogeneous
    # w=1 rather than this rounded sum. Zero rest positions isolate that error.
    affine_camera = bytearray(data)
    struct.pack_into("<I", affine_camera, 20, 128)
    weights = [0.4496974050998688, 0.021457619965076447, 0.3954407572746277, 0.1334042102098465]
    for vertex in range(3):
        struct.pack_into("<3f", affine_camera, 28 + vertex * 64, 0, 0, 0)
        struct.pack_into("<4I4f", affine_camera, 60 + vertex * 64, 0, 0, 0, 0, *weights)
    for offset in clip_offsets:
        struct.pack_into("<f", affine_camera, offset + 24, 10000)
        for frame in range(256):
            struct.pack_into("<2f", affine_camera, offset + 128 + frame * 8, 0, 0)
    affine_expected = [10000, 0, 0, math.sqrt(0.5), math.sqrt(0.5), 0] * 3
    return {
        "format": "GSR1",
        "hex": data.hex(),
        "expected": expected,
        "malformed": [{"name": n, "offset": o, "word": w} for n, o, w in malformed],
        "boundaries": boundaries,
        "affineCamera": {"hex": affine_camera.hex(), "expected": affine_expected},
    }


if __name__ == "__main__":
    (ROOT / "test/fixtures/skins/rig.json").write_text(
        json.dumps(fixture(), separators=(",", ":")) + "\n"
    )
