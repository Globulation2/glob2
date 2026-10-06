#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Author a clean worker-walk candidate in Blender 3.6.23.

Use the shipped welded rest surface as the paint/topology reference. Retarget
component centers to fresh bones using shortest-arc swings, never metaball rolls.
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
from mathutils import Matrix, Quaternion, Vector
from export_units import open_source, components, influence_matrices, sample_pose
from export_rig import encode, write_candidate
ROOT = Path(__file__).resolve().parents[2]


def author(output):
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError('Use Blender 3.6.23')
    source, scene = open_source('worker', 'walk')
    parts = components(scene)
    origin = float(scene.objects['RotEmpty'].rotation_euler.z)
    motion = []
    for sample in range(64):
        direction, phase = divmod(sample, 32)
        sample_pose(scene, direction, phase, origin)
        matrices = influence_matrices(parts)
        undo = np.array(Matrix.Rotation(direction * math.pi / 4, 4, 'Z'))
        motion.append([Matrix(undo @ m) for m in matrices])
    definition_path = ROOT / 'datasrc/gfx/authored/skins/limb-surfaces.json'
    paths = json.loads(definition_path.read_text())['worker']['paths']
    asset = ROOT / 'data/skins/colony-v1/worker-walk.gsk'
    view_path = asset.with_suffix('.view.json')
    view = json.loads(view_path.read_text())
    data = asset.read_bytes()
    _, count, index_count, _, size = struct.unpack_from('<4sIIII', data)
    uv = np.frombuffer(data, '<f4', count * 2, 20).reshape(-1, 2)
    indices = np.frombuffer(data, '<u4', index_count, 20 + count * 8)
    pose = np.frombuffer(data, '<f4', count * 6, 20 + count * 8 + index_count * 4).reshape(-1, 6)
    inv = np.array(view['clipToModel']).reshape(4, 4)
    positions = (np.column_stack((pose[:, :3], np.ones(count))) @ inv.T)[:, :3]
    normals = pose[:, 3:] @ np.array(view['normalToModel']).reshape(3, 3).T
    normals /= np.linalg.norm(normals, axis=1)[:, None]
    contract_path = asset.parent / 'worker-surface.json'
    regions = json.loads(contract_path.read_text())['regions']
    # Create a new scene; legacy objects are reference data only.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    armature = bpy.data.armatures.new('WorkerSkeleton')
    rig = bpy.data.objects.new('WorkerRig', armature)
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig; rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    root = armature.edit_bones.new('body')
    root.head = motion[0][0].translation; root.tail = root.head + Vector((0, 1, 0))
    parents = [0xffffffff]
    names = ['body']
    centers = [(None, 0)]
    for limb, path in enumerate(paths):
        previous = 0
        for segment, part in enumerate(path):
            name = f'limb-{limb}-{segment}'
            bone = armature.edit_bones.new(name)
            start = 0 if segment == 0 else path[segment - 1]
            bone.head = motion[0][start].translation; bone.tail = motion[0][part].translation
            bone.parent = armature.edit_bones[names[previous]]
            parents.append(previous); previous = len(names); names.append(name); centers.append((start, part))
    bpy.ops.object.mode_set(mode='OBJECT')
    rest = [armature.bones[name].matrix_local.copy() for name in names]
    mesh = bpy.data.meshes.new('WorkerRestSurface')
    mesh.from_pydata(positions.tolist(), [], indices.reshape(-1, 3).tolist()); mesh.update()
    obj = bpy.data.objects.new('WorkerRestSurface', mesh); scene.collection.objects.link(obj)
    layer = mesh.uv_layers.new(name='PublishedPaint')
    for loop in mesh.loops: layer.data[loop.index].uv = (float(uv[loop.vertex_index, 0]), 1 - float(uv[loop.vertex_index, 1]))
    for poly in mesh.polygons: poly.use_smooth = True
    groups = [obj.vertex_groups.new(name=name) for name in names]
    influences = []
    for v, point in enumerate(positions):
        region = regions[v]
        if region < 0:
            joints, weights = [0, 0, 0, 0], [1., 0., 0., 0.]
        else:
            joints = [0] + [1 + region * 3 + segment for segment in range(3)]
            # Local influence support: another limb can never pull this socket.
            anchors = [motion[0][0].translation] + [motion[0][p].translation for p in paths[region]]
            distances = np.array([np.linalg.norm(point - np.array(p)) for p in anchors])
            raw = 1 / np.maximum(distances, .1)**4
            weights = (raw / raw.sum()).tolist()
        influences.append((joints, weights))
    # Diffuse support over the welded graph so socket rims do not form a
    # discontinuity between root-only torso vertices and articulated limbs.
    # A limb never receives another limb's bones; only the torso can blend
    # adjacent sockets. Truncate to four influences once after smoothing.
    dense = np.zeros((count, len(names)))
    adjacency = [set() for _ in range(count)]
    for triangle in indices.reshape(-1, 3):
        for vertex in triangle:
            adjacency[vertex].update(int(other) for other in triangle if other != vertex)
    for v, (joints, weights) in enumerate(influences):
        for joint, weight in zip(joints, weights): dense[v, joint] += weight
    allowed = np.ones_like(dense)
    for v, region in enumerate(regions):
        if region >= 0:
            allowed[v] = 0
            allowed[v, [0, 1+region*3, 2+region*3, 3+region*3]] = 1
    for _ in range(32):
        blended = np.array([dense[list(neighbors)].mean(axis=0) for neighbors in adjacency])
        dense = (.5*dense + .5*blended) * allowed
        dense /= dense.sum(axis=1)[:, None]
    influences = []
    for v in range(count):
        joints = np.argsort(-dense[v], kind='stable')[:4].tolist()
        weights = dense[v, joints]; weights /= weights.sum()
        influences.append((joints, weights.tolist()))
        for joint, weight in zip(joints, weights):
            if weight: groups[joint].add([v], float(weight), 'REPLACE')
    modifier = obj.modifiers.new('Skin', 'ARMATURE'); modifier.object = rig
    tracks = []
    for frame, parts in enumerate(motion):
        body_rotation = parts[0].to_quaternion() @ motion[0][0].to_quaternion().conjugated()
        body_scale = parts[0].to_scale().x / motion[0][0].to_scale().x
        world = [Matrix.Translation(parts[0].translation) @ body_rotation.to_matrix().to_4x4() @ Matrix.Scale(body_scale, 4)]
        # All rotations derive from fresh rest axes and the body-relative path.
        for bone, (start, end) in enumerate(centers[1:], 1):
            rest_direction = motion[0][end].translation - motion[0][start].translation
            direction = parts[end].translation - parts[start].translation
            swing = (body_rotation @ rest_direction).rotation_difference(direction)
            world.append(Matrix.Translation(parts[start].translation) @ swing.to_matrix().to_4x4() @ body_rotation.to_matrix().to_4x4() @ rest[bone].to_quaternion().to_matrix().to_4x4() @ Matrix.Scale(direction.length / rest_direction.length, 4))
        local = [world[i] if parents[i] == 0xffffffff else world[parents[i]].inverted() @ world[i] for i in range(len(world))]
        tracks.append(local)
        for i, name in enumerate(names):
            rest_local = rest[i] if parents[i] == 0xffffffff else rest[parents[i]].inverted() @ rest[i]
            bone = rig.pose.bones[name]; bone.rotation_mode = 'QUATERNION'; bone.matrix_basis = rest_local.inverted() @ local[i]
            for channel in ('location', 'rotation_quaternion', 'scale'): bone.keyframe_insert(channel, frame=frame + 1)
    scene.frame_start = 1; scene.frame_end = 64; scene.frame_set(1)
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str((output / 'worker-walk.blend').resolve()))
    vertices = [(positions[v], normals[v], uv[v], *influences[v]) for v in range(count)]
    bones = [(parents[i], rest[i] if parents[i] == 0xffffffff else rest[parents[i]].inverted() @ rest[i], rest[i].inverted()) for i in range(len(rest))]
    clip = {'id': 0, 'duration': 2, 'modelToClip': view['modelToClip'],
            'normalToCamera': np.array(view['normalToModel']).reshape(3, 3).T.flatten().tolist(),
            'pivot': [0, 0, 0], 'radius': view['radius'],
            'frames': [(- (frame // 32) * math.pi / 4, ((frame // 32) % 2 * 32 + frame % 32) / 32) for frame in range(256)], 'tracks': tracks}
    packed = encode(vertices, indices.tolist(), bones, [clip], size)
    write_candidate(output, 'worker-walk', packed,
        [source, asset, view_path, contract_path, definition_path, Path(__file__), Path(__file__).with_name('export_rig.py'), Path(__file__).with_name('export_units.py')],
        [{'id': 0, 'name': 'walk', 'samples': 64, 'mapping': 'alternating-gait-halves-v1', 'direction8': 'existing-unitAnimationFrame'}])
    print(f'Worker rig: {count} vertices, {len(bones)} bones, {len(packed)} bytes')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:]); author(args.output)
