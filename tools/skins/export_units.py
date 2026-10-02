#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stable-surface unit export. Run inside pinned Blender 3.6.23.

Keep original sources untouched. A single evaluated walk mesh supplies topology
and UVs for all clips. Fixed, normalized metaball influence weights transfer that
surface through the original animated component transforms. This is an asset
feasibility experiment, not a claim of exact metaball surface reconstruction.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
MODELS = {'worker': (38, ('walk', 'swim', 'harvest')),
          'warrior': (40, ('walk', 'swim', 'fight')),
          'explorer': (32, ('fly',))}


def open_source(model, clip):
    path = ROOT / 'datasrc/gfx/originals/units' / ('explorer.blend' if model == 'explorer' else 'glob-' + model + '-' + clip + '.blend')
    bpy.ops.wm.open_mainfile(filepath=str(path), use_scripts=False)
    scene = bpy.context.scene
    scene.frame_set(1)
    return path, scene


def components(scene):
    result = sorted((o for o in scene.objects if o.type == 'META'), key=lambda o: o.name)
    if not result or any(len(o.data.elements) != 1 or o.data.elements[0].type not in ('BALL', 'ELLIPSOID') for o in result):
        raise ValueError('Expected named, single ball/ellipsoid unit components')
    return result


def influence_matrices(parts):
    # Spherical metaball rotations have no geometric meaning. Their individual
    # bone rolls can accumulate across gait cycles; transferring those rotations
    # twists a welded mesh even when the source balls return to the same shape.
    # Transfer each ball's center and scale in the shared body orientation.
    root = np.array(parts[0].matrix_world, dtype=np.float64)
    rotation = root[:3, :3] / np.linalg.norm(root[:3, :3], axis=0)
    result = []
    for part in parts:
        world = np.array(part.matrix_world, dtype=np.float64)
        sizes = np.linalg.norm(world[:3, :3], axis=0)
        if np.max(sizes) - np.min(sizes) > 1e-4:
            raise ValueError('Anisotropic metaball needs an explicit deformation rule')
        transform = np.eye(4)
        element = part.data.elements[0]
        if element.type == 'BALL':
            transform[:3, :3] = rotation * sizes.mean()
        else:
            # Wings/body ellipsoids have meaningful orientation and anisotropic
            # field axes. Include both in bind-local coordinates and deformation.
            axes = np.array([element.size_x, element.size_y, element.size_z])
            if np.min(axes) <= 0:
                raise ValueError('Invalid ellipsoid axes')
            element_rotation = np.array(element.rotation.to_matrix())
            transform[:3, :3] = world[:3, :3] @ element_rotation @ np.diag(axes)
        center = np.array([*part.data.elements[0].co, 1.0])
        transform[:3, 3] = (world @ center)[:3]
        result.append(transform)
    return np.array(result)


def normals(positions, triangles):
    face = np.cross(positions[triangles[:, 1]] - positions[triangles[:, 0]],
                    positions[triangles[:, 2]] - positions[triangles[:, 0]])
    result = np.zeros_like(positions)
    for corner in range(3):
        np.add.at(result, triangles[:, corner], face)
    lengths = np.linalg.norm(result, axis=1)
    if np.any(lengths < 1e-10):
        raise ValueError('Degenerate vertex normal')
    return result / lengths[:, None]


def canonical_surface(scene):
    parts = components(scene)
    graph = bpy.context.evaluated_depsgraph_get()
    surfaces = []
    for part in parts:
        evaluated = part.evaluated_get(graph)
        mesh = evaluated.to_mesh()
        if len(mesh.vertices):
            surfaces.append((part, bpy.data.meshes.new_from_object(evaluated)))
        evaluated.to_mesh_clear()
    if len(surfaces) != 1:
        raise ValueError('Expected one evaluated unit surface')
    part, mesh = surfaces[0]
    mesh.transform(part.matrix_world)
    obj = bpy.data.objects.new('SkinCanonicalSurface', mesh)
    scene.collection.objects.link(obj)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.025)
    bpy.ops.object.mode_set(mode='OBJECT')
    mesh.calc_loop_triangles()
    positions = np.array([v.co[:] for v in mesh.vertices], dtype=np.float64)
    triangles = np.array([t.vertices[:] for t in mesh.loop_triangles], dtype=np.uint32)
    # Split only UV seams; normals are still accumulated on the welded surface.
    split, uvs, indices, lookup = [], [], [], {}
    for triangle in mesh.loop_triangles:
        for loop_id in triangle.loops:
            vertex = mesh.loops[loop_id].vertex_index
            uv = tuple(mesh.uv_layers.active.data[loop_id].uv)
            key = (vertex, uv)
            if key not in lookup:
                lookup[key] = len(split)
                split.append(vertex)
                uvs.append((uv[0], 1.0 - uv[1]))  # image origin is top-left
            indices.append(lookup[key])
    homogeneous = np.column_stack((positions, np.ones(len(positions))))
    inverse = np.linalg.inv(influence_matrices(parts))
    local = np.einsum('pij,vj->pvi', inverse, homogeneous)
    centers = np.zeros((len(parts), 3))
    radii = np.array([o.data.elements[0].radius for o in parts])
    distance = np.linalg.norm(local[:, :, :3] - centers[:, None, :], axis=2) / radii[:, None]
    # Smooth fixed influences; retaining four closest lobes avoids distant limbs
    # dragging painted patches around. At the bind pose this reproduces the
    # canonical surface exactly because each transform cancels its inverse.
    weights = np.maximum(distance, 0.1) ** -6
    keep = np.argsort(weights, axis=0)[-4:]
    mask = np.zeros_like(weights)
    np.put_along_axis(mask, keep, 1, axis=0)
    weights *= mask
    weights /= weights.sum(axis=0)
    return (positions, triangles, np.array(split), np.array(uvs), np.array(indices),
            local, weights, [o.name for o in parts])


def export_model(output, model):
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError('Use Blender 3.6.23 for reproducible legacy imports')
    output.mkdir(parents=True, exist_ok=True)
    size, clips = MODELS[model]
    _, scene = open_source(model, clips[0])
    points, triangles, split, uvs, indices, local, weights, names = canonical_surface(scene)
    manifest = {'format': 'GSK1', 'experimental': True, 'uvLayout': model + '-v1',
                'blender': bpy.app.version_string, 'framesPerDirection': 32,
                'directions': 8, 'logicalSize': size, 'clips': {}}
    for clip in clips:
        source, scene = open_source(model, clip)
        parts = components(scene)
        if [o.name for o in parts] != names:
            raise ValueError('Unit component identity differs across clips')
        camera = np.linalg.inv(np.array(scene.camera.matrix_world))
        # Blender 2.34 orthographic size depended on camera depth and lens.
        # Modern import sets a different ortho_scale (and prints a warning).
        # Use the same legacy projection documented by unit-animation/render.py.
        scale = -camera[2, 3] * 32.0 / scene.camera.data.lens
        if scale <= 0:
            raise ValueError('Invalid legacy camera projection')
        frames, bounds = [], []
        model_view = camera.flatten().tolist()
        for direction in range(8):
            for phase in range(32):
                t = 1 + direction * 8 + phase / 4
                scene.frame_set(int(t), subframe=t % 1)
                matrices = influence_matrices(parts)
                positions = np.einsum('pvi,pv->vi', np.einsum('pij,pvj->pvi', matrices, local), weights)[:, :3]
                n = normals(positions, triangles) @ camera[:3, :3].T
                view = np.column_stack((positions, np.ones(len(positions)))) @ camera.T
                # Orthographic clip coordinates: preserve source camera and image
                # canvas. Depth is bounded independently of screen placement.
                projected = np.column_stack((view[:, 0] * 2 / scale,
                                             view[:, 1] * 2 / scale, -view[:, 2] / 100))
                if model == 'explorer':
                    projected[:, 0] -= 1.0 / size
                    projected[:, 1] -= 1.0 / size
                packed = np.column_stack((projected[split], n[split])).astype('<f4')
                if not np.isfinite(packed).all() or np.abs(projected[:, 2]).max() >= 1:
                    raise ValueError('Invalid projected mesh')
                frames.append(packed)
                bounds.append([projected[:, :2].min(axis=0).tolist(), projected[:, :2].max(axis=0).tolist()])
        path = output / (model + '-' + clip + '.gsk')
        with path.open('wb') as stream:
            stream.write(struct.pack('<4sIIII', b'GSK1', len(split), len(indices), len(frames), size))
            stream.write(uvs.astype('<f4').tobytes())
            stream.write(indices.astype('<u4').tobytes())
            for frame in frames:
                stream.write(frame.tobytes())
        manifest['clips'][clip] = {'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                   'source': str(source.relative_to(ROOT)),
                                   'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                                   'vertices': len(split), 'triangles': len(indices) // 3,
                                   'projectedBounds': bounds, 'cameraScale': scale,
                                   'sourceViewMatrix': model_view}
        print('Exported', path, flush=True)
    (output / (model + '-manifest.json')).write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--models', nargs='+', choices=tuple(MODELS), default=list(MODELS))
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    for model in args.models:
        export_model(args.output.resolve(), model)
