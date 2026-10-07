#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""GSR1 encoder for authored Blender rigs; no runtime surface rebuilding.

Transforms must be translation, quaternion rotation and positive uniform scale.
Every value is checked after rounding to binary32, so an authoring value the
production decoders would reject never reaches an asset.
"""

import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from skin_assets import (
    FRAMES,
    GSR_HEADER,
    MAX_BONES,
    MAX_CLIPS,
    MAX_HEADING,
    MAX_INDICES,
    MAX_LOGICAL_SIZE,
    MAX_SCALAR,
    MAX_VERTICES,
    f32,
    write_candidate as write_record,
)

NO_PARENT = 0xFFFFFFFF
TOLERANCE = 0.0001
MINIMUM_SCALE = f32(0.0001)
MINIMUM_DURATION = f32(0.0001)
MAX_SAMPLES = 256


def _values(values, count, label):
    if len(values) != count:
        raise ValueError(f"Invalid GSR1 {label} length")
    return [f32(value) for value in values]


def transform(matrix):
    """Serialized TRS of a Blender matrix, rejecting what the runtime cannot represent."""
    from mathutils import Matrix

    translation, rotation, scale = matrix.decompose()
    if min(scale) <= 0 or max(scale) - min(scale) > 1e-5 * max(scale):
        raise ValueError("GSR1 requires positive uniform scale")
    uniform = sum(scale) / 3
    rebuilt = Matrix.Translation(translation) @ rotation.to_matrix().to_4x4() @ Matrix.Scale(uniform, 4)
    if max(abs(matrix[y][x] - rebuilt[y][x]) for y in range(4) for x in range(4)) > 1e-4:
        raise ValueError("GSR1 cannot represent shear")
    result = _values([*translation, rotation.x, rotation.y, rotation.z, rotation.w, uniform], 8, "transform")
    if result[7] < MINIMUM_SCALE or abs(sum(v * v for v in result[3:7]) - 1) > TOLERANCE:
        raise ValueError("Invalid GSR1 transform")
    return result


def _matrix(trs):
    # Decoders normalize serialized quaternions and round the stored values
    # back to binary32 before checking bind transforms with binary64 arithmetic.
    norm = math.sqrt(sum(value * value for value in trs[3:7]))
    x, y, z, w = (f32(value / norm) for value in trs[3:7])
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
    radius = f32(clip["radius"])
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
    # The decoders bound every interpolated hierarchy, so the product of a
    # bone's scale range with its ancestors' and its inverse bind must stay
    # representable.
    lo, hi = [], []
    for bone, (parent, _, inverse) in enumerate(bones):
        scales = [sample[bone][7] for sample in samples]
        lo.append(min(scales) * (1 if parent == NO_PARENT else lo[parent]))
        hi.append(max(scales) * (1 if parent == NO_PARENT else hi[parent]))
        if (
            lo[bone] < MINIMUM_SCALE
            or hi[bone] > MAX_SCALAR
            or lo[bone] * inverse[7] < MINIMUM_SCALE
            or hi[bone] * inverse[7] > MAX_SCALAR
        ):
            raise ValueError("Unbounded GSR1 hierarchy or deformation scale")


def encode(vertices, indices, bones, clips, logical_size):
    """Serialize a GSR1 asset.

    ``vertices`` are ``(position, normal, uv, joints, weights)``; ``bones`` are
    ``(parent, rest, inverse_bind)`` Blender matrices in parent order; ``clips``
    are the dictionaries rig_scene.clip_record builds.
    """
    if (
        not 3 <= len(vertices) <= MAX_VERTICES
        or not 3 <= len(indices) <= MAX_INDICES
        or len(indices) % 3
        or not 1 <= len(bones) <= MAX_BONES
        or not 1 <= len(clips) <= MAX_CLIPS
        or not 1 <= logical_size <= MAX_LOGICAL_SIZE
    ):
        raise ValueError("GSR1 dimensions exceed v1 limits")
    data = bytearray(b"GSR1")

    def words(*values):
        if any(not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF for value in values):
            raise ValueError("Invalid GSR1 integer")
        data.extend(struct.pack("<" + "I" * len(values), *values))

    def floats(*values):
        data.extend(struct.pack("<" + "f" * len(values), *(f32(v) for v in values)))

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
            not 1 <= len(samples) <= MAX_SAMPLES
            or any(len(s) != len(bones) for s in samples)
            or len(clip["frames"]) != FRAMES
        ):
            raise ValueError("Incomplete rig tracks or frame mapping")
        if clip["id"] in clip_ids:
            raise ValueError("Duplicate GSR1 clip")
        clip_ids.add(clip["id"])
        duration = f32(clip["duration"])
        if duration < MINIMUM_DURATION:
            raise ValueError("Invalid GSR1 duration")
        words(clip["id"], len(samples))
        floats(duration, *_camera(clip))
        for frame in clip["frames"]:
            heading, time = _values(frame, 2, "frame mapping")
            if abs(heading) > MAX_HEADING or not 0 <= time < duration:
                raise ValueError("Invalid GSR1 frame mapping")
            floats(heading, time)
        encoded_samples = [[transform(local) for local in sample] for sample in samples]
        _validate_scales(encoded_samples, encoded_bones)
        for sample in encoded_samples:
            for local in sample:
                floats(*local)
    struct.pack_into("<I", data, GSR_HEADER - 4, len(data) - GSR_HEADER)
    return bytes(data)


def write_candidate(output, name, data, sources, clips):
    """Stage a GSR1 asset with the record install_rigs.py validates."""
    return write_record(output, name, data, sources, clips, "GSR1")
