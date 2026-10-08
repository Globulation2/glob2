#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit every worker/warrior pose under Blender; --report saves review evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).parent))
import numpy as np
from mathutils.bvhtree import BVHTree
from skin_assets import UNIT_CLIPS, read_gsk, ROOT
from surface_contract import validate_surface


def read_shapes(path):
    from fit_unit_shapes import evaluate
    blob = path.read_bytes()
    magic, vertices, indices, count, normal_count, clips, _, payload = struct.unpack_from('<4s7I', blob)
    assert magic == b'GSB1' and clips == 1 and payload == len(blob) - 32
    offset = 32 + vertices * 8
    triangles = np.frombuffer(blob, '<u4', indices, offset).reshape(-1, 3)
    offset += indices * 4
    mean = np.frombuffer(blob, '<f4', vertices * 3, offset).reshape(-1, 3).astype(float)
    offset += vertices * 12
    normal_mean = np.frombuffer(blob, '<f4', vertices * 3, offset).reshape(-1, 3).astype(float)
    offset += vertices * 12
    basis = []
    for _ in range(count + normal_count):
        scale = struct.unpack_from('<f', blob, offset)[0]
        offset += 4
        basis.append(np.frombuffer(blob, '<i2', vertices * 3, offset).reshape(-1, 3).astype(float) * scale)
        offset += vertices * 6
    _, frames = struct.unpack_from('<2I', blob, offset)
    assert frames == 256
    offset += 8
    camera = np.frombuffer(blob, '<f4', 16, offset).copy(); offset += 64
    normals = np.frombuffer(blob, '<f4', 9, offset).copy(); offset += 36
    headings = np.frombuffer(blob, '<f4', frames, offset); offset += frames * 4
    coefficients = np.frombuffer(blob, '<f4', frames * count, offset).reshape(frames, count); offset += frames * count * 4
    normal_coefficients = np.frombuffer(blob, '<f4', frames * normal_count, offset).reshape(frames, normal_count); offset += frames * normal_count * 4
    assert offset == len(blob)
    poses = np.array([evaluate(mean, np.array(basis[:count]), c, normal_mean, np.array(basis[count:]), cn, heading, camera, normals)
        for c, cn, heading in zip(coefficients, normal_coefficients, headings)])
    return triangles, poses, np.linalg.inv(camera.reshape(4, 4)), np.linalg.inv(normals.reshape(3, 3))


def audit(root, fitted=False, models=('worker', 'warrior')):
    reports = {}
    authored = json.loads((ROOT / 'datasrc/gfx/authored/skins/limb-paint-charts.json').read_text())
    for model in models:
        contract = json.loads((root / (model + '-surface.json')).read_text())
        welds = np.array(contract['welds'])
        for clip in UNIT_CLIPS[model]:
            name = model + '-' + clip
            path = root / (name + ('.gsb' if fitted else '.gsk'))
            baked = root / (name + '.gsk')
            uv, triangles, poses, _ = read_gsk(baked)
            validate_surface(baked.read_bytes(), contract)
            assert np.array_equal(uv, np.asarray(authored[model], dtype=np.float32)), 'saved paint coordinates moved'
            if fitted:
                triangles, poses, inverse, normals = read_shapes(path)
            else:
                manifest = root / (model + '-manifest.json')
                if manifest.exists():
                    record = json.loads(manifest.read_text())['clips'][clip]
                    camera = np.array(record['sourceViewMatrix']).reshape(4, 4)
                    projection = np.diag([2 / record['cameraScale'], 2 / record['cameraScale'], -.01, 1]) @ camera
                    inverse, normals = np.linalg.inv(projection), np.linalg.inv(camera[:3, :3])
                else:
                    view = json.loads(baked.with_suffix('.view.json').read_text())
                    inverse = np.array(view['clipToModel']).reshape(4, 4)
                    normals = np.array(view['normalToModel']).reshape(3, 3)
            sets = [set(t) for t in welds[triangles]]
            min_area, worst_normal = float('inf'), 0.
            for frame, pose in enumerate(poses):
                assert np.isfinite(pose).all()
                assert np.max(np.abs(np.linalg.norm(pose[:, 3:], axis=1) - 1)) < 1e-4
                p = (np.c_[pose[:, :3], np.ones(len(pose))] @ inverse.T)[:, :3]
                tree = BVHTree.FromPolygons(p.tolist(), triangles.tolist(), all_triangles=True, epsilon=0.)
                pairs = [(a, b) for a, b in tree.overlap(tree) if a < b and not sets[a] & sets[b]]
                assert not pairs, f'{name} frame {frame}: {len(pairs)} non-adjacent intersections'
                face = np.cross(p[triangles[:, 1]] - p[triangles[:, 0]], p[triangles[:, 2]] - p[triangles[:, 0]])
                min_area = min(min_area, float(np.linalg.norm(face, axis=1).min()))
                assert min_area > 1e-9, 'collapsed joint triangle'
                if not fitted:
                    expected = np.zeros_like(p)
                    for k in range(3): np.add.at(expected, welds[triangles[:, k]], face)
                    expected = expected[welds]
                    expected /= np.linalg.norm(expected, axis=1)[:, None]
                    actual = pose[:, 3:] @ normals.T
                    worst_normal = max(worst_normal, float(np.linalg.norm(expected - actual, axis=1).max()))
                    assert worst_normal < .002, 'normals differ from welded geometry'
            reports[name] = dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(), frames=len(poses),
                nonAdjacentIntersections=0, minimumDoubleArea=min_area, maximumNormalError=worst_normal)
            print(name, 'all', len(poses), 'poses passed', flush=True)
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--fitted', action='store_true')
    parser.add_argument('--models', nargs='+', choices=('worker', 'warrior'), default=['worker', 'warrior'])
    parser.add_argument('--report', type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    results = audit(args.root, args.fitted, args.models)
    if args.report: args.report.write_text(json.dumps(results, indent=2) + '\n')
