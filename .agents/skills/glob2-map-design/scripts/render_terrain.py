#!/usr/bin/env python3
"""Render a close-up of a generated map from its terrain dump, one colour per tile kind.

The native preview draws undermap corners at two pixels a tile and no buildings, which hides
beaches, one-corner sand lines, sealed plot edges and wall gaps. The terrain dump shows the tiles
the game will see. Make one with the structured command (sizes are exponents, 8 = 256):

  build/src/glob2 --generate-map --generator 39 --map-seed 7 --param teams=4 \
      --param width=8 --param height=8 --report terrain --output-dir /tmp/dump
  python3 render_terrain.py /tmp/dump/terrain.txt fort.png --centre 113 136 --radius 40 --scale 10

Codes (src/MapStudy.cpp): 0 pure grass, 1 pure sand, 2 pure water, 3 mixed (a beach or a
one-corner sand line: walkable, unbuildable, crops never cross it), 4 wheat, 5 wood, 6 stone,
7 building, 8 fruit, 9 algae. A resource hides the terrain under it.
"""
import argparse
from PIL import Image

COLOURS = {0: (46, 125, 50), 1: (215, 200, 140), 2: (25, 55, 150), 3: (150, 160, 110),
           4: (235, 210, 90), 5: (20, 70, 25), 6: (120, 120, 120), 7: (200, 40, 40),
           8: (220, 60, 160), 9: (40, 160, 160)}

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('dump')
parser.add_argument('output')
parser.add_argument('--centre', nargs=2, type=int, help='tile at the middle (default: map middle)')
parser.add_argument('--radius', type=int, help='tiles either side (default: the whole map)')
parser.add_argument('--scale', type=int, default=3, help='pixels per tile')
args = parser.parse_args()
with open(args.dump) as f:
    w, h = map(int, f.readline().split())
    grid = [list(map(int, line.split())) for line in f]
cx, cy = args.centre if args.centre else (w // 2, h // 2)
r = args.radius if args.radius else min(w, h) // 2
image = Image.new('RGB', (2 * r * args.scale, 2 * r * args.scale))
pixels = image.load()
for j in range(2 * r):
    for i in range(2 * r):
        colour = COLOURS[grid[(cy - r + j) % h][(cx - r + i) % w]]
        for b in range(args.scale):
            for a in range(args.scale):
                pixels[i * args.scale + a, j * args.scale + b] = colour
image.save(args.output)
