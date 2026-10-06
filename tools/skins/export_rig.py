#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""GSR1 export helpers for authored Blender rigs; no runtime surface rebuilding.

Transforms must be translation, quaternion rotation and positive uniform scale.
Validate the serialized values before returning bytes: rounding to binary32 must
not turn a valid authoring value into an asset that production decoders reject.
"""

import hashlib
import json
import math
from pathlib import Path
import struct

NO_PARENT = 0xFFFFFFFF
TOLERANCE = 0.0001


def _f32(value):
    if not math.isfinite(value) or abs(value) > 10000:
        raise ValueError("Invalid GSR1 scalar")
    return struct.unpack("<f", struct.pack("<f", value))[0]


MINIMUM_SCALE = _f32(0.0001)
MINIMUM_DURATION = _f32(0.0001)
MAXIMUM_HEADING = _f32(6.283186)


def _values(values, count, label):
    if len(values) != count:
        raise ValueError(f"Invalid GSR1 {label} length")
    return [_f32(value) for value in values]


def transform(matrix):
    """Return serialized TRS, rejecting matrices the runtime cannot represent."""
    translation, rotation, scale = matrix.decompose()
    if min(scale) <= 0 or max(scale) - min(scale) > 1e-5 * max(scale):
        raise ValueError("GSR1 requires positive uniform scale")
    from mathutils import Matrix

    uniform = sum(scale) / 3
    rebuilt = (
        Matrix.Translation(translation) @ rotation.to_matrix().to_4x4() @ Matrix.Scale(uniform, 4)
    )
    if max(abs(matrix[y][x] - rebuilt[y][x]) for y in range(4) for x in range(4)) > 1e-4:
        raise ValueError("GSR1 cannot represent shear")
    result = _values(
        [*translation, rotation.x, rotation.y, rotation.z, rotation.w, uniform], 8, "transform"
    )
    if result[7] < MINIMUM_SCALE or abs(sum(v * v for v in result[3:7]) - 1) > TOLERANCE:
        raise ValueError("Invalid GSR1 transform")
    return result


def _matrix(trs):
    # Decoders normalize serialized quaternions and round the stored values
    # back to binary32 before checking bind transforms with binary64 arithmetic.
    norm = math.sqrt(sum(value * value for value in trs[3:7]))
    x, y, z, w = (_f32(value / norm) for value in trs[3:7])
    s = trs[7]
    return [
        (1 - 2 * (y * y + z * z)) * s,
        2 * (x * y - z * w) * s,
        2 * (x * z + y * w) * s,
        trs[0],
        2 * (x * y + z * w) * s,
        (1 - 2 * (x * x + z * z)) * s,
        2 * (y * z - x * w) * s,
        trs[1],
        2 * (x * z - y * w) * s,
        2 * (y * z + x * w) * s,
        (1 - 2 * (x * x + y * y)) * s,
        trs[2],
        0,
        0,
        0,
        1,
    ]


def _multiply(a, b):
    return [sum(a[y * 4 + k] * b[k * 4 + x] for k in range(4)) for y in range(4) for x in range(4)]


def _camera(clip):
    m = _values(clip["modelToClip"], 16, "camera")
    n = _values(clip["normalToCamera"], 9, "normal camera")
    pivot = _values(clip["pivot"], 3, "pivot")
    radius = _f32(clip["radius"])
    determinant = (
        m[0] * (m[5] * m[10] - m[6] * m[9])
        - m[1] * (m[4] * m[10] - m[6] * m[8])
        + m[2] * (m[4] * m[9] - m[5] * m[8])
    )
    if m[12:] != [0, 0, 0, 1] or abs(determinant) <= 1e-12 or radius <= 0:
        raise ValueError("Invalid GSR1 camera or radius")
    for i in range(3):
        for j in range(3):
            dot = sum(n[i * 3 + k] * n[j * 3 + k] for k in range(3))
            if abs(dot - (1 if i == j else 0)) >= TOLERANCE:
                raise ValueError("Invalid GSR1 normal camera")
    return [*m, *n, *pivot, radius]


def _validate_scales(samples, bones):
    lo, hi = [], []
    for bone, (parent, _, inverse) in enumerate(bones):
        scales = [sample[bone][7] for sample in samples]
        lo.append(min(scales) * (1 if parent == NO_PARENT else lo[parent]))
        hi.append(max(scales) * (1 if parent == NO_PARENT else hi[parent]))
        if (
            lo[bone] < MINIMUM_SCALE
            or hi[bone] > 10000
            or lo[bone] * inverse[7] < MINIMUM_SCALE
            or hi[bone] * inverse[7] > 10000
        ):
            raise ValueError("Unbounded GSR1 hierarchy or deformation scale")


def encode(vertices, indices, bones, clips, logical_size):
    if (
        not 3 <= len(vertices) <= 8192
        or not 3 <= len(indices) <= 49152
        or len(indices) % 3
        or not 1 <= len(bones) <= 32
        or not 1 <= len(clips) <= 8
        or not 1 <= logical_size <= 128
    ):
        raise ValueError("GSR1 dimensions exceed v1 limits")
    data = bytearray(b"GSR1")

    def words(*values):
        if any(not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF for value in values):
            raise ValueError("Invalid GSR1 integer")
        data.extend(struct.pack("<" + "I" * len(values), *values))

    def floats(*values):
        data.extend(struct.pack("<" + "f" * len(values), *(_f32(v) for v in values)))

    words(len(vertices), len(indices), len(bones), len(clips), logical_size, 0)
    for position, normal, uv, joints, weights in vertices:
        p = _values(position, 3, "position")
        n = _values(normal, 3, "normal")
        uv = _values(uv, 2, "UV")
        weights = _values(weights, 4, "weights")
        if len(joints) != 4:
            raise ValueError("Invalid GSR1 joints length")
        if abs(sum(v * v for v in n) - 1) >= TOLERANCE or any(v < 0 or v > 1 for v in uv):
            raise ValueError("Invalid GSR1 normal or UV")
        if (
            any(b < 0 or b >= len(bones) for b in joints)
            or any(w < 0 or w > 1 for w in weights)
            or abs(sum(weights) - 1) >= TOLERANCE
        ):
            raise ValueError("Invalid GSR1 influences")
        floats(*p, *n, *uv)
        words(*joints)
        floats(*weights)
    if any(i < 0 or i >= len(vertices) for i in indices):
        raise ValueError("Invalid triangle index")
    words(*indices)
    encoded_bones, global_rest = [], []
    for i, (parent, rest, inverse) in enumerate(bones):
        if parent != NO_PARENT and not 0 <= parent < i:
            raise ValueError("Skeleton must be parent ordered")
        rest, inverse = transform(rest), transform(inverse)
        world = _matrix(rest)
        if parent != NO_PARENT:
            world = _multiply(global_rest[parent], world)
        identity = _multiply(world, _matrix(inverse))
        if any(abs(value - (1 if j % 5 == 0 else 0)) >= 0.002 for j, value in enumerate(identity)):
            raise ValueError("Invalid GSR1 inverse bind")
        global_rest.append(world)
        encoded_bones.append((parent, rest, inverse))
        words(parent)
        floats(*rest, *inverse)
    clip_ids = set()
    for clip in clips:
        samples = clip["tracks"]
        if (
            not 1 <= len(samples) <= 256
            or any(len(s) != len(bones) for s in samples)
            or len(clip["frames"]) != 256
        ):
            raise ValueError("Incomplete rig tracks or frame mapping")
        if clip["id"] in clip_ids:
            raise ValueError("Duplicate GSR1 clip")
        clip_ids.add(clip["id"])
        duration = _f32(clip["duration"])
        if duration < MINIMUM_DURATION:
            raise ValueError("Invalid GSR1 duration")
        words(clip["id"], len(samples))
        floats(duration, *_camera(clip))
        for frame in clip["frames"]:
            heading, time = _values(frame, 2, "frame mapping")
            if abs(heading) > MAXIMUM_HEADING or not 0 <= time < duration:
                raise ValueError("Invalid GSR1 frame mapping")
            floats(heading, time)
        encoded_samples = [[transform(local) for local in sample] for sample in samples]
        _validate_scales(encoded_samples, encoded_bones)
        for sample in encoded_samples:
            for local in sample:
                floats(*local)
    struct.pack_into("<I", data, 24, len(data) - 28)
    return bytes(data)


def provenance(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_candidate(output, name, data, sources, clips):
    output.mkdir(parents=True, exist_ok=True)
    (output / (name + ".gsr")).write_bytes(data)
    root = Path(__file__).resolve().parents[2]
    record = {
        "format": "GSR1",
        "version": 1,
        "experimental": True,
        "file": name + ".gsr",
        "sha256": hashlib.sha256(data).hexdigest(),
        "clips": clips,
        "sources": {str(Path(p).resolve().relative_to(root)): provenance(p) for p in sources},
    }
    (output / (name + "-rig.json")).write_text(json.dumps(record, indent=2) + "\n")
