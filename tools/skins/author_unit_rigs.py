#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Author warrior/explorer GSR1 candidates and editable Blender actions.

Run in Blender 3.6.23 with -- --model warrior|explorer --output artifacts/rig/MODEL.
The warrior shares one rest mesh and skeleton across walk/swim/fight. Explorer
ellipsoid proportions live in the rest mesh; its animated bones are uniform TRS.
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
from mathutils import Matrix, Quaternion, Vector
from export_units import open_source, components, sample_pose
from export_rig import encode, write_candidate
from limb_surface import LimbSurface
from worker_surface import smooth_surface, vertex_normals

ROOT = Path(__file__).resolve().parents[2]
CLIPS = {"warrior": ("walk", "swim", "fight"), "explorer": ("fly",)}


def baked(model, clip):
    path = ROOT / f"data/skins/colony-v1/{model}-{clip}.gsk"
    data = path.read_bytes()
    _, count, index_count, _, size = struct.unpack_from("<4s4I", data)
    uv = np.frombuffer(data, "<f4", count * 2, 20).reshape(-1, 2)
    indices = np.frombuffer(data, "<u4", index_count, 20 + count * 8).reshape(-1, 3)
    pose = np.frombuffer(
        data, "<f4", count * 6, 20 + count * 8 + index_count * 4
    ).reshape(-1, 6)
    view = json.loads(path.with_suffix(".view.json").read_text())
    positions = (
        np.column_stack((pose[:, :3], np.ones(count)))
        @ np.array(view["clipToModel"]).reshape(4, 4).T
    )[:, :3]
    return path, uv, indices, positions, size, view


def ease(x, lo, hi):
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def pack_weights(dense):
    if dense.min() < -1e-8 or np.any((dense > 1e-10).sum(axis=1) > 4):
        raise ValueError(
            f"Influence support exceeds four bones: min={dense.min()}, maxCount={(dense>1e-10).sum(axis=1).max()}"
        )
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


def rest_surface(model):
    path, uv, indices, positions, size, view = baked(model, CLIPS[model][0])
    if model == "warrior":
        definition = json.loads(
            (ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json").read_text()
        )[model]
        surface = LimbSurface(definition)
        if not np.array_equal(surface.triangles, indices) or not np.array_equal(
            surface.uv.astype("<f4"), uv
        ):
            raise ValueError("Warrior paint topology changed")
        _, motion = reference_motion("warrior", "walk")
        reference = motion[0]
        anchors = np.array([np.array(part.translation) for part in reference])
        origin = anchors[0]
        directions = np.array(
            [(0, 1, 1), (0, -1, 1), (0, 1, -1), (0, -1, -1)]
        ) / math.sqrt(2)
        # All four lobes share the accepted leg proportions. Source asymmetry
        # belongs to the animation, never to the rest shape or control lengths.
        lengths = np.mean(
            [
                [np.linalg.norm(anchors[part] - origin) for part in path]
                for path in definition["paths"][2:]
            ],
            axis=0,
        )
        for direction, path in zip(directions, definition["paths"]):
            for part, length in zip(path, lengths):
                anchors[part] = origin + direction * length
        bones = [("body", -1, origin, origin + (0, 1, 0))]
        centers = []
        for limb, parts in enumerate(definition["paths"]):
            limb_name, side = (("arm", "R"), ("arm", "L"), ("leg", "R"), ("leg", "L"))[
                limb
            ]
            for segment, part in enumerate(parts):
                start = 0 if segment == 0 else parts[segment - 1]
                parent = 0 if segment == 0 else len(bones) - 1
                direction = anchors[part] - anchors[start]
                bones.append(
                    (
                        f'{limb_name}.{("upper", "lower")[segment]}.{side}',
                        parent,
                        anchors[part],
                        anchors[part] + direction / np.linalg.norm(direction),
                    )
                )
                centers.append((start, part))
        positions, dense = [], []
        body_radius, bulb = 3.5, 2.78
        canonical = np.array(
            [
                -3 * math.pi / 4,
                -math.pi / 4,
                math.pi / 4,
                3 * math.pi / 4,
                5 * math.pi / 4,
            ]
        )
        target = np.array(
            [math.atan2(directions[b][2], directions[b][1]) for b in [3, 2, 0, 1, 3]]
        )
        for i in range(1, len(target)):
            while target[i] <= target[i - 1]:
                target[i] += 2 * math.pi
        for descriptor in surface.vertices:
            kind = descriptor[0]
            weights = np.zeros(9)
            if kind == "average":
                positions.append(np.mean([positions[i] for i in descriptor[1]], axis=0))
                dense.append(np.mean([dense[i] for i in descriptor[1]], axis=0))
                continue
            if kind == "body":
                v = np.array(descriptor[1])
                v = np.array(
                    (v[0], (v[1] - v[2]) / math.sqrt(2), (v[1] + v[2]) / math.sqrt(2))
                )
                phi = math.atan2(v[2], v[1])
                if phi < canonical[0]:
                    phi += 2 * math.pi
                angle = np.interp(phi, canonical, target)
                radial = math.hypot(v[1], v[2])
                body_direction = (
                    np.array((v[0], radial * math.cos(angle), radial * math.sin(angle)))
                    / 1.8
                )
                # Give every lobe a broad, identical opening in the torso.
                # Expanding only the outer rings leaves a narrow stalk at the
                # body. Disjoint angular patches preserve both reflections.
                for junction in directions:
                    cosine = float(np.clip(body_direction @ junction, -1, 1))
                    theta = math.acos(cosine)
                    if 1e-8 < theta < math.pi / 4:
                        tangent = (body_direction - junction * cosine) / math.sin(theta)
                        theta = math.pi / 4 * (theta / (math.pi / 4)) ** 0.35
                        body_direction = junction * math.cos(
                            theta
                        ) + tangent * math.sin(theta)
                positions.append(origin + body_direction * body_radius)
                # Share some movement with the surrounding body. A full-weight
                # patch folds the torso at legacy extremes; partial influence
                # lets the junction travel while retaining the central volume.
                junction_weights = [
                    float(
                        0.5
                        * ease(body_direction @ direction, math.cos(1.3), math.cos(0.5))
                    )
                    for direction in directions
                ]
                weights[1::2] = junction_weights
                weights[0] = 1 - max(junction_weights)
                weights /= weights.sum()
            else:
                branch = descriptor[1]
                direction = directions[branch]
                end = anchors[definition["paths"][branch][-1]]
                front = np.array((1.0, 0, 0))
                side = np.cross(direction, front)
                side /= np.linalg.norm(side)
                angle = (
                    surface.branches[branch][3][descriptor[-1]] if kind != "tip" else 0
                )
                radial = front * math.cos(angle) + side * math.sin(angle)
                if kind == "ring":
                    t = descriptor[2]
                    opening = math.pi / 4 * (math.atan(0.25) / (math.pi / 4)) ** 0.35
                    socket = origin + direction * body_radius * math.cos(opening)
                    center = socket + (end - socket) * t
                    distance = np.linalg.norm(end - center)
                    width = max(
                        2.1,
                        math.sqrt(max(0, bulb**2 - distance**2)),
                    )
                    collar = body_radius * math.sin(opening)
                    width = (1 - ease(t, 0, 0.3)) * collar + ease(t, 0, 0.3) * width
                    positions.append(center + radial * width)
                    # Both lobe controls share one orientation during motion
                    # transfer. Blend only their axial translations here: each
                    # cross-section stays round, with a straight centerline.
                    # Root rotation influence along the shaft caused visible
                    # bending and pinching even when the two controls aligned.
                    # Spread axial shortening across the whole connector. An
                    # early terminal-weight plateau folds the middle rings
                    # backwards when the lobe retracts toward the torso.
                    terminal = t
                    weights[1 + branch * 2] = 1 - terminal
                    weights[2 + branch * 2] = terminal
                else:
                    latitude = math.pi / 2 if kind == "tip" else descriptor[2]
                    positions.append(
                        end
                        + direction * bulb * math.sin(latitude)
                        + radial * bulb * math.cos(latitude)
                    )
                    weights[2 + branch * 2] = 1
            dense.append(weights)
        positions = smooth_surface(np.array(positions), indices, iterations=4)
        normals = vertex_normals(positions, indices)
        dense = np.array(dense)
        extras = {"anchors": anchors, "centers": centers}
    else:
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
        extras = {"weldedIndices": faces, "weldMap": inverse}
    return positions, normals, uv, indices, pack_weights(dense), bones, size, extras


def reference_motion(model, clip):
    source, scene = open_source(model, clip)
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
        # baked into explorer rest geometry, not legal bone scales.
        motion.append([undo @ p.matrix_world.copy() for p in parts])
    return source, motion


def create_scene(model, positions, uv, indices, influences, bones):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    rig = bpy.data.objects.new(
        model.title() + "Rig", bpy.data.armatures.new(model.title() + "Skeleton")
    )
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, parent, head, tail in bones:
        bone = rig.data.edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        if parent >= 0:
            bone.parent = rig.data.edit_bones[bones[parent][0]]
            bone.use_connect = bool(
                np.linalg.norm(np.array(head) - np.array(bones[parent][3])) < 1e-6
            )
    bpy.ops.object.mode_set(mode="OBJECT")
    rest = [rig.data.bones[b[0]].matrix_local.copy() for b in bones]
    mesh = bpy.data.meshes.new(model.title() + "RestSurface")
    mesh.from_pydata(positions.tolist(), [], indices.tolist())
    mesh.update()
    obj = bpy.data.objects.new(mesh.name, mesh)
    scene.collection.objects.link(obj)
    layer = mesh.uv_layers.new(name="PublishedPaint")
    for loop in mesh.loops:
        layer.data[loop.index].uv = (
            float(uv[loop.vertex_index, 0]),
            1 - float(uv[loop.vertex_index, 1]),
        )
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    groups = [obj.vertex_groups.new(name=b[0]) for b in bones]
    for v, (joints, weights) in enumerate(influences):
        for joint, weight in zip(joints, weights):
            if weight:
                groups[joint].add([v], float(weight), "REPLACE")
    modifier = obj.modifiers.new("Skin", "ARMATURE")
    modifier.object = rig
    rig.show_in_front = True
    rig.animation_data_create()
    for i, (name, *_) in enumerate(bones):
        bone = rig.pose.bones[name]
        bone.rotation_mode = "QUATERNION"
        bone.lock_location = (model != "warrior" and i != 0,) * 3
        bone.lock_scale = (True,) * 3
        if model == "warrior" and i > 0 and i % 2 == 0:
            # The distal handle changes reach along the shaft, not its bend.
            bone.lock_location = (True, False, True)
            bone.lock_rotation = (True,) * 3
            bone.lock_rotations_4d = True
            bone.lock_rotation_w = True
    scene.render.fps = 32
    scene.frame_start = 1
    scene.frame_end = 64
    return scene, rig, rest


def retarget(model, motion, reference, rest, bones, extras, clip):
    result = []
    for parts in motion:
        rotation = parts[0].to_quaternion() @ reference[0].to_quaternion().conjugated()
        if model == "warrior":
            definition = json.loads(
                (ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json").read_text()
            )[model]

            def body_frame(part, axes):
                basis = np.array(part.to_quaternion().to_matrix())
                return Matrix(
                    np.column_stack(
                        [
                            basis[:, abs(axis) - 1] * (1 if axis > 0 else -1)
                            for axis in axes
                        ]
                    )
                ).to_quaternion()

            axes = definition.get("clipBodyAxes", {}).get(clip, definition["bodyAxes"])
            rotation = (
                body_frame(parts[0], axes)
                @ body_frame(reference[0], definition["bodyAxes"]).conjugated()
            )
        root_head = (
            parts[0].translation
            if model == "warrior"
            else parts[0].translation - reference[0].translation
        )
        world = [
            Matrix.Translation(root_head)
            @ rotation.to_matrix().to_4x4()
            @ rest[0].to_quaternion().to_matrix().to_4x4()
        ]
        if model == "warrior":
            if clip == "swim":
                # The swim stroke retracts whole lobes into the body. Solving
                # it as fixed-length segments loses that inward motion even
                # before contact constraints run. Transfer volume centers;
                # the shared radial orientation is applied after clearance.
                world.extend(
                    Matrix.Translation(parts[end].translation)
                    for _, end in extras["centers"]
                )
            else:
                for bone, (start, end) in enumerate(extras["centers"], 1):
                    parent = bones[bone][1]
                    neutral = Vector(extras["anchors"][end] - extras["anchors"][start])
                    desired = parts[end].translation - parts[start].translation
                    parent_rotation = (
                        rotation
                        if parent == 0
                        else (world[parent] @ rest[parent].inverted()).to_quaternion()
                    )
                    swing = (parent_rotation @ neutral).rotation_difference(desired)
                    limit = 0.5 if parent == 0 else 0.65
                    if swing.angle > limit:
                        swing = Quaternion().slerp(swing, limit / swing.angle)
                    delta = swing @ parent_rotation
                    start_point = (
                        parts[0].translation
                        if parent == 0
                        else world[parent].translation
                    )
                    head = start_point + delta @ neutral
                    world.append(
                        Matrix.Translation(head)
                        @ delta.to_matrix().to_4x4()
                        @ rest[bone].to_quaternion().to_matrix().to_4x4()
                    )
            # Keep rigid terminal volumes apart instead of letting skinning
            # flatten them into the torso or another limb at legacy extremes.
            terminals = [2, 4, 6, 8]
            minimum_reach = 6.0 if clip == "swim" else 6.3
            for _ in range(12):
                for i in terminals:
                    offset = world[i].translation - world[0].translation
                    if offset.length < minimum_reach:
                        world[i].translation = (
                            world[0].translation + offset.normalized() * minimum_reach
                        )
                for a_index, a in enumerate(terminals):
                    for b in terminals[a_index + 1 :]:
                        offset = world[b].translation - world[a].translation
                        if offset.length < 5.7:
                            correction = (
                                offset.normalized() * (5.7 - offset.length) * 0.5
                            )
                            world[a].translation -= correction
                            world[b].translation += correction
            # Straight, full-width shafts need clearance along their length,
            # not just between the terminal spheres. Spread converging rays
            # equally while retaining each lobe's radial reach.
            for _ in range(12):
                for a_index, a in enumerate(terminals):
                    for b in terminals[a_index + 1 :]:
                        va = world[a].translation - root_head
                        vb = world[b].translation - root_head
                        angle = va.angle(vb)
                        minimum = math.radians(60)
                        if angle < minimum:
                            axis = va.cross(vb).normalized()
                            correction = (minimum - angle) * 0.5
                            world[a].translation = (
                                root_head + Quaternion(axis, -correction) @ va
                            )
                            world[b].translation = (
                                root_head + Quaternion(axis, correction) @ vb
                            )
            # During recovery, gather the four lobes into the symmetric
            # compact pose. Their end caps retain full size; only the exposed
            # shaft length decreases. Opening the angles toward the rest pose
            # gives the broad caps room to tuck without crossing each other.
            retraction = 0.0
            if clip == "swim":
                source_reach = np.mean(
                    [(parts[p].translation - root_head).length for p in [4, 3, 1, 2]]
                )
                retraction = 1.0 - float(ease(source_reach, 2.7, 6.5))
            # Treat each pair as controls for one radial lobe, not an elbow.
            # Follow the displaced volume center with a continuous broad neck;
            # independent upper/lower rotations otherwise fold its inner edge.
            for upper, terminal in [(1, 2), (3, 4), (5, 6), (7, 8)]:
                neutral = Vector(bones[terminal][2] - bones[0][2])
                outward = world[terminal].translation - root_head
                delta = (rotation @ neutral).rotation_difference(outward) @ rotation
                terminal_head = world[terminal].translation.copy()
                if retraction > 0:
                    delta = delta.slerp(rotation, retraction)
                    delta.normalize()
                    reach = outward.length * (1 - retraction) + 3.5 * retraction
                    terminal_head = root_head + delta @ neutral.normalized() * reach
                for bone, head in [
                    (upper, root_head + delta @ Vector(bones[upper][2] - bones[0][2])),
                    (terminal, terminal_head),
                ]:
                    world[bone] = (
                        Matrix.Translation(head)
                        @ delta.to_matrix().to_4x4()
                        @ rest[bone].to_quaternion().to_matrix().to_4x4()
                    )
        else:
            for bone, part in [(1, 3), (2, 1), (3, 2)]:
                delta = (
                    rotation
                    if bone == 1
                    else parts[part].to_quaternion()
                    @ reference[part].to_quaternion().conjugated()
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


def install_action(rig, rest, bones, tracks, name):
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    rig.animation_data.action = action
    previous = {}
    for frame, local in enumerate(tracks):
        for i, (name, parent, *_) in enumerate(bones):
            bind = rest[i] if parent < 0 else rest[parent].inverted() @ rest[i]
            bone = rig.pose.bones[name]
            bone.matrix_basis = bind.inverted() @ local[i]
            if name in previous:
                bone.rotation_quaternion.make_compatible(previous[name])
            previous[name] = bone.rotation_quaternion.copy()
            for channel in ("location", "rotation_quaternion", "scale"):
                bone.keyframe_insert(channel, frame=frame + 1)
    for curve in action.fcurves:
        curve.keyframe_points.insert(65, curve.keyframe_points[0].co.y)
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
        curve.modifiers.new("CYCLES")
    return action


def import_action(scene, rig, bones, source, name):
    """Sample only quaternion bone channels against the reproducible rest rig."""
    with bpy.data.libraries.load(str(source.resolve()), link=False) as (
        available,
        loaded,
    ):
        if name not in available.actions:
            raise ValueError(f"Missing action {name}")
        loaded.actions = [name]
    action = loaded.actions[0]
    valid_paths = {
        rig.pose.bones[bone[0]].path_from_id(channel)
        for bone in bones
        for channel in ("location", "rotation_quaternion", "scale")
    }
    if any(curve.data_path not in valid_paths for curve in action.fcurves):
        raise ValueError(
            "Action must animate the rig bones with quaternion TRS channels"
        )
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    rig.animation_data.action = action
    tracks = []
    for frame in range(1, 65):
        scene.frame_set(frame)
        world = [rig.pose.bones[bone[0]].matrix.copy() for bone in bones]
        if rig.name == "WarriorRig":
            for upper, terminal in [(1, 2), (3, 4), (5, 6), (7, 8)]:
                a = (
                    world[upper]
                    @ rig.data.bones[bones[upper][0]].matrix_local.inverted()
                )
                b = (
                    world[terminal]
                    @ rig.data.bones[bones[terminal][0]].matrix_local.inverted()
                )
                same_rotation = np.allclose(
                    np.array(a.to_quaternion().to_matrix()),
                    np.array(b.to_quaternion().to_matrix()),
                    atol=2e-5,
                )
                axis = world[upper].to_3x3().col[1].normalized()
                offset = world[terminal].translation - world[upper].translation
                if not same_rotation or offset.cross(axis).length > 2e-5:
                    raise ValueError(
                        f"Warrior straight shaft constraint at frame {frame}: "
                        f"rotate {bones[upper][0]} and move {bones[terminal][0]} "
                        "only along its local Y axis"
                    )
        tracks.append(
            [
                world[i] if bone[1] < 0 else world[bone[1]].inverted() @ world[i]
                for i, bone in enumerate(bones)
            ]
        )
    return action, tracks


def author(model, output, action_blend=None, action_name=None, preview_clip=None):
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError("Use Blender 3.6.23")
    if action_blend:
        action_blend = action_blend.resolve()
        action_blend.relative_to(ROOT)
        if action_blend == (output / (model + ".blend")).resolve():
            raise ValueError(
                "Keep the edited action source separate from generated output"
            )
        if not action_name or preview_clip not in CLIPS[model]:
            raise ValueError(
                "Action import requires --action and a valid --clip for this model"
            )
    elif action_name or preview_clip:
        raise ValueError("--action and --clip require --action-blend")
    positions, normals, uv, indices, influences, bones, size, extras = rest_surface(
        model
    )
    motions = {}
    sources = []
    for clip in (() if action_blend else CLIPS[model]):
        source, motion = reference_motion(model, clip)
        sources.append(source)
        motions[clip] = motion
    scene, rig, rest = create_scene(model, positions, uv, indices, influences, bones)
    reference = None if action_blend else motions[CLIPS[model][0]][0]
    encoded_bones = [
        (
            parent if parent >= 0 else 0xFFFFFFFF,
            rest[i] if parent < 0 else rest[parent].inverted() @ rest[i],
            rest[i].inverted(),
        )
        for i, (_, parent, *_) in enumerate(bones)
    ]
    vertices = [
        (positions[i], normals[i], uv[i], *influences[i]) for i in range(len(positions))
    ]
    actions = []
    dependencies = [
        Path(__file__),
        Path(__file__).with_name("worker_surface.py"),
        Path(__file__).with_name("limb_surface.py"),
        Path(__file__).with_name("export_rig.py"),
        Path(__file__).with_name("export_units.py"),
    ]
    if model == "warrior":
        dependencies += [
            ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json",
            ROOT / "data/skins/colony-v1/warrior-surface.json",
            ROOT / "datasrc/gfx/originals/units/glob-warrior-walk.blend",
        ]
    base_asset = baked(model, CLIPS[model][0])[0]
    dependencies += [base_asset, base_asset.with_suffix(".view.json")]
    if action_blend:
        sources.append(action_blend)
    for clip_id, clip in enumerate(CLIPS[model]):
        if action_blend:
            if clip != preview_clip:
                continue
            action, tracks = import_action(scene, rig, bones, action_blend, action_name)
            actions.append(action)
        else:
            tracks = retarget(
                model, motions[clip], reference, rest, bones, extras, clip
            )
            actions.append(install_action(rig, rest, bones, tracks, clip.title()))
        asset, _, _, _, _, view = baked(model, clip)
        record = {
            "id": clip_id,
            "duration": 2,
            "modelToClip": view["modelToClip"],
            "normalToCamera": np.array(view["normalToModel"])
            .reshape(3, 3)
            .T.flatten()
            .tolist(),
            "pivot": [0, 0, 0],
            "radius": view["radius"],
            "frames": [
                (-(f // 32) * math.pi / 4, ((f // 32) % 2 * 32 + f % 32) / 32)
                for f in range(256)
            ],
            "tracks": tracks,
        }
        data = encode(vertices, indices.ravel().tolist(), encoded_bones, [record], size)
        write_candidate(
            output,
            model + "-" + clip,
            data,
            dependencies + sources + [asset, asset.with_suffix(".view.json")],
            [
                {
                    "id": clip_id,
                    "name": action_name if action_blend else clip,
                    "samples": 64,
                    "mapping": "alternating-gait-halves-v1",
                    "direction8": "existing-unitAnimationFrame",
                }
            ],
        )
        print(
            f"{model}-{clip}: {len(positions)} vertices, {len(bones)} bones, {len(data)} bytes"
        )
    rig.animation_data.action = actions[0]
    scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str((output / (model + ".blend")).resolve()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", choices=CLIPS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--action-blend", type=Path)
    parser.add_argument("--action")
    parser.add_argument("--clip", choices=("walk", "swim", "fight", "fly"))
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    author(args.model, args.output, args.action_blend, args.action, args.clip)
