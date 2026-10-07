#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit GSB1 blend-shape clips to the baked worker and warrior clips.

A blend shape is a stored offset for every vertex; a pose is the mean mesh
plus a weighted sum of such offsets. Per model, every baked frame of every
clip (heading undone) is decomposed by principal component analysis into a
mean mesh plus K shape vectors ordered by how much variation they explain,
and each frame becomes its K coefficients. The baked normals get a smaller
basis of their own, so shading matches the baked clips' analytic field
normals rather than the geometry's. The runtime evaluates
mean + sum(c_k * shape_k) for positions and normals, turns the result by the
frame's heading and applies the clip camera, producing the per-pose layout
the baked clips use.

Needs only NumPy (run under Blender's Python if the system one lacks it):
  blender --background --python tools/skins/fit_unit_shapes.py -- \\
    --model worker --output artifacts/shapes/worker
"""

import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
from skin_assets import (
    FRAMES,
    GSB_HEADER,
    MAX_CLIPS,
    MAX_HEADING,
    MAX_INDICES,
    MAX_LOGICAL_SIZE,
    MAX_SCALAR,
    MAX_SHAPES,
    MAX_VERTICES,
    UNIT_CLIPS,
    baked_clip,
    heading_of_frame,
    normal_to_camera,
    rotation_z,
    script_arguments,
    write_candidate,
    write_json,
)

MODELS = {model: clips for model, clips in UNIT_CLIPS.items() if model != "explorer"}
DEFAULT_SHAPES = 32
DEFAULT_NORMAL_SHAPES = 24
MAGIC = b"GSB1"
# Sources whose change requires a refit; the installer checks their digests.
SOURCES = [Path(__file__), Path(__file__).with_name("skin_assets.py")]


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


def quantize(basis):
    """Per-shape scale and int16 deltas; returns (scales, deltas, rounded basis)."""
    extent = np.abs(basis).reshape(len(basis), -1).max(axis=1)
    scales = np.maximum(extent, 1e-12) / 32767.0
    deltas = np.round(basis / scales[:, None, None]).astype(np.int16)
    return scales.astype(np.float32), deltas, deltas.astype(np.float64) * scales[:, None, None]


def encode(uv, indices, mean, scales, deltas, normal_mean, normal_scales, normal_deltas, clips, logical_size):
    """Serialize a GSB1 asset.

    ``clips`` are dicts with id, modelToClip (16), normalToCamera (9),
    headings (256), coefficients (256, K) and normalCoefficients (256, Kn).
    """
    vertices, shapes, normal_shapes = len(mean), len(deltas), len(normal_deltas)
    if not (3 <= vertices <= MAX_VERTICES and 3 <= len(indices) <= MAX_INDICES and len(indices) % 3 == 0):
        raise ValueError("GSB1 dimensions exceed v1 limits")
    if not (1 <= shapes <= MAX_SHAPES and 1 <= normal_shapes <= MAX_SHAPES):
        raise ValueError("GSB1 dimensions exceed v1 limits")
    if not (1 <= len(clips) <= MAX_CLIPS and 1 <= logical_size <= MAX_LOGICAL_SIZE):
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
        if matrix.shape != (16,) or normal.shape != (9,) or headings.shape != (FRAMES,):
            raise ValueError("Invalid GSB1 clip camera or headings")
        if coefficients.shape != (FRAMES, shapes) or not np.all(np.isfinite(coefficients)):
            raise ValueError("Invalid GSB1 coefficients")
        if normal_coefficients.shape != (FRAMES, normal_shapes) or not np.all(np.isfinite(normal_coefficients)):
            raise ValueError("Invalid GSB1 normal coefficients")
        if np.any(np.abs(headings) > MAX_HEADING) or np.any(np.abs(coefficients) > MAX_SCALAR):
            raise ValueError("GSB1 clip values out of range")
        if np.any(np.abs(normal_coefficients) > MAX_SCALAR):
            raise ValueError("GSB1 clip values out of range")
        data += struct.pack("<2I", int(clip["id"]), FRAMES)
        data += matrix.tobytes() + normal.tobytes() + headings.tobytes()
        data += coefficients.tobytes() + normal_coefficients.tobytes()
    struct.pack_into("<I", data, GSB_HEADER - 4, len(data) - GSB_HEADER)
    return bytes(data)


def evaluate(mean, basis, coefficients, normal_mean, normal_basis, normal_coefficients, heading, model_to_clip, camera_normals):
    """Reference evaluation of one frame into the baked per-pose layout."""
    positions = mean + np.tensordot(coefficients, basis, axes=1)
    normals = normal_mean + np.tensordot(normal_coefficients, normal_basis, axes=1)
    turn = rotation_z(heading)
    positions = positions @ turn.T
    normals = normals @ turn.T
    homogeneous = np.concatenate((positions, np.ones((len(positions), 1))), axis=1)
    clip_positions = (homogeneous @ np.asarray(model_to_clip).reshape(4, 4).T)[:, :3]
    clip_normals = normals @ np.asarray(camera_normals).reshape(3, 3).T
    lengths = np.linalg.norm(clip_normals, axis=1, keepdims=True)
    clip_normals = np.where(lengths < 1e-8, np.array([[0.0, 0.0, 1.0]]), clip_normals / np.maximum(lengths, 1e-12))
    return np.concatenate((clip_positions, clip_normals), axis=1)


def fit(model, shapes=DEFAULT_SHAPES, normal_shapes=DEFAULT_NORMAL_SHAPES):
    """Fit one model's shared basis to all of its baked clips.

    Returns the geometry, quantized bases, one coefficient record per clip and
    a fidelity report per clip: distance to the baked frames after
    quantization, in model units, and the angle between rebuilt and baked
    normals.
    """
    clips = MODELS[model]
    loaded = {clip: baked_clip(model, clip) for clip in clips}
    first = loaded[clips[0]]
    for clip in clips[1:]:
        if not np.array_equal(loaded[clip].uv, first.uv) or not np.array_equal(loaded[clip].indices, first.indices):
            raise ValueError(f"{model} clips do not share one paint topology")
    frames = np.concatenate([loaded[clip].positions for clip in clips])
    mean, basis, coefficients = pca(frames, shapes)
    scales, deltas, rounded = quantize(basis)
    normals = np.concatenate([loaded[clip].normals for clip in clips])
    normal_mean, normal_basis, normal_coefficients = pca(normals, normal_shapes)
    normal_scales, normal_deltas, normal_rounded = quantize(normal_basis)
    reports, records = {}, []
    for index, clip in enumerate(clips):
        view = loaded[clip].view
        c = coefficients[index * FRAMES : (index + 1) * FRAMES].astype(np.float32)
        cn = normal_coefficients[index * FRAMES : (index + 1) * FRAMES].astype(np.float32)
        records.append(
            {
                "id": index,
                "modelToClip": view["modelToClip"],
                "normalToCamera": normal_to_camera(view),
                "headings": [heading_of_frame(f) for f in range(FRAMES)],
                "coefficients": c,
                "normalCoefficients": cn,
            }
        )
        rebuilt = mean + np.tensordot(c.astype(np.float64), rounded, axes=1)
        distance = np.linalg.norm(rebuilt - loaded[clip].positions, axis=2)
        rebuilt_normals = normal_mean + np.tensordot(cn.astype(np.float64), normal_rounded, axes=1)
        rebuilt_normals /= np.maximum(np.linalg.norm(rebuilt_normals, axis=2, keepdims=True), 1e-12)
        angles = np.degrees(np.arccos(np.clip((rebuilt_normals * loaded[clip].normals).sum(axis=2), -1, 1)))
        reports[clip] = {
            "rms": float(np.sqrt(np.mean(distance**2))),
            "p95": float(np.quantile(distance, 0.95)),
            "max": float(distance.max()),
            "worstFrame": int(distance.mean(axis=1).argmax()),
            "normalMedianDegrees": float(np.median(angles)),
            "normalP99Degrees": float(np.quantile(angles, 0.99)),
        }
    return {
        "uv": first.uv,
        "indices": first.indices,
        "size": first.size,
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
        "sources": [path for clip in clips for path in (loaded[clip].path, loaded[clip].path.with_suffix(".view.json"))],
    }


def author(model, output, shapes=DEFAULT_SHAPES, normal_shapes=DEFAULT_NORMAL_SHAPES, fit_result=None):
    """Stage one GSB1 asset, candidate record and fit report per clip of ``model``."""
    result = fit_result or fit(model, shapes, normal_shapes)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    for index, clip in enumerate(MODELS[model]):
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
        write_candidate(
            output,
            name,
            data,
            SOURCES + result["sources"],
            [{"id": index, "name": clip, "frames": FRAMES, "shapes": int(shapes), "normalShapes": int(normal_shapes)}],
            "GSB1",
        )
        frames = result["reports"][clip]
        write_json(
            output / f"{name}-fit.json",
            {"model": model, "clip": clip, "shapes": int(shapes), "normalShapes": int(normal_shapes), "frames": frames},
        )
        print(
            f"{name}: {len(result['mean'])} vertices, {shapes}+{normal_shapes} shapes, {len(data)} bytes, "
            f"rms {frames['rms']:.3f} p95 {frames['p95']:.3f} max {frames['max']:.3f} "
            f"normals median {frames['normalMedianDegrees']:.1f} p99 {frames['normalP99Degrees']:.1f} deg"
        )
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", choices=MODELS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--shapes", type=int, default=DEFAULT_SHAPES)
    parser.add_argument("--normal-shapes", type=int, default=DEFAULT_NORMAL_SHAPES)
    args = parser.parse_args(script_arguments())
    author(args.model, args.output, args.shapes, args.normal_shapes)
