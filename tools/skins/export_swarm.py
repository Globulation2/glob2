#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create a small paintable swarm surface from the retained TRELLIS source.

Run with Blender 3.6.23 --python-exit-code 1 --python tools/skins/export_swarm.py
-- --output artifacts/skins/units. Source topology/textures remain untouched.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]


def export(output):
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError('Use Blender 3.6.23')
    source = ROOT / 'datasrc/gfx/buildings/swarm-trellis.glb'
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(source))
    objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Source has UV seams and duplicate triangles. Welding before simplification
    # avoids tearing them; our new paint layout is made only after simplification.
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
    bm.to_mesh(mesh)
    bm.free()
    mesh.calc_loop_triangles()
    original_triangles = len(mesh.loop_triangles)
    # The generated source has non-manifold folds that prevent collapse from
    # reaching a useful budget. Reconstruct a closed surface before decimation.
    remesh = obj.modifiers.new('SkinClosedSurface', 'REMESH')
    remesh.mode = 'SHARP'
    remesh.octree_depth = 7
    remesh.use_remove_disconnected = True
    remesh.threshold = 0.05
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    mesh = obj.data
    mesh.calc_loop_triangles()
    reconstructed_triangles = len(mesh.loop_triangles)
    modifier = obj.modifiers.new('SkinSurfaceBudget', 'DECIMATE')
    modifier.ratio = min(1.0, 2000 / reconstructed_triangles)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.025)
    bpy.ops.object.mode_set(mode='OBJECT')
    mesh.calc_loop_triangles()
    mesh.calc_normals()
    positions = np.array([v.co[:] for v in mesh.vertices])
    low, high = positions.min(axis=0), positions.max(axis=0)
    positions -= (low + high) / 2
    # Orthographic camera matches the game's elevated view. Fit the projected
    # surface into its sprite canvas; visual calibration remains explicit.
    rotation = np.array(Vector((6, -8, 10)).to_track_quat('Z', 'Y').to_matrix()).T
    view = positions @ rotation.T
    extent = np.ptp(view[:, :2], axis=0).max()
    view[:, :2] *= 1.8 / extent
    view[:, 2] *= -0.5 / max(np.ptp(view[:, 2]), 1e-6)
    normals = np.array([v.normal[:] for v in mesh.vertices]) @ rotation.T
    vertices, uv, indices, lookup = [], [], [], {}
    for tri in mesh.loop_triangles:
        for loop_id in tri.loops:
            vertex = mesh.loops[loop_id].vertex_index
            tex = tuple(mesh.uv_layers.active.data[loop_id].uv)
            key = vertex, tex
            if key not in lookup:
                lookup[key] = len(vertices)
                vertices.append([*view[vertex], *normals[vertex]])
                uv.append((tex[0], 1-tex[1]))
            indices.append(lookup[key])
    if len(vertices) > 8192 or len(indices) > 49152:
        raise ValueError(f'Swarm exceeds runtime geometry budget: {len(vertices)} vertices, {len(indices)//3} triangles from {original_triangles}')
    output.mkdir(parents=True, exist_ok=True)
    path = output / 'swarm.gsk'
    with path.open('wb') as stream:
        stream.write(struct.pack('<4sIIII', b'GSK1', len(vertices), len(indices), 1, 96))
        stream.write(np.array(uv, dtype='<f4').tobytes())
        stream.write(np.array(indices, dtype='<u4').tobytes())
        stream.write(np.array(vertices, dtype='<f4').tobytes())
    metadata = {'format': 'GSK1', 'experimental': True, 'uvLayout': 'swarm-v1',
                'source': str(source.relative_to(ROOT)),
                'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'sourceTriangles': original_triangles, 'reconstructedTriangles': reconstructed_triangles, 'triangles': len(indices)//3,
                'vertices': len(vertices), 'logicalSize': 96, 'frames': 1,
                'cameraDirection': [6, -8, 10], 'cameraFit': '90% square canvas'}
    (output / 'swarm-mesh.json').write_text(json.dumps(metadata, indent=2)+'\n')
    print(json.dumps(metadata), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    export(parser.parse_args(sys.argv[sys.argv.index('--')+1:]).output.resolve())
