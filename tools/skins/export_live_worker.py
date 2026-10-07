#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export a prototype walking-worker control rig in pinned Blender 3.6.23.

blender --background --factory-startup -t 1 --python tools/skins/export_live_worker.py -- --output artifacts/live-worker
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
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).parent))
from export_units import ROOT, open_source, components, influence_matrices, sample_pose
from limb_surface import LimbSurface


def plain(value):
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, (tuple, list)):
        return [plain(x) for x in value]
    return value


def export(output):
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError('Use Blender 3.6.23')
    output = output.resolve()
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / 'artifacts'):
        raise ValueError('Prototype outputs belong outside the repository or under artifacts/')
    source, scene = open_source('worker', 'walk')
    parts = components(scene)
    if len(parts) != 13:
        raise ValueError('Expected 13 worker controls')
    definition_path = ROOT / 'datasrc/gfx/authored/skins/limb-surfaces.json'
    definition = json.loads(definition_path.read_text())['worker']
    surface = LimbSurface(definition)
    heading = scene.objects['RotEmpty']
    origin = float(heading.rotation_euler.z)
    pivot = np.array(heading.matrix_world.translation)
    camera = np.linalg.inv(np.array(scene.camera.matrix_world))
    scale = -camera[2, 3] * 32 / scene.camera.data.lens
    projection = camera.copy()
    projection[0] *= 2 / scale
    projection[1] *= 2 / scale
    projection[2] *= -1 / 100
    radii = np.array([p.data.elements[0].radius for p in parts])
    stiffness = np.array([p.data.elements[0].stiffness for p in parts])
    threshold = parts[0].data.threshold
    samples, matrices = [], []
    for phase in range(64):
        family = phase // 32
        sample_pose(scene, family, phase % 32, origin)
        values = influence_matrices(parts)
        angle = family * math.pi / 4
        c, z = math.cos(angle), math.sin(angle)
        undo = np.array([[c, -z, 0], [z, c, 0], [0, 0, 1]])
        values[:, :3, :3] = undo @ values[:, :3, :3]
        values[:, :3, 3] = (values[:, :3, 3] - pivot) @ undo.T + pivot
        matrices.append(values)
        controls = []
        for m in values:
            s = np.linalg.norm(m[:3, :3], axis=0).mean()
            q = Matrix((m[:3, :3] / s).tolist()).to_quaternion()
            controls.append([*m[:3, 3], *q, s])
        samples.append(controls)
    reference = ROOT / 'data/skins/colony-v1/worker-walk.gsk'
    blob = reference.read_bytes()
    magic, vertices, indices, frames, logical = struct.unpack_from('<4sIIII', blob)
    if (magic, vertices, frames, logical) != (b'GSK1', len(surface.vertices), 256, 38):
        raise ValueError('Unexpected worker reference geometry')
    uv = np.frombuffer(blob, '<f4', count=vertices*2, offset=20).reshape(-1, 2)
    idx = np.frombuffer(blob, '<u4', count=indices, offset=20+vertices*8)
    if not np.array_equal(uv, surface.uv.astype('<f4')) or not np.array_equal(idx, surface.triangles.flatten()):
        raise ValueError('Reference topology or paint coordinates differ')
    poses = np.frombuffer(blob, '<f4', offset=20+vertices*8+indices*4).reshape(256, vertices, 6)
    max_position = max_normal = 0.0
    heading_offsets = []
    for direction in range(8):
        direction_position = direction_normal = 0.0
        angle = -direction * math.pi / 4
        c, s = math.cos(angle), math.sin(angle)
        rotation = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
        family = direction % 2
        sample_pose(scene, direction, 0, origin)
        actual_controls = influence_matrices(parts)
        expected_root = (matrices[family*32][0, :3, 3] - pivot) @ rotation.T + pivot
        offset = actual_controls[0, :3, 3] - expected_root
        heading_offsets.append(offset.tolist())
        for phase, values in enumerate(matrices[family*32:family*32+32]):
            headed = values.copy()
            headed[:, :3, :3] = rotation @ values[:, :3, :3]
            headed[:, :3, 3] = (values[:, :3, 3] - pivot) @ rotation.T + pivot + offset
            positions = surface.evaluate(headed, radii, stiffness, threshold)
            normals = surface.normals(positions) @ camera[:3, :3].T
            projected = np.column_stack((positions, np.ones(vertices))) @ projection.T
            actual = poses[direction*32+phase]
            direction_position = max(direction_position, float(np.max(np.abs(projected[:, :2]-actual[:, :2])))*logical/2)
            max_position = max(max_position, direction_position)
            direction_normal = max(direction_normal, float(np.max(np.abs(normals-actual[:, 3:]))))
            max_normal = max(max_normal, direction_normal)
        print(f"heading {direction}: {direction_position} px / {direction_normal} normal", flush=True)
    if max_position >= .05 or max_normal >= .001:
        raise ValueError(f'Heading reconstruction rejected: position={max_position}px normal={max_normal}')
    record = dict(version=1, model='worker', clip='walk', logicalSize=logical,
                  controls=13, phases=64, cycleSourceFrames=16, headingOffsets=heading_offsets,
                  definition=definition, descriptors=plain(surface.vertices),
                  regions=surface.regions.tolist(), uv=surface.uv.tolist(),
                  indices=surface.triangles.flatten().tolist(),
                  radii=radii.tolist(), stiffness=stiffness.tolist(), threshold=threshold,
                  pivot=pivot.tolist(), modelToClip=projection.flatten().tolist(),
                  normalToCamera=camera[:3, :3].flatten().tolist(), samples=samples,
                  provenance=dict(blender=bpy.app.version_string,
                      sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                      referenceSha256=hashlib.sha256(blob).hexdigest(),
                      exporterSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      surfaceSha256=hashlib.sha256((Path(__file__).parent/'limb_surface.py').read_bytes()).hexdigest(),
                      definitionSha256=hashlib.sha256(definition_path.read_bytes()).hexdigest()))
    encoded = (json.dumps(record, separators=(',', ':'), allow_nan=False)+'\n').encode()
    if len(encoded) > 1024*1024:
        raise ValueError('Rig exceeds 1 MiB')
    output.mkdir(parents=True, exist_ok=True)
    (output/'worker-walk.live.json').write_bytes(encoded)
    (output/'export-verification.json').write_text(json.dumps(dict(positionPixels=max_position, normalError=max_normal, bytes=len(encoded)), indent=2)+'\n')
    print(f'Exported {len(encoded)} bytes; max position {max_position:.8f}px; normal {max_normal:.8f}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    export(parser.parse_args(sys.argv[sys.argv.index('--')+1:]).output)
