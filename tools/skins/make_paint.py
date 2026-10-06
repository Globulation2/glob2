#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create reproducible colony-v2 source fixtures and WebP preview assets.

A colony-v2 skin is a 512px colour atlas (paint.png) and a 512px greyscale
material map (material.png). Each holds four 256px quadrants, one per model:
worker top-left, warrior top-right, explorer bottom-left, swarm bottom-right.
Material map values are material ids (see MATERIALS).
"""
import argparse
import io
import json
from pathlib import Path
import struct
import zlib

SIZE = 512
QUADRANT = 256
REGISTRY = json.loads((Path(__file__).resolve().parents[2] / 'libgag/shaders/skin-materials.json').read_text())
MATERIALS = {m['key']: m['id'] for m in REGISTRY['materials']}
# Distinct per-model colours make quadrant mix-ups visible in previews.
MODEL_COLORS = (((45, 115, 180), (240, 190, 50)),   # worker
                ((180, 40, 40), (235, 235, 235)),   # warrior
                ((60, 160, 70), (250, 240, 120)),   # explorer
                ((120, 60, 170), (240, 150, 40)))   # swarm


def png(width, height, color_type, rows):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    raw = b''.join(b'\0' + row for row in rows)  # PNG row filter: none
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, color_type, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def pattern_index(pattern, x, y):
    if pattern == 'checker':
        return 2 if 116 <= x < 140 else (x // 16 + y // 16) % 2
    if pattern == 'stripes':
        return ((x + y) // 24) % 2
    if pattern == 'solid':
        return 0
    dx, dy = x % 48 - 24, y % 48 - 24
    return int(dx * dx + dy * dy < 120)


def paint(pattern, color=None):
    rows = []
    for y in range(SIZE):
        row = bytearray()
        for x in range(SIZE):
            model = (y // QUADRANT) * 2 + x // QUADRANT
            index = pattern_index(pattern, x % QUADRANT, y % QUADRANT)
            if color is not None:
                row.extend(color)
            else:
                row.extend((220, 30, 100) if index == 2 else MODEL_COLORS[model][index])
        rows.append(bytes(row))
    return png(SIZE, SIZE, 2, rows)


def material(name, pattern, only_pattern=False):
    """'mixed' gives each model vertical bands of every material on the pattern.
    only_pattern applies the material to the pattern's marks over a glossy body."""
    rows = []
    for y in range(SIZE):
        row = bytearray()
        for x in range(SIZE):
            if name == 'mixed':
                band = (x % QUADRANT) * len(MATERIALS) // QUADRANT
                row.append(band if pattern_index(pattern, x % QUADRANT, y % QUADRANT) else 0)
            elif only_pattern and not pattern_index(pattern, x % QUADRANT, y % QUADRANT):
                row.append(MATERIALS['glossy'])
            else:
                row.append(MATERIALS[name])
        rows.append(bytes(row))
    return png(SIZE, SIZE, 0, rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='directory for WebP assets and PNG source copies')
    parser.add_argument('--pattern', choices=('checker', 'stripes', 'spots', 'solid'), default='checker')
    parser.add_argument('--color', help='solid RGB colour as RRGGBB, replacing the pattern colours')
    parser.add_argument('--material', choices=(*MATERIALS, 'mixed'), default='glossy')
    parser.add_argument('--material-on-pattern', action='store_true',
                        help='apply --material only to the pattern marks; the rest stays glossy')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    color = tuple(bytes.fromhex(args.color)) if args.color else None
    from PIL import Image, features
    if not features.check('webp'):
        parser.error('Pillow must include WebP support')
    for name, source in [('paint', paint(args.pattern, color)),
                         ('material', material(args.material, args.pattern, args.material_on_pattern))]:
        # Source copies support image editing and the existing offline pixel
        # analysis tools. Native previews load only the lossless WebP pair.
        (args.output / (name + '.png')).write_bytes(source)
        with Image.open(io.BytesIO(source)) as image:
            image.convert('RGB').save(args.output / (name + '.webp'), format='WEBP',
                                      lossless=True, quality=75, method=4, exact=True)
