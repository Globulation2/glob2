#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit GSB1 blend-shape models to the baked worker and warrior clips.

Bone skinning cannot follow the baked clips where limbs meet the torso: each
frame re-solves the surface so a limb's first rings swell into the shoulder or
hip lobe while the torso sheet retreats, and a fixed-weight skin leaves a seam
where the two cross. A blend-shape basis has no such limit. Per model, every
baked frame of every clip (heading undone) is decomposed into a mean mesh plus
K shape vectors by principal component analysis; each frame is then its K
coefficients; the baked normals get a smaller basis of their own, so shading
matches the baked clips' analytic field normals rather than the geometry's.
The runtime evaluates mean + sum(c_k * shape_k) for positions and normals,
turns the result by the frame's heading and applies the clip camera,
producing exactly the per-pose layout the baked clips use.

Needs only NumPy (run under Blender's Python if the system one lacks it):
  blender --background --python tools/skins/fit_unit_shapes.py -- \
    --model worker --output artifacts/shapes/worker
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
from rig_scene import ROOT, baked_frames

CLIPS = {"worker": ("walk", "swim", "harvest"), "warrior": ("walk", "swim", "fight")}
DEFAULT_SHAPES = 32
DEFAULT_NORMAL_SHAPES = 24
MAXIMUM_SHAPES = 128
MAGIC = b"GSB1"


def pca(frames, shapes):
    """Mean and the ``shapes`` leading components of the frame matrix.

    Returns (mean (V,3), basis (K,V,3), coefficients (F,K)). The basis rows are
    unit vectors ordered by variance; coefficients carry the magnitudes.
    """
    count = frames.shape[0]
    matrix = frames.reshape(count, -1)
    mean = matrix.mean(axis=0)
    u, s, vt = np.linalg.svd(matrix - mean, full_matrices=False)
    basis = vt[:shapes]
    coefficients = u[:, :shapes] * s[:shapes]
    # Fix each component's sign so a rerun on the same data is byte-identical.
    sign = np.sign(basis[:, 0] + 1e-12 * (basis[:, 1] + 1e-12))
    sign[sign == 0] = 1
    return mean.reshape(-1, 3), (basis * sign[:, None]).reshape(shapes, -1, 3), coefficients * sign


def quantise(basis):
    """Per-shape scale and int16 deltas; returns (scales, deltas, rounded basis)."""
    extent = np.abs(basis).reshape(len(basis), -1).max(axis=1)
    scales = np.maximum(extent, 1e-12) / 32767.0
    deltas = np.round(basis / scales[:, None, None]).astype(np.int16)
    return scales.astype(np.float32), deltas, deltas.astype(np.float64) * scales[:, None, None]


def welded_normals(positions, triangles):
    """Area-weighted vertex normals of a welded mesh, unit length."""
    face = np.cross(
        positions[:, triangles[:, 1]] - positions[:, triangles[:, 0]],
        positions[:, triangles[:, 2]] - positions[:, triangles[:, 0]],
    )
    normals = np.zeros_like(positions)
    for corner in range(3):
        np.add.at(normals, (slice(None), triangles[:, corner]), face)
    return normals / np.maximum(np.linalg.norm(normals, axis=2, keepdims=True), 1e-12)


def encode(uv, indices, mean, scales, deltas, normal_mean, normal_scales, normal_deltas, clips, logical_size):
    """Serialise a GSB1 asset. ``clips`` are dicts with id, modelToClip (16),
    normalToCamera (9), headings (256), coefficients (256, K) and
    normalCoefficients (256, Kn)."""
    vertices, shapes, normal_shapes = len(mean), len(deltas), len(normal_deltas)
    if not (3 <= vertices <= 8192 and 3 <= len(indices) <= 49152 and len(indices) % 3 == 0):
        raise ValueError("GSB1 dimensions exceed v1 limits")
    if not (1 <= shapes <= MAXIMUM_SHAPES and 1 <= normal_shapes <= MAXIMUM_SHAPES):
        raise ValueError("GSB1 dimensions exceed v1 limits")
    if not (1 <= len(clips) <= 8 and 1 <= logical_size <= 128):
        raise ValueError("GSB1 dimensions exceed v1 limits")
    data = bytearray(MAGIC)
    data += struct.pack("<7I", vertices, len(indices), shapes, normal_shapes, len(clips), logical_size, 0)
    uv = np.asarray(uv, dtype="<f4")
    if uv.shape != (vertices, 2) or np.any(uv < 0) or np.any(uv > 1):
        raise ValueError("Invalid GSB1 UVs")
    data += uv.tobytes()
    indices = np.asarray(indices, dtype="<u4")
    if np.any(indices >= vertices):
        raise ValueError("Invalid triangle index")
    data += indices.tobytes()
    data += np.asarray(mean, dtype="<f4").tobytes()
    data += np.asarray(normal_mean, dtype="<f4").tobytes()
    for scale, delta in list(zip(scales, deltas)) + list(zip(normal_scales, normal_deltas)):
        if not np.isfinite(scale) or scale <= 0:
            raise ValueError("Invalid GSB1 shape scale")
        data += struct.pack("<f", float(scale))
        data += np.asarray(delta, dtype="<i2").tobytes()
    seen = set()
    for clip in clips:
        if clip["id"] in seen:
            raise ValueError("Duplicate GSB1 clip")
        seen.add(clip["id"])
        matrix = np.asarray(clip["modelToClip"], dtype="<f4")
        normal = np.asarray(clip["normalToCamera"], dtype="<f4")
        headings = np.asarray(clip["headings"], dtype="<f4")
        coefficients = np.asarray(clip["coefficients"], dtype="<f4")
        normal_coefficients = np.asarray(clip["normalCoefficients"], dtype="<f4")
        if matrix.shape != (16,) or normal.shape != (9,) or headings.shape != (256,):
            raise ValueError("Invalid GSB1 clip camera or headings")
        if coefficients.shape != (256, shapes) or not np.all(np.isfinite(coefficients)):
            raise ValueError("Invalid GSB1 coefficients")
        if normal_coefficients.shape != (256, normal_shapes) or not np.all(np.isfinite(normal_coefficients)):
            raise ValueError("Invalid GSB1 normal coefficients")
        if np.any(np.abs(headings) > 6.283186) or np.any(np.abs(coefficients) > 10000):
            raise ValueError("GSB1 clip values out of range")
        if np.any(np.abs(normal_coefficients) > 10000):
            raise ValueError("GSB1 clip values out of range")
        data += struct.pack("<2I", int(clip["id"]), 256)
        data += matrix.tobytes() + normal.tobytes() + headings.tobytes()
        data += coefficients.tobytes() + normal_coefficients.tobytes()
    struct.pack_into("<I", data, 28, len(data) - 32)
    return bytes(data)


def evaluate(mean, basis, coefficients, normal_mean, normal_basis, normal_coefficients, heading, model_to_clip, normal_to_camera):
    """Reference evaluation of one frame into the baked per-pose layout."""
    positions = mean + np.tensordot(coefficients, basis, axes=1)
    normals = normal_mean + np.tensordot(normal_coefficients, normal_basis, axes=1)
    turn = np.array(
        [[math.cos(heading), -math.sin(heading), 0], [math.sin(heading), math.cos(heading), 0], [0, 0, 1]]
    )
    positions = positions @ turn.T
    normals = normals @ turn.T
    homogeneous = np.concatenate((positions, np.ones((len(positions), 1))), axis=1)
    clip_positions = (homogeneous @ np.asarray(model_to_clip).reshape(4, 4).T)[:, :3]
    clip_normals = normals @ np.asarray(normal_to_camera).reshape(3, 3).T
    lengths = np.linalg.norm(clip_normals, axis=1, keepdims=True)
    clip_normals = np.where(lengths < 1e-8, np.array([[0.0, 0.0, 1.0]]), clip_normals / np.maximum(lengths, 1e-12))
    return np.concatenate((clip_positions, clip_normals), axis=1)


def baked_normals(model, clip, loaded):
    """Baked camera-space normals back in heading-free model space."""
    path, _, _, _, _, view = loaded
    data = path.read_bytes()
    count, index_count = struct.unpack_from("<2I", data, 4)
    poses = np.frombuffer(data, "<f4", count * 6 * 256, 20 + count * 8 + index_count * 4)
    normals = poses.reshape(256, count, 6)[:, :, 3:].astype(np.float64)
    to_model = np.array(view["normalToModel"]).reshape(3, 3)
    normals = normals @ to_model.T
    for frame in range(256):
        a = (frame // 32) * math.pi / 4
        turn = np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])
        normals[frame] = normals[frame] @ turn.T
    return normals


def fit(model, shapes=DEFAULT_SHAPES, normal_shapes=DEFAULT_NORMAL_SHAPES):
    clips = CLIPS[model]
    loaded = {clip: baked_frames(model, clip) for clip in clips}
    first = loaded[clips[0]]
    uv, indices, size = first[1], first[2], first[4]
    for clip in clips[1:]:
        if not np.array_equal(loaded[clip][1], uv) or not np.array_equal(loaded[clip][2], indices):
            raise ValueError(f"{model} clips do not share one paint topology")
    frames = np.concatenate([loaded[clip][3] for clip in clips])
    mean, basis, coefficients = pca(frames, shapes)
    scales, deltas, rounded = quantise(basis)
    normals = np.concatenate([baked_normals(model, clip, loaded[clip]) for clip in clips])
    normal_mean, normal_basis, normal_coefficients = pca(normals, normal_shapes)
    normal_scales, normal_deltas, normal_rounded = quantise(normal_basis)
    reports, records = {}, []
    for index, clip in enumerate(clips):
        view = loaded[clip][5]
        model_to_clip = view["modelToClip"]
        normal_to_camera = np.array(view["normalToModel"]).reshape(3, 3).T.flatten().tolist()
        headings = [-(f // 32) * math.pi / 4 for f in range(256)]
        c = coefficients[index * 256 : (index + 1) * 256].astype(np.float32)
        cn = normal_coefficients[index * 256 : (index + 1) * 256].astype(np.float32)
        records.append(
            {
                "id": index,
                "modelToClip": model_to_clip,
                "normalToCamera": normal_to_camera,
                "headings": headings,
                "coefficients": c,
                "normalCoefficients": cn,
            }
        )
        # Distance to the baked frames after quantisation, in model space, and
        # the angle between rebuilt and baked normals.
        rebuilt = mean + np.tensordot(c.astype(np.float64), rounded, axes=1)
        distance = np.linalg.norm(rebuilt - loaded[clip][3], axis=2)
        rebuilt_normals = normal_mean + np.tensordot(cn.astype(np.float64), normal_rounded, axes=1)
        rebuilt_normals /= np.maximum(np.linalg.norm(rebuilt_normals, axis=2, keepdims=True), 1e-12)
        angles = np.degrees(
            np.arccos(np.clip((rebuilt_normals * normals[index * 256 : (index + 1) * 256]).sum(axis=2), -1, 1))
        )
        reports[clip] = {
            "rms": float(np.sqrt(np.mean(distance**2))),
            "p95": float(np.quantile(distance, 0.95)),
            "max": float(distance.max()),
            "worstFrame": int(distance.mean(axis=1).argmax()),
            "normalMedianDegrees": float(np.median(angles)),
            "normalP99Degrees": float(np.quantile(angles, 0.99)),
        }
    return {
        "uv": uv,
        "indices": indices,
        "size": size,
        "mean": mean,
        "scales": scales,
        "deltas": deltas,
        "basis": rounded,
        "normalMean": normal_mean,
        "normalScales": normal_scales,
        "normalDeltas": normal_deltas,
        "normalBasis": normal_rounded,
        "clips": records,
        "reports": reports,
        "sources": [
            path
            for clip in clips
            for path in (loaded[clip][0], loaded[clip][0].with_suffix(".view.json"))
        ],
    }


def author(model, output, shapes=DEFAULT_SHAPES, normal_shapes=DEFAULT_NORMAL_SHAPES):
    result = fit(model, shapes, normal_shapes)
    output.mkdir(parents=True, exist_ok=True)
    dependencies = [Path(__file__), Path(__file__).with_name("rig_scene.py")] + result["sources"]
    for index, clip in enumerate(CLIPS[model]):
        data = encode(
            result["uv"],
            result["indices"].ravel(),
            result["mean"],
            result["scales"],
            result["deltas"],
            result["normalMean"],
            result["normalScales"],
            result["normalDeltas"],
            [result["clips"][index]],
            result["size"],
        )
        name = f"{model}-{clip}"
        (output / f"{name}.gsb").write_bytes(data)
        record = {
            "format": "GSB1",
            "version": 1,
            "experimental": True,
            "file": f"{name}.gsb",
            "sha256": hashlib.sha256(data).hexdigest(),
            "clips": [
                {"id": index, "name": clip, "frames": 256, "shapes": int(shapes), "normalShapes": int(normal_shapes)}
            ],
            "sources": {
                str(Path(p).resolve().relative_to(ROOT)): hashlib.sha256(Path(p).read_bytes()).hexdigest()
                for p in dependencies
            },
        }
        (output / f"{name}-shapes.json").write_text(json.dumps(record, indent=2) + "\n")
        report = {
            "model": model,
            "clip": clip,
            "shapes": int(shapes),
            "normalShapes": int(normal_shapes),
            "frames": result["reports"][clip],
        }
        (output / f"{name}-fit.json").write_text(json.dumps(report, indent=2) + "\n")
        frames = result["reports"][clip]
        print(
            f"{name}: {len(result['mean'])} vertices, {shapes}+{normal_shapes} shapes, {len(data)} bytes, "
            f"rms {frames['rms']:.3f} p95 {frames['p95']:.3f} max {frames['max']:.3f} "
            f"normals median {frames['normalMedianDegrees']:.1f} p99 {frames['normalP99Degrees']:.1f} deg"
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", choices=CLIPS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--shapes", type=int, default=DEFAULT_SHAPES)
    parser.add_argument("--normal-shapes", type=int, default=DEFAULT_NORMAL_SHAPES)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else sys.argv[1:])
    author(args.model, args.output, args.shapes, args.normal_shapes)
