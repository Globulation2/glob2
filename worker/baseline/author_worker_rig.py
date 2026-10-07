#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Author a clean worker-walk candidate in Blender 3.6.23.

Author a symmetric rest shape while preserving the shipped paint topology.
Retarget terminal paths onto straight, sliding lobe controls, never joint bends.
Outputs stay staged until visual and runtime acceptance; does not install assets.
Run: blender --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/author_worker_rig.py -- --output artifacts/rig/worker
"""

import argparse
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import numpy as np
from mathutils import Matrix, Vector, Quaternion
from export_units import open_source, components, influence_matrices, sample_pose
from export_rig import encode, write_candidate
from worker_surface import BODY_HALF_EXTENSION, build_surface, skin_weights
from limb_surface import LimbSurface

ROOT = Path(__file__).resolve().parents[2]


def torso_curl_angle(chest, reference_chest):
    """Gentle forward curl from the original hands' body-relative sweep.

    The shoulder anchors are fixed in the legacy scene; the terminal midpoint
    carries the gathering motion. A smooth bound avoids abrupt bend plateaus.
    """
    lean = math.atan2(chest.x, chest.z) - math.atan2(
        reference_chest.x, reference_chest.z
    )
    lean = math.atan2(math.sin(lean), math.cos(lean))
    return 0.08 * math.tanh(lean / 0.5)


def author(output, action_blend=None, action_name="Walk"):
    if (
        action_blend
        and action_blend.resolve() == (output / "worker-walk.blend").resolve()
    ):
        raise ValueError("Export edited actions to a different output directory")
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError("Use Blender 3.6.23")
    motion = []
    source = None
    if action_blend:
        # Provenance for staged exports is repository-relative, like all other
        # skin sources. Keep working copies in ignored artifacts/.
        try:
            action_blend.resolve().relative_to(ROOT)
        except ValueError as error:
            raise ValueError(
                "Keep the edited action source under this repository (for example artifacts/)"
            ) from error
    else:
        source, scene = open_source("worker", "walk")
        parts = components(scene)
        origin = float(scene.objects["RotEmpty"].rotation_euler.z)
        for sample in range(64):
            direction, phase = divmod(sample, 32)
            sample_pose(scene, direction, phase, origin)
            matrices = influence_matrices(parts)
            undo = np.array(Matrix.Rotation(direction * math.pi / 4, 4, "Z"))
            motion.append([Matrix(undo @ m) for m in matrices])
    definition_path = ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json"
    paths = json.loads(definition_path.read_text())["worker"]["paths"]
    asset = ROOT / "data/skins/colony-v1/worker-walk.gsk"
    view_path = asset.with_suffix(".view.json")
    view = json.loads(view_path.read_text())
    data = asset.read_bytes()
    _, count, index_count, _, size = struct.unpack_from("<4sIIII", data)
    uv = np.frombuffer(data, "<f4", count * 2, 20).reshape(-1, 2)
    indices = np.frombuffer(data, "<u4", index_count, 20 + count * 8)
    definition = json.loads(definition_path.read_text())["worker"]
    topology = LimbSurface(definition)
    if not np.array_equal(topology.triangles.ravel(), indices) or not np.array_equal(
        topology.uv.astype("<f4"), uv
    ):
        raise ValueError("Worker topology no longer matches the published paint chart")
    positions, normals, anchors = build_surface(definition)
    contract_path = asset.parent / "worker-surface.json"
    # Create a new scene; legacy objects are reference data only.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    armature = bpy.data.armatures.new("WorkerSkeleton")
    rig = bpy.data.objects.new("WorkerRig", armature)
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    root = armature.edit_bones.new("body")
    root.head = anchors[0]
    root.tail = root.head + Vector((0, 1, 0))
    parents = [0xFFFFFFFF]
    names = ["body"]
    centers = [(None, 0)]
    for limb, path in enumerate(paths):
        previous = 0
        for segment, part in enumerate(path):
            limb_name, side = (("arm", "R"), ("arm", "L"), ("leg", "R"), ("leg", "L"))[
                limb
            ]
            name = f"{limb_name}.{('attach', 'upper', 'lower')[segment]}.{side}"
            bone = armature.edit_bones.new(name)
            start = 0 if segment == 0 else path[segment - 1]
            bone.head = anchors[start]
            bone.tail = anchors[part]
            bone.parent = armature.edit_bones[names[previous]]
            bone.use_connect = False
            parents.append(previous)
            previous = len(names)
            names.append(name)
            centers.append((start, part))
    bpy.ops.object.mode_set(mode="OBJECT")
    rest = [armature.bones[name].matrix_local.copy() for name in names]
    # Retarget the original gait in its existing reference space, then move
    # each complete lobe to the corresponding end of the taller torso.
    motion_rest = [m.copy() for m in rest]
    offsets = [Vector((0, 0, 0))] + [
        Vector((0, 0, BODY_HALF_EXTENSION * (1 if i < 7 else -1))) for i in range(1, 13)
    ]
    bpy.ops.object.mode_set(mode="EDIT")
    for i, name in enumerate(names):
        armature.edit_bones[name].head += offsets[i]
        armature.edit_bones[name].tail += offsets[i]
    bpy.ops.object.mode_set(mode="OBJECT")
    rest = [armature.bones[name].matrix_local.copy() for name in names]
    mesh = bpy.data.meshes.new("WorkerRestSurface")
    mesh.from_pydata(positions.tolist(), [], indices.reshape(-1, 3).tolist())
    mesh.update()
    obj = bpy.data.objects.new("WorkerRestSurface", mesh)
    scene.collection.objects.link(obj)
    layer = mesh.uv_layers.new(name="PublishedPaint")
    for loop in mesh.loops:
        layer.data[loop.index].uv = (
            float(uv[loop.vertex_index, 0]),
            1 - float(uv[loop.vertex_index, 1]),
        )
    for poly in mesh.polygons:
        poly.use_smooth = True
    groups = [obj.vertex_groups.new(name=name) for name in names]
    influences = skin_weights(positions, anchors, paths)
    for v, (joints, weights) in enumerate(influences):
        for joint, weight in zip(joints, weights):
            if weight:
                groups[joint].add([v], float(weight), "REPLACE")
    modifier = obj.modifiers.new("Skin", "ARMATURE")
    modifier.object = rig
    rig.show_in_front = True
    rig.animation_data_create()
    rig.animation_data.action = bpy.data.actions.new("Walk")
    for i, name in enumerate(names):
        bone = rig.pose.bones[name]
        bone.rotation_mode = "QUATERNION"
        # Attach steers the whole lobe; the two child controls set its reach.
        # Rotating these children independently would recreate pinched joints.
        radial_child = i != 0 and (i - 1) % 3 != 0
        bone.lock_location = (True, False, True) if radial_child else (False,) * 3
        bone.lock_rotation = (radial_child,) * 3
        bone.lock_rotations_4d = radial_child
        bone.lock_rotation_w = radial_child
        bone.lock_scale = (True,) * 3
    previous_rotations = {}
    tracks = []
    for frame, parts in enumerate(motion):
        body_rotation = (
            parts[0].to_quaternion() @ motion[0][0].to_quaternion().conjugated()
        )
        body_scale = parts[0].to_scale().x / motion[0][0].to_scale().x
        world = [
            Matrix.Translation(parts[0].translation)
            @ body_rotation.to_matrix().to_4x4()
            @ Matrix.Scale(body_scale, 4)
        ]
        # All rotations derive from fresh rest axes and the body-relative path.
        for bone, (start, end) in enumerate(centers[1:], 1):
            rest_direction = Vector(anchors[end] - anchors[start])
            direction = parts[end].translation - parts[start].translation
            swing = (body_rotation @ rest_direction).rotation_difference(direction)
            parent = parents[bone]
            head = (
                world[parent] @ motion_rest[parent].inverted() @ Vector(anchors[start])
            )
            world.append(
                Matrix.Translation(head)
                @ swing.to_matrix().to_4x4()
                @ body_rotation.to_matrix().to_4x4()
                @ motion_rest[bone].to_quaternion().to_matrix().to_4x4()
            )
        # Preserve the existing gait's terminal targets, then replace the
        # independently bent chain by one shared direction. Clearance keeps
        # full-size terminal lobes from intersecting during the forward roll.
        root_head = parts[0].translation
        targets = []
        for branch, path in enumerate(paths):
            last = 3 + branch * 3
            targets.append(
                world[last] @ motion_rest[last].inverted() @ Vector(anchors[path[-1]])
            )
        for branch, path in enumerate(paths):
            outward = targets[branch] - root_head
            neutral = body_rotation @ Vector(anchors[path[-1]])
            swing = neutral.rotation_difference(outward)
            if swing.angle > 0.6:
                swing = Quaternion(swing.axis, 0.6)
                targets[branch] = (
                    root_head + swing @ neutral.normalized() * outward.length
                )
        for _ in range(12):
            for a in range(4):
                for b in range(a + 1, 4):
                    va, vb = targets[a] - root_head, targets[b] - root_head
                    angle = va.angle(vb)
                    if angle < math.radians(60):
                        axis = va.cross(vb).normalized()
                        correction = (math.radians(60) - angle) * 0.5
                        targets[a] = root_head + Quaternion(axis, -correction) @ va
                        targets[b] = root_head + Quaternion(axis, correction) @ vb
        for branch, path in enumerate(paths):
            neutral = Vector(anchors[path[-1]])
            outward = targets[branch] - root_head
            delta = (body_rotation @ neutral).rotation_difference(
                outward
            ) @ body_rotation
            delta.normalize()
            # Linear axial translation across the three controls preserves
            # each ring's cross-section and leaves the terminal cap rigid.
            shift = outward - delta @ neutral
            for bone in range(1 + branch * 3, 4 + branch * 3):
                start, _ = centers[bone]
                head = root_head + delta @ Vector(anchors[start])
                head += shift * ((bone - 1 - branch * 3) / 2)
                world[bone] = (
                    Matrix.Translation(head)
                    @ delta.to_matrix().to_4x4()
                    @ motion_rest[bone].to_quaternion().to_matrix().to_4x4()
                )
        for bone in range(1, 13):
            world[bone].translation += body_rotation @ offsets[bone]
        chest = body_rotation.inverted() @ (
            (parts[6].translation + parts[3].translation) * 0.5 - root_head
        )
        reference_chest = (
            motion[0][6].translation + motion[0][3].translation
        ) * 0.5 - motion[0][0].translation
        flex = torso_curl_angle(chest, reference_chest)
        # Bow both ends forward through the waist. Transform complete lobes
        # together, preserving their shared orientation and rigid end caps.
        for first, angle in [(1, flex), (7, -flex)]:
            bend_rotation = (
                body_rotation
                @ Quaternion((0, 1, 0), angle)
                @ body_rotation.conjugated()
            )
            bend_matrix = (
                Matrix.Translation(root_head)
                @ bend_rotation.to_matrix().to_4x4()
                @ Matrix.Translation(-root_head)
            )
            for bone in range(first, first + 6):
                world[bone] = bend_matrix @ world[bone]
        local = [
            (
                world[i]
                if parents[i] == 0xFFFFFFFF
                else world[parents[i]].inverted() @ world[i]
            )
            for i in range(len(world))
        ]
        tracks.append(local)
        for i, name in enumerate(names):
            rest_local = (
                rest[i]
                if parents[i] == 0xFFFFFFFF
                else rest[parents[i]].inverted() @ rest[i]
            )
            bone = rig.pose.bones[name]
            bone.matrix_basis = rest_local.inverted() @ local[i]
            if name in previous_rotations:
                bone.rotation_quaternion.make_compatible(previous_rotations[name])
            previous_rotations[name] = bone.rotation_quaternion.copy()
            for channel in ("location", "rotation_quaternion", "scale"):
                bone.keyframe_insert(channel, frame=frame + 1)
    # Explicit closing key gives Blender the same last-to-first interpolation
    # interval as the runtime. The exported tracks still contain 64 samples.
    for curve in rig.animation_data.action.fcurves:
        first = curve.keyframe_points[0].co.y
        curve.keyframe_points.insert(65, first)
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
        curve.modifiers.new("CYCLES")
    if action_blend:
        # Load only the authored action; the reproducible mesh, weights and
        # skeleton remain the common base for every prototype animation.
        with bpy.data.libraries.load(str(action_blend.resolve()), link=False) as (
            available,
            loaded,
        ):
            if action_name not in available.actions:
                raise ValueError(f"Missing action {action_name}")
            loaded.actions = [action_name]
        action = loaded.actions[0]
        valid_paths = {
            rig.pose.bones[name].path_from_id(channel)
            for name in names
            for channel in ("location", "rotation_quaternion", "scale")
        }
        if any(curve.data_path not in valid_paths for curve in action.fcurves):
            raise ValueError(
                "Action must animate the worker bones with quaternion TRS channels"
            )
        rig.animation_data.action = None
        for bone in rig.pose.bones:
            bone.matrix_basis = Matrix.Identity(4)
        rig.animation_data.action = action
        tracks = []
        for frame in range(64):
            scene.frame_set(frame + 1)
            world = [rig.pose.bones[name].matrix.copy() for name in names]
            for attach in range(1, 13, 3):
                a = world[attach] @ rest[attach].inverted()
                axis = world[attach].to_3x3().col[1].normalized()
                previous_reach = 0.0
                for child in (attach + 1, attach + 2):
                    b = world[child] @ rest[child].inverted()
                    same_rotation = np.allclose(
                        np.array(a.to_quaternion().to_matrix()),
                        np.array(b.to_quaternion().to_matrix()),
                        atol=2e-5,
                    )
                    offset = world[child].translation - world[attach].translation
                    reach = offset.dot(axis)
                    if (
                        not same_rotation
                        or offset.cross(axis).length > 2e-5
                        or reach <= previous_reach
                    ):
                        raise ValueError(
                            f"Worker straight shaft constraint at frame {frame + 1}: "
                            f"rotate {names[attach]} and move its children only "
                            "along local Y, keeping their radial order"
                        )
                    previous_reach = reach
            tracks.append(
                [
                    (
                        world[i]
                        if parents[i] == 0xFFFFFFFF
                        else world[parents[i]].inverted() @ world[i]
                    )
                    for i in range(len(world))
                ]
            )
    scene.render.fps = 32
    scene.frame_start = 1
    scene.frame_end = 64
    scene.frame_set(1)
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str((output / "worker-walk.blend").resolve()))
    vertices = [(positions[v], normals[v], uv[v], *influences[v]) for v in range(count)]
    bones = [
        (
            parents[i],
            (
                rest[i]
                if parents[i] == 0xFFFFFFFF
                else rest[parents[i]].inverted() @ rest[i]
            ),
            rest[i].inverted(),
        )
        for i in range(len(rest))
    ]
    clip = {
        "id": 0,
        "duration": 2,
        "modelToClip": view["modelToClip"],
        "normalToCamera": np.array(view["normalToModel"])
        .reshape(3, 3)
        .T.flatten()
        .tolist(),
        "pivot": [0, 0, 0],
        "radius": view["radius"],
        "frames": [
            (-(frame // 32) * math.pi / 4, ((frame // 32) % 2 * 32 + frame % 32) / 32)
            for frame in range(256)
        ],
        "tracks": tracks,
    }
    packed = encode(vertices, indices.tolist(), bones, [clip], size)
    write_candidate(
        output,
        "worker-walk",
        packed,
        [
            asset,
            view_path,
            contract_path,
            definition_path,
            Path(__file__),
            Path(__file__).with_name("export_rig.py"),
            Path(__file__).with_name("export_units.py"),
            Path(__file__).with_name("worker_surface.py"),
            Path(__file__).with_name("limb_surface.py"),
        ]
        + ([source] if source else [])
        + ([action_blend] if action_blend else []),
        [
            {
                "id": 0,
                "name": action_name if action_blend else "walk",
                "samples": 64,
                "mapping": "alternating-gait-halves-v1",
                "direction8": "existing-unitAnimationFrame",
            }
        ],
    )
    print(f"Worker rig: {count} vertices, {len(bones)} bones, {len(packed)} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--action-blend",
        type=Path,
        help="Import a 64-frame quaternion action from an edited worker scene",
    )
    parser.add_argument(
        "--action",
        default="Walk",
        help="Action to import; exported as a worker-walk preview",
    )
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    author(args.output, args.action_blend, args.action)
