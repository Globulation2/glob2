#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared Blender scene helpers for GSR1 rig authoring.

Only authoring uses this module; the runtime never rebuilds these scenes. A rig
author writes an editable scene with the rest surface, published paint UVs,
bone vertex groups and one action per clip, so an artist can retime or replace
a clip in Blender and export it again.
"""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import numpy as np
from mathutils import Matrix
from skin_assets import FRAMES, PHASES, ROOT, heading_of_frame, normal_to_camera

BLENDER_VERSION = (3, 6, 23)
SAMPLES_PER_SECOND = 32
# A clip stores 64, 128 or 256 uniformly spaced samples at 32 per second, so
# its duration is samples / 32 seconds. Frame f of the 256 gameplay poses maps
# direction f // 32 and phase f % 32 onto sample (direction % groups) * 32 +
# phase: with 64 samples even directions show samples 0-31 and odd directions
# 32-63; with 256 every direction has its own samples.
MAPPINGS = {64: "alternating-gait-halves-v1", 128: "gait-quarters-v1", 256: "direction-major-v1"}


def require_blender():
    """Authoring is pinned to one Blender release so regeneration is reproducible."""
    if bpy.app.version[:3] != BLENDER_VERSION:
        raise ValueError("Use Blender " + ".".join(map(str, BLENDER_VERSION)))


def check_action_arguments(action_blend, action_name):
    """Importing an edited action needs both the file and the action name."""
    if action_blend and not action_name:
        raise ValueError("Action import requires --action")
    if action_name and not action_blend:
        raise ValueError("--action requires --action-blend")


def sample_of_frame(samples):
    """The stored sample each of the 256 gameplay frames shows."""
    if samples not in MAPPINGS:
        raise ValueError("Clips store 64, 128 or 256 samples")
    return [((f // PHASES) % (samples // PHASES)) * PHASES + f % PHASES for f in range(FRAMES)]


def frames_for(samples):
    """GSR1 frame mapping: (heading, time) for each of the 256 gameplay frames."""
    return [(heading_of_frame(f), sample / SAMPLES_PER_SECOND) for f, sample in enumerate(sample_of_frame(samples))]


def clip_descriptor(samples):
    """Manifest description of how a clip's samples map onto gameplay frames."""
    return {"samples": samples, "mapping": MAPPINGS[samples], "direction8": "existing-unitAnimationFrame"}


def clip_record(clip_id, view, tracks):
    """The clip dictionary export_rig.encode serializes, from a baked clip's camera."""
    return {
        "id": clip_id,
        "duration": len(tracks) / SAMPLES_PER_SECOND,
        "modelToClip": view["modelToClip"],
        "normalToCamera": normal_to_camera(view),
        "pivot": [0, 0, 0],
        "radius": view["radius"],
        "frames": frames_for(len(tracks)),
        "tracks": tracks,
    }


def create_scene(model, positions, uv, indices, influences, bones):
    """Fresh scene with the rest surface skinned to an armature.

    ``bones`` are ``(name, parent, bind)`` with ``bind`` a unit-scale 4x4 rest
    matrix in model space, or ``(name, parent, head, tail)`` to let Blender
    derive the rest matrix from a zero-roll bone (``parent`` is an index or -1).
    Returns the scene, the armature object and the rest matrices Blender stored.
    """
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    rig = bpy.data.objects.new(model.title() + "Rig", bpy.data.armatures.new(model.title() + "Skeleton"))
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, parent, *shape in bones:
        bone = rig.data.edit_bones.new(name)
        if len(shape) == 1:
            bind = Matrix(shape[0])
            bone.head = bind.translation
            bone.tail = bind.translation + bind.to_3x3() @ Matrix.Identity(3).col[1]
            bone.matrix = bind
        else:
            bone.head, bone.tail = shape
        if parent >= 0:
            bone.parent = rig.data.edit_bones[bones[parent][0]]
            bone.use_connect = len(shape) == 2 and bool(
                np.linalg.norm(np.array(shape[0]) - np.array(bones[parent][3])) < 1e-6
            )
    bpy.ops.object.mode_set(mode="OBJECT")
    rest = [rig.data.bones[name].matrix_local.copy() for name, *_ in bones]
    for (name, _, *shape), stored in zip(bones, rest):
        if len(shape) == 1 and max(
            abs(a - b) for row_a, row_b in zip(Matrix(shape[0]), stored) for a, b in zip(row_a, row_b)
        ) > 1e-5:
            raise ValueError(f"Blender did not store the {name} rest matrix")
    mesh = bpy.data.meshes.new(model.title() + "RestSurface")
    mesh.from_pydata(np.asarray(positions).tolist(), [], np.asarray(indices).tolist())
    mesh.update()
    obj = bpy.data.objects.new(mesh.name, mesh)
    scene.collection.objects.link(obj)
    layer = mesh.uv_layers.new(name="PublishedPaint")
    for loop in mesh.loops:
        layer.data[loop.index].uv = (float(uv[loop.vertex_index, 0]), 1 - float(uv[loop.vertex_index, 1]))
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    groups = [obj.vertex_groups.new(name=name) for name, *_ in bones]
    for v, (joints, weights) in enumerate(influences):
        for joint, weight in zip(joints, weights):
            if weight:
                groups[joint].add([v], float(weight), "REPLACE")
    modifier = obj.modifiers.new("Skin", "ARMATURE")
    modifier.object = rig
    rig.show_in_front = True
    rig.animation_data_create()
    for bone in rig.pose.bones:
        bone.rotation_mode = "QUATERNION"
    scene.render.fps = SAMPLES_PER_SECOND
    scene.frame_start = 1
    scene.frame_end = 64
    return scene, rig, rest


def install_action(rig, rest, bones, tracks, name):
    """Key the parent-relative samples as a cyclic quaternion action."""
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    rig.animation_data.action = action
    previous = {}
    for frame, local in enumerate(tracks):
        for i, (bone_name, parent, *_) in enumerate(bones):
            bind = rest[i] if parent < 0 else rest[parent].inverted() @ rest[i]
            bone = rig.pose.bones[bone_name]
            bone.matrix_basis = bind.inverted() @ Matrix(local[i])
            if bone_name in previous:
                bone.rotation_quaternion.make_compatible(previous[bone_name])
            previous[bone_name] = bone.rotation_quaternion.copy()
            for channel in ("location", "rotation_quaternion", "scale"):
                bone.keyframe_insert(channel, frame=frame + 1)
    # The closing key gives Blender the same last-to-first interval as the
    # runtime, which wraps the last sample toward the first.
    for curve in action.fcurves:
        curve.keyframe_points.insert(len(tracks) + 1, curve.keyframe_points[0].co.y)
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
        curve.modifiers.new("CYCLES")
    return action


def import_action(scene, rig, bones, source, name, samples=64):
    """Sample a user action's quaternion bone channels on the authored skeleton.

    Frames 1..samples are sampled; frame samples + 1 should repeat frame 1.
    """
    with bpy.data.libraries.load(str(source.resolve()), link=False) as (available, loaded):
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
        raise ValueError("Action must animate the rig bones with quaternion TRS channels")
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    rig.animation_data.action = action
    tracks = []
    for frame in range(1, samples + 1):
        scene.frame_set(frame)
        world = [rig.pose.bones[bone[0]].matrix.copy() for bone in bones]
        tracks.append([world[i] if bone[1] < 0 else world[bone[1]].inverted() @ world[i] for i, bone in enumerate(bones)])
    return action, tracks


def check_action_source(action_blend, output, model):
    """Edited sources stay repository-relative and separate from generated output."""
    action_blend = action_blend.resolve()
    try:
        action_blend.relative_to(ROOT)
    except ValueError as error:
        raise ValueError("Keep the edited action source under this repository (for example artifacts/)") from error
    if action_blend == (output / (model + ".blend")).resolve():
        raise ValueError("Keep the edited action source separate from generated output")
    return action_blend
