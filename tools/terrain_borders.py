#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build decorative whole-cell borders from the prototype's ice and road textures.

The 32x32 frame always remains inside the receiving cell. Bits N/E/S/W select
four-pixel-wide edge bands, so visual blending never changes gameplay coverage.
Run from the repository root; output slots match TerrainPresentation.h.
"""
from pathlib import Path
from PIL import Image

def main():
    root = Path(__file__).resolve().parents[1] / "data/gfx"
    for source, first in ((272, 304), (288, 319)):
        texture = Image.open(root / f"terrain{source}.png").convert("RGBA")
        for mask in range(1, 16):
            result = texture.copy()
            for y in range(32):
                for x in range(32):
                    distances = (y, 31-x, 31-y, x)
                    distance = min(distances[side] for side in range(4) if mask & (1 << side))
                    # Fade only beyond the material boundary; a subtly ragged
                    # last pixel preserves the original artwork's coarse grain.
                    strength = (144, 96, 48, 16)[distance] if distance < 4 else 0
                    r,g,b,_ = texture.getpixel((x,y))
                    result.putpixel((x,y), (r,g,b,strength))
            result.save(root / f"terrain{first+mask-1}.png")

if __name__ == "__main__":
    main()
