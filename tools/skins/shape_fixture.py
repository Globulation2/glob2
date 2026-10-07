#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Analytic GSB1 fixture shared by the native and Studio decoders.

Writes test/fixtures/skins/shapes.json: a tiny asset whose frames can be
checked by hand (four vertices, two position shapes, one normal shape, two
clips), the expected clip-space output for a few frames computed here in plain
Python, and malformed variants that must be rejected. Needs no NumPy.
"""

import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
FRAMES = 256


def f32(values):
    return b"".join(struct.pack("<f", float(v)) for v in values)


def build(vertices, indices, mean, normal_mean, shapes, normal_shapes, clips, size=16):
    data = bytearray(b"GSB1")
    data += struct.pack(
        "<7I", len(vertices), len(indices), len(shapes), len(normal_shapes), len(clips), size, 0
    )
    data += f32(v for uv in vertices for v in uv)
    data += b"".join(struct.pack("<I", i) for i in indices)
    data += f32(v for p in mean for v in p)
    data += f32(v for n in normal_mean for v in n)
    for scale, deltas in shapes + normal_shapes:
        data += struct.pack("<f", scale)
        data += b"".join(struct.pack("<h", d) for row in deltas for d in row)
    for clip in clips:
        data += struct.pack("<2I", clip["id"], FRAMES)
        data += f32(clip["modelToClip"]) + f32(clip["normalToCamera"]) + f32(clip["headings"])
        data += f32(v for frame in clip["coefficients"] for v in frame)
        data += f32(v for frame in clip["normalCoefficients"] for v in frame)
    struct.pack_into("<I", data, 28, len(data) - 32)
    return bytes(data)


def evaluate(mean, normal_mean, shapes, normal_shapes, clip, frame):
    positions = [list(p) for p in mean]
    normals = [list(n) for n in normal_mean]
    for s, (scale, deltas) in enumerate(shapes):
        c = clip["coefficients"][frame][s] * scale
        for v, row in enumerate(deltas):
            for k in range(3):
                positions[v][k] += c * row[k]
    for s, (scale, deltas) in enumerate(normal_shapes):
        c = clip["normalCoefficients"][frame][s] * scale
        for v, row in enumerate(deltas):
            for k in range(3):
                normals[v][k] += c * row[k]
    h = clip["headings"][frame]
    cos, sin = math.cos(h), math.sin(h)
    m, n = clip["modelToClip"], clip["normalToCamera"]
    out = []
    for p, q in zip(positions, normals):
        x, y, z = cos * p[0] - sin * p[1], sin * p[0] + cos * p[1], p[2]
        nx, ny, nz = cos * q[0] - sin * q[1], sin * q[0] + cos * q[1], q[2]
        pose = [m[r * 4] * x + m[r * 4 + 1] * y + m[r * 4 + 2] * z + m[r * 4 + 3] for r in range(3)]
        normal = [n[r * 3] * nx + n[r * 3 + 1] * ny + n[r * 3 + 2] * nz for r in range(3)]
        length = math.sqrt(sum(v * v for v in normal))
        normal = [0, 0, 1] if length < 1e-8 else [v / length for v in normal]
        out.append(pose + normal)
    return out


def fixture_clips():
    """The two analytic clips: a scaled camera and a smaller, shifted one."""
    headings = [-(f // 32) * math.pi / 4 for f in range(FRAMES)]
    clips = []
    for clip_id, (sx, tz) in enumerate(((0.5, 0.0), (0.25, 0.125))):
        clips.append(
            {
                "id": clip_id,
                "modelToClip": [sx, 0, 0, 0, 0, sx, 0, 0, 0, 0, 0.01, tz, 0, 0, 0, 1],
                "normalToCamera": [1, 0, 0, 0, 0, -1, 0, 1, 0],
                "headings": headings,
                "coefficients": [
                    [math.sin(f / 10.0) * (1 + clip_id), math.cos(f / 7.0)] for f in range(FRAMES)
                ],
                "normalCoefficients": [[0.5 * math.sin(f / 5.0)] for f in range(FRAMES)],
            }
        )
    return clips


def fixture():
    vertices = [(0, 0), (1, 0), (0, 1), (1, 1)]
    indices = [0, 1, 2, 2, 1, 3]
    mean = [(1, 0, 0), (0, 1, 0), (-1, 0, 0), (0, -1, 0.5)]
    normal_mean = [(1, 0, 0), (0, 1, 0), (-1, 0, 0), (0, 0, 1)]
    shapes = [
        (0.001, [(1000, 0, 0), (0, 1000, 0), (-1000, 0, 0), (0, -1000, 0)]),
        (0.0005, [(0, 0, 2000), (0, 0, 2000), (0, 0, -2000), (0, 0, 0)]),
    ]
    normal_shapes = [(0.002, [(0, 0, 500), (0, 0, 500), (0, 0, 500), (500, 0, 0)])]
    clips = fixture_clips()
    data = build(vertices, indices, mean, normal_mean, shapes, normal_shapes, clips)
    expected = {
        f"{clip['id']}:{frame}": evaluate(mean, normal_mean, shapes, normal_shapes, clip, frame)
        for clip in clips
        for frame in (0, 1, 31, 32, 100, 255)
    }
    malformed = {
        "badMagic": b"GSK1" + data[4:],
        "truncated": data[:-1],
        "trailing": data + b"\0",
        "badIndex": data[: 32 + 4 * 8] + struct.pack("<I", 9) + data[32 + 4 * 8 + 4 :],
        "zeroScale": data[: 32 + 4 * 8 + 6 * 4 + 4 * 24] + struct.pack("<f", 0.0)
        + data[32 + 4 * 8 + 6 * 4 + 4 * 24 + 4 :],
    }
    header = struct.unpack_from("<4s7I", data)
    return {
        "format": "GSB1",
        "hex": data.hex(),
        "header": {
            "vertices": header[1],
            "indices": header[2],
            "shapes": header[3],
            "normalShapes": header[4],
            "clips": header[5],
            "logicalSize": header[6],
        },
        "expected": expected,
        "malformed": {name: bytes_.hex() for name, bytes_ in malformed.items()},
    }


if __name__ == "__main__":
    path = ROOT / "test/fixtures/skins/shapes.json"
    path.write_text(json.dumps(fixture(), separators=(",", ":")) + "\n")
    print(path)
