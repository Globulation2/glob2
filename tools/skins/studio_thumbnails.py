#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Small UI thumbnails derived from shipped meshes; requires NumPy."""
from pathlib import Path
import struct
import numpy as np
from swarm_metrics import rasterize, write_png

ROOT = Path(__file__).resolve().parents[2]
output = ROOT / 'platform/apps/web/public/skins/thumbs'
output.mkdir(parents=True, exist_ok=True)
for path in sorted((ROOT / 'data/skins/colony-v1').glob('*.gsk')):
    data = path.read_bytes()
    _, count, index_count, frames, _ = struct.unpack_from('<4sIIII', data)
    indices = np.frombuffer(data, '<u4', index_count, 20 + count * 8).reshape(-1, 3)
    pose = np.frombuffer(data, '<f4', count * 6, 20 + count * 8 + index_count * 4).reshape(-1, 6)
    screen = np.column_stack(((pose[:, 0] / 1.25 + 1) * 48, (1 - pose[:, 1] / 1.25) * 48))
    ids = rasterize(screen, pose[:, 2], indices, 96)
    normals = pose[indices, 3:].mean(axis=1)
    normals /= np.maximum(1e-8, np.linalg.norm(normals, axis=1))[:, None]
    light = np.array([-.4, .7, 1]); light /= np.linalg.norm(light)
    half = light + [0,0,1]; half /= np.linalg.norm(half)
    diffuse = .3 + .65 * np.maximum(0, normals @ light)
    shine = .3 * np.maximum(0, normals @ half)**4
    colors = diffuse[:, None] * np.array([.88,.78,.62]) + shine[:, None]
    image = np.zeros((96,96,4))
    seen = ids >= 0
    image[seen,:3] = colors[ids[seen]]
    image[seen,3] = 1
    write_png(output / (path.stem + '.png'), image)
