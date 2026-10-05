#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build decorative whole-cell borders from the prototype's ice and trail textures.

The 32x32 frame always remains inside the receiving cell. Bits N/E/S/W select
four-pixel-wide edge bands, so visual blending never changes gameplay coverage.
Run from the repository root; output slots match TerrainPresentation.h.
"""
from pathlib import Path
from PIL import Image

TILE_SIZE = 32
ALPHA_RAMP = (144, 96, 48, 16)
TRAIL_SOURCE_FRAME = 288
# Source frame and first output mask, matching TerrainPresentation.h.
BORDER_FRAMES = ((272, 304), (TRAIL_SOURCE_FRAME, 319))


def trail_fraying(x, y):
    """A repeatable alpha reduction; no randomness or neighboring-cell state."""
    return (((x * 37) ^ (y * 53) ^ (x * y * 7)) & 3) * 8


def main():
    root = Path(__file__).resolve().parents[1] / "data/gfx"
    last = TILE_SIZE - 1
    for source, first in BORDER_FRAMES:
        with Image.open(root / f"terrain{source}.png") as image:
            texture = image.convert("RGBA")
        for mask in range(1, 16):
            result = texture.copy()
            for y in range(TILE_SIZE):
                for x in range(TILE_SIZE):
                    distances = (y, last-x, last-y, x)
                    distance = min(distances[side] for side in range(4) if mask & (1 << side))
                    # Fade only beyond the material boundary; a subtly ragged
                    # last pixel preserves the original artwork's coarse grain.
                    strength = ALPHA_RAMP[distance] if distance < len(ALPHA_RAMP) else 0
                    if source == TRAIL_SOURCE_FRAME and 1 <= distance < len(ALPHA_RAMP):
                        # Trail shoulders fray into the neighboring material.
                        # Coordinate-only variation keeps the recipe repeatable;
                        # the outer edge remains continuous and inside the cell.
                        strength = max(0, strength - trail_fraying(x, y))
                    r,g,b,_ = texture.getpixel((x,y))
                    result.putpixel((x,y), (r,g,b,strength))
            result.save(root / f"terrain{first+mask-1}.png")


if __name__ == "__main__":
    main()
