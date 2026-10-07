#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Author the explorer GSR1 candidate and its editable Blender scene.

The baked explorer clip is already a four-influence skin over its ellipsoid
components, so the rig keeps that geometry: the rest mesh is the baked first
pose, welded for one smoothing pass and normal calculation, and the body, head
and wing bones follow the source transforms directly. Ellipsoid proportions live
in the rest mesh rather than nonuniform bone scales.

Run in Blender 3.6.23:
  blender --background --factory-startup -t 1 --python-exit-code 1 \
    --python tools/skins/author_explorer_rig.py -- --output artifacts/rig/explorer
"""

import argparse
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import numpy as np
from mathutils import Matrix, Vector
from export_units import open_source, components, sample_pose
from export_rig import NO_PARENT, encode, write_candidate
from rig_scene import (
    baked_frames,
    check_action_source,
    clip_descriptor,
    clip_record,
    create_scene,
    import_action,
    install_action,
)

MODEL = "explorer"
CLIP = "fly"


def smooth_surface(positions, triangles, iterations=12):
    """Nonshrinking two-pass fairing on the welded graph."""
    adjacency = [set() for _ in positions]
    for triangle in triangles:
        for v in triangle:
            adjacency[v].update(int(k) for k in triangle if k != v)
    rows = np.array([v for v, ns in enumerate(adjacency) for _ in ns])
    cols = np.array([k for ns in adjacency for k in sorted(ns)])
    degree = np.bincount(rows)[:, None]
    positions = np.array(positions, dtype=float, copy=True)
    for _ in range(iterations):
        for rate in (0.5, -0.53):
            mean = np.zeros_like(positions)
            np.add.at(mean, rows, positions[cols])
            positions += rate * (mean / degree - positions)
    return positions


def vertex_normals(positions, triangles):
    face = np.cross(
        positions[triangles[:, 1]] - positions[triangles[:, 0]],
        positions[triangles[:, 2]] - positions[triangles[:, 0]],
    )
    normals = np.zeros_like(positions)
    for corner in triangles.T:
        np.add.at(normals, corner, face)
    lengths = np.linalg.norm(normals, axis=1)
    if np.any(lengths < 1e-10):
        raise ValueError("Degenerate explorer surface normal")
    return normals / lengths[:, None]


def ease(x, lo, hi):
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def pack_weights(dense):
    if dense.min() < -1e-8 or np.any((dense > 1e-10).sum(axis=1) > 4):
        raise ValueError("Influence support exceeds four bones")
    dense = np.maximum(dense, 0)
    result = []
    for row in dense:
        joints = np.argsort(-row, kind="stable")[:4].tolist()
        while len(joints) < 4:
            joints.append(0)
        weights = row[joints]
        weights /= weights.sum()
        result.append((joints, weights.tolist()))
    return result


def rest_surface():
    path, uv, indices, frames, size, view = baked_frames(MODEL, CLIP)
    positions = frames[0]
    # Weld UV splits for fairing and normals, then map back to unchanged
    # serialized vertices. Otherwise a paint seam becomes a shading seam.
    welded, inverse = np.unique(positions, axis=0, return_inverse=True)
    faces = inverse[indices]
    welded = smooth_surface(welded, faces, iterations=1)
    normals = vertex_normals(welded, faces)[inverse]
    positions = welded[inverse]
    bones = [
        ("body", -1, np.zeros(3), np.array((1, 0, 0))),
        ("head", 0, np.array((-2.0, 0, 0)), np.array((-3.0, 0, 0))),
        ("wing.R", 0, np.array((0, 0.8, 0)), np.array((0, 5.0, 0))),
        ("wing.L", 0, np.array((0, -0.8, 0)), np.array((0, -5.0, 0))),
    ]
    right = ease(positions[:, 1], 0.55, 1.35)
    left = ease(-positions[:, 1], 0.55, 1.35)
    core = 1 - right - left
    head = ease(-positions[:, 0], 1.8, 3.2) * core
    dense = np.column_stack((core - head, head, right, left))
    return path, view, size, positions, normals, uv, indices, pack_weights(dense), bones


def reference_motion():
    source, scene = open_source(MODEL, CLIP)
    parts = components(scene)
    origin = (
        float(scene.objects["RotEmpty"].rotation_euler.z)
        if scene.objects.get("RotEmpty")
        else 0
    )
    motion = []
    for sample in range(64):
        direction, phase = divmod(sample, 32)
        sample_pose(scene, direction, phase, origin)
        undo = Matrix.Rotation(direction * math.pi / 4, 4, "Z")
        # Object orientation excludes ellipsoid shape axes: those are already
        # baked into the rest geometry, not legal bone scales.
        motion.append([undo @ p.matrix_world.copy() for p in parts])
    return source, motion


def retarget(motion, rest, bones):
    reference = motion[0]
    result = []
    for parts in motion:
        rotation = parts[0].to_quaternion() @ reference[0].to_quaternion().conjugated()
        root_head = parts[0].translation - reference[0].translation
        world = [
            Matrix.Translation(root_head)
            @ rotation.to_matrix().to_4x4()
            @ rest[0].to_quaternion().to_matrix().to_4x4()
        ]
        for bone, part in [(1, 3), (2, 1), (3, 2)]:
            delta = (
                rotation
                if bone == 1
                else parts[part].to_quaternion() @ reference[part].to_quaternion().conjugated()
            )
            head = world[0] @ rest[0].inverted() @ Vector(bones[bone][2])
            world.append(
                Matrix.Translation(head)
                @ delta.to_matrix().to_4x4()
                @ rest[bone].to_quaternion().to_matrix().to_4x4()
            )
        result.append(
            [
                world[i] if b[1] < 0 else world[b[1]].inverted() @ world[i]
                for i, b in enumerate(bones)
            ]
        )
    return result


def author(output, action_blend=None, action_name=None):
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError("Use Blender 3.6.23")
    if action_blend:
        action_blend = check_action_source(action_blend, output, MODEL)
        if not action_name:
            raise ValueError("Action import requires --action")
    elif action_name:
        raise ValueError("--action requires --action-blend")
    asset, view, size, positions, normals, uv, indices, influences, bones = rest_surface()
    # Sample the source before creating the output scene: opening it replaces
    # the current file.
    source, motion = (None, None) if action_blend else reference_motion()
    scene, rig, rest = create_scene(MODEL, positions, uv, indices, influences, bones)
    encoded_bones = [
        (
            parent if parent >= 0 else NO_PARENT,
            rest[i] if parent < 0 else rest[parent].inverted() @ rest[i],
            rest[i].inverted(),
        )
        for i, (_, parent, *_) in enumerate(bones)
    ]
    vertices = [
        (positions[i], normals[i], uv[i], *influences[i]) for i in range(len(positions))
    ]
    sources = [
        Path(__file__),
        Path(__file__).with_name("rig_scene.py"),
        Path(__file__).with_name("export_rig.py"),
        Path(__file__).with_name("export_units.py"),
        asset,
        asset.with_suffix(".view.json"),
    ]
    if action_blend:
        action, tracks = import_action(scene, rig, bones, action_blend, action_name)
        sources.append(action_blend)
    else:
        sources.append(source)
        tracks = retarget(motion, rest, bones)
        action = install_action(rig, rest, bones, tracks, CLIP.title())
    data = encode(vertices, indices.ravel().tolist(), encoded_bones, [clip_record(0, view, tracks)], size)
    output.mkdir(parents=True, exist_ok=True)
    write_candidate(
        output,
        f"{MODEL}-{CLIP}",
        data,
        sources,
        [{"id": 0, "name": action_name if action_blend else CLIP, **clip_descriptor(len(tracks))}],
    )
    print(f"{MODEL}-{CLIP}: {len(positions)} vertices, {len(bones)} bones, {len(data)} bytes")
    rig.animation_data.action = action
    scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str((output / (MODEL + ".blend")).resolve()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--action-blend", type=Path, help="Import a 64-frame quaternion action")
    parser.add_argument("--action", help="Action to import; exported as the fly preview")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    author(args.output, args.action_blend, args.action)
