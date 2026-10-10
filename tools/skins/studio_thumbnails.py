#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate Studio selectors from production sprite bundles or legacy GSKs.

For rigs, use --sprite-bundle with a neutral-paint assets render-skin export. This
uses the production loader/evaluator/renderer instead of another rig decoder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from skin_assets import ROOT, UNIT_CLIP_NAMES

CLIPS = (*UNIT_CLIP_NAMES, 'swarm')


def from_bundle(bundle, output, clips):
    from PIL import Image
    manifest = json.loads((bundle / 'manifest.json').read_text())
    if manifest.get('format') != 'colony-sprites-v1' or manifest.get('tileSize') != 128:
        raise ValueError('Unsupported production sprite bundle')
    # Check every requested page before replacing any selectors.
    images = {}
    for clip in clips:
        page = next(p for p in manifest['pages'] if p['clip'] == clip and p['first'] == 0)
        digest = page['sha256']
        if not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('Invalid sprite page hash')
        path = bundle / (digest + '.webp')
        if path.stat().st_size > 4 * 1024 * 1024 or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Sprite page hash mismatch')
        with Image.open(path) as image:
            if image.size != (page['width'], page['height']) or image.width not in (128, 1024) or image.width != image.height:
                raise ValueError('Invalid sprite page dimensions')
            images[clip] = image.crop((0, 0, 128, 128)).convert('RGBA').resize((96, 96), Image.Resampling.LANCZOS)
    output.mkdir(parents=True, exist_ok=True)
    for clip, image in images.items():
        image.save(output / (clip + '.png'))


def from_legacy(output):
    import numpy as np
    from swarm_metrics import rasterize, write_png
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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sprite-bundle', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'platform/apps/web/public/skins/thumbs')
    parser.add_argument('--clip', choices=CLIPS, action='append')
    args = parser.parse_args()
    if args.sprite_bundle:
        from_bundle(args.sprite_bundle, args.output, args.clip or CLIPS)
    elif args.clip:
        parser.error('--clip requires --sprite-bundle')
    else:
        from_legacy(args.output)
