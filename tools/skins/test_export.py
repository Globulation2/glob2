#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate generated unit sets without Blender or third-party packages."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def validate_model(root, path):
    manifest = json.loads(path.read_text())
    reference = None
    for clip in manifest['clips']:
        record = manifest['clips'][clip]
        data = (root / record['file']).read_bytes()
        assert hashlib.sha256(data).hexdigest() == record['sha256']
        magic, vertices, indices, frames, size = struct.unpack_from('<4sIIII', data)
        assert magic == b'GSK1' and frames == 256 and size == manifest['logicalSize']
        assert 3 <= vertices <= 8192 and indices % 3 == 0
        end_uv = 20 + vertices * 8
        end_indices = end_uv + indices * 4
        assert len(data) == end_indices + frames * vertices * 24
        topology = data[20:end_indices]
        if reference is None:
            reference = topology
        assert topology == reference, 'UV or vertex identity changed between actions'
        uv = struct.unpack_from('<' + str(vertices * 2) + 'f', data, 20)
        assert all(math.isfinite(v) and 0 <= v <= 1 for v in uv)
        index = struct.unpack_from('<' + str(indices) + 'I', data, end_uv)
        assert max(index) < vertices
        stride = vertices * 24
        for direction in range(8):
            poses = set()
            for phase in range(32):
                start = end_indices + (direction * 32 + phase) * stride
                pose = data[start:start + stride]
                values = struct.unpack('<' + str(vertices * 6) + 'f', pose)
                assert all(math.isfinite(v) for v in values)
                assert all(-1 < values[i] < 1 for i in range(2, len(values), 6))
                poses.add(pose)
            assert len(poses) > 1, 'animation froze'
        print(manifest['uvLayout'] + '/' + clip + ': finite animated poses and shared UV/index topology verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    root = parser.parse_args().directory
    manifests = sorted(root.glob('*-manifest.json'))
    assert manifests, 'No unit manifests found'
    for path in manifests:
        validate_model(root, path)
