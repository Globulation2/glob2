#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export the generated Trail material into the stable classic terrain slots.

Only classic Trail frames are replaced; HD artwork and simulation are untouched.
Run from any directory. Requires Pillow (tools/asset-requirements.txt).
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
TILE_SIZE = 32
SHEET_TILES = 4
FIRST_FRAME = 288


def frames():
    with Image.open(ROOT / "datasrc/gfx/trail/material.png") as image:
        source = image.convert("RGB")
    # One 4x4 material sheet supplies sixteen non-directional variants. BOX
    # averages the native grit into the coarse classic 32px texture.
    sheet_size = TILE_SIZE * SHEET_TILES
    sheet = source.resize((sheet_size, sheet_size), Image.Resampling.BOX)
    tiles = [sheet.crop((x * TILE_SIZE, y * TILE_SIZE,
                        (x + 1) * TILE_SIZE, (y + 1) * TILE_SIZE))
             for y in range(SHEET_TILES) for x in range(SHEET_TILES)]
    # Equalize the perimeter to a shared, textured boundary so any two
    # variants can meet, including at the torus seam. Blend the next pixel
    # inward to avoid a sharp border around each independently chosen tile.
    shared = tiles[0].copy()
    for tile in tiles:
        last = TILE_SIZE - 1
        for y in range(TILE_SIZE):
            for x in range(TILE_SIZE):
                distance = min(x, y, last - x, last - y)
                if distance > 1:
                    continue
                # Opposite edges use the same coordinates. No directional
                # markings or gameplay-dependent neighbor selection needed.
                sx = 0 if x == last else x
                sy = 0 if y == last else y
                boundary = shared.getpixel((sx, sy))
                original = tile.getpixel((x, y))
                weight = 2 if distance == 0 else 1
                tile.putpixel((x, y), tuple((a * (2 - weight) + b * weight) // 2
                                           for a, b in zip(original, boundary)))
        yield tile.convert("RGBA")


def main():
    for slot, tile in enumerate(frames(), start=FIRST_FRAME):
        tile.save(ROOT / f"data/gfx/terrain{slot}.png")
    print("Exported sixteen 32x32 opaque Trail frames (288–303)")


if __name__ == "__main__":
    main()
