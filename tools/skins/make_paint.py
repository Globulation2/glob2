#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create reproducible 256px opaque paint fixtures without image dependencies."""
import argparse
from pathlib import Path
import struct
import zlib


def paint(pattern):
    colors = ((45, 115, 180), (240, 190, 50))
    rows = bytearray()
    for y in range(256):
        rows.append(0)  # PNG row filter: none
        for x in range(256):
            if pattern == 'checker':
                color = (220, 30, 100) if 116 <= x < 140 else colors[(x // 16 + y // 16) % 2]
            elif pattern == 'stripes':
                color = colors[((x + y) // 24) % 2]
            else:
                dx, dy = x % 48 - 24, y % 48 - 24
                color = colors[int(dx * dx + dy * dy < 120)]
            rows.extend(color)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 256, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--pattern', choices=('checker', 'stripes', 'spots'), default='checker')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(paint(args.pattern))
