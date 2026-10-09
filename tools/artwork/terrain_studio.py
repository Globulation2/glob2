#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded source-image to portable Terrain Studio sheet conversion.

Uses the same premultiplied downsampling and shared terrain perimeter as shipped
material production. This tool never edits installed assets.
"""
import argparse
import json
import math
from pathlib import Path
from PIL import Image
from material_tiles import box_down, share_perimeter, tiles_to_sheet


def frames(source, kind):
    source = source.convert('RGBA')
    if kind == 'terrain':
        if source.getextrema()[3] != (255, 255):
            raise ValueError('Terrain source must be fully opaque')
        sheet = box_down(source, 64)
        tiles = [sheet.crop((x*32, y*32, (x+1)*32, (y+1)*32)) for y in range(2) for x in range(2)]
        return share_perimeter(tiles)
    columns, rows = (2, 3) if kind == 'resource' else (2, 2)
    tiles = []
    for y in range(rows):
        for x in range(columns):
            crop = source.crop((x*source.width//columns, y*source.height//rows,
                                (x+1)*source.width//columns, (y+1)*source.height//rows))
            alpha = crop.getchannel('A')
            values = list(alpha.getdata())
            if sum(v < 8 for v in values) < len(values)*0.05:
                raise ValueError('Resource/decor source needs a transparent background')
            bbox = alpha.point(lambda v: 255 if v > 16 else 0).getbbox()
            if not bbox:
                raise ValueError('Missing resource/decor frame')
            if bbox[0] < 2 or bbox[1] < 2 or bbox[2] > crop.width-2 or bbox[3] > crop.height-2:
                raise ValueError('Sprite touches a frame edge; regenerate with more padding')
            obj = crop.crop(bbox)
            # Same cell scale across the source grid; do not enlarge immature stock stages.
            scale = min(60/crop.width, 60/crop.height)
            obj = obj.convert('RGBa').resize((max(1, round(obj.width*scale)), max(1, round(obj.height*scale))), Image.Resampling.BOX).convert('RGBA')
            tile = Image.new('RGBA', (64, 64))
            tile.paste(obj, ((64-obj.width)//2, 62-obj.height))
            tiles.append(tile)
    return tiles


def process(source, kind, phases=1):
    if phases not in range(1, 5):
        raise ValueError('Expected one to four animation phases')
    base = frames(source, kind)
    if kind == 'decor':
        phases = 1
    output = []
    for phase in range(phases):
        gain = 1 if phases == 1 else 0.92 + 0.08*math.cos(2*math.pi*phase/phases)
        for tile in base:
            copy = tile.copy()
            # A coherent glow pulse; structure, edges and alpha never move.
            pixels = [(min(255, round(r*gain)), min(255, round(g*gain)), min(255, round(b*gain)), a)
                      for r, g, b, a in copy.getdata()]
            copy.putdata(pixels)
            output.append(copy)
    return tiles_to_sheet(output, columns=len(base))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--kind', choices=('terrain', 'resource', 'decor'), required=True)
    parser.add_argument('--phases', type=int, default=1)
    args = parser.parse_args()
    if args.input.stat().st_size > 16*1024*1024:
        raise ValueError('Source image exceeds 16 MiB')
    with Image.open(args.input) as source:
        if source.format != 'PNG' or source.width > 2048 or source.height > 2048 or source.width < 64 or source.height < 64:
            raise ValueError('Expected a bounded PNG source image')
        sheet = process(source, args.kind, args.phases)
        sheet.save(args.output, format='PNG')
        pixels = list(sheet.getdata())
        total_alpha = sum(p[3] for p in pixels)
        average = tuple(round(sum(p[c]*p[3] for p in pixels)/total_alpha) for c in range(3)) if total_alpha else (0, 0, 0)
        print(json.dumps({'frameWidth': 32 if args.kind == 'terrain' else 64,
                          'frameHeight': 32 if args.kind == 'terrain' else 64, 'color': average}))


if __name__ == '__main__':
    main()
