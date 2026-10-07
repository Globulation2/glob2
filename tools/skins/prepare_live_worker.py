#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare reproducible live-worker viewer fixtures, without touching shipped assets."""
import argparse
import io
from pathlib import Path
import shutil
from PIL import Image, ImageDraw
from make_paint import paint, material

ROOT = Path(__file__).resolve().parents[2]


def prepare(output):
    output = output.resolve()
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT/'artifacts'):
        raise ValueError('Use artifacts/ or an external directory')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT/'data/skins/colony-v1/worker-walk.gsk', output/'worker-walk.gsk')
    for name, blob in [('paint', paint('checker')), ('material', material('mixed', 'checker'))]:
        image = Image.open(io.BytesIO(blob)).convert('RGB')
        image.save(output/(name+'.webp'), lossless=True, exact=True)
    neutral = Image.new('RGB', (512, 512), (170, 180, 190))
    neutral.save(output/'neutral.webp', lossless=True, exact=True)
    numbered = Image.new('RGB', (512, 512), (240, 240, 240))
    draw = ImageDraw.Draw(numbered)
    for y in range(16):
        for x in range(16):
            draw.rectangle((x*32,y*32,x*32+31,y*32+31), fill=(80,150,210) if (x+y)%2 else (240,190,70), outline=(20,20,20))
            draw.text((x*32+2,y*32+10), f'{x%8},{y%8}', fill=(0,0,0))
    numbered.save(output/'numbered.webp', lossless=True, exact=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    prepare(parser.parse_args().output)
