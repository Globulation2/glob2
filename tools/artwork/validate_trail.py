#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Trail's installed classic artwork and provenance without writing files."""
import hashlib
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "datasrc/gfx/trail"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_frame(slot):
    path = ROOT / f"data/gfx/terrain{slot}.png"
    with Image.open(path) as image:
        require(image.size == (32, 32), f"{path.name}: expected 32x32")
        require(image.mode == "RGBA", f"{path.name}: expected RGBA")
        return image.copy()


def main():
    provenance = json.loads((SOURCE / "provenance.json").read_text())
    source = SOURCE / provenance["source"]
    require(hashlib.sha256(source.read_bytes()).hexdigest() ==
            provenance["source_sha256"], "Source hash differs from provenance")
    expected = {f"data/gfx/terrain{slot}.png"
                for slot in (*range(288, 304), *range(319, 334))}
    require(set(provenance["runtime_sha256"]) == expected,
            "Provenance must cover exactly the 16 bases and 15 edge masks")
    for name, digest in provenance["runtime_sha256"].items():
        require(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest,
                f"{name}: hash differs from provenance")

    bases = [load_frame(slot) for slot in range(288, 304)]
    require(len({tile.tobytes() for tile in bases}) == 16,
            "Base variants must be distinct")
    for tile in bases:
        require(tile.getchannel("A").getextrema() == (255, 255),
                "Base tiles must be opaque")
        for neighbor in bases:
            require(tile.crop((31, 0, 32, 32)).tobytes() ==
                    neighbor.crop((0, 0, 1, 32)).tobytes(),
                    "Horizontal variant join differs")
            require(tile.crop((0, 31, 32, 32)).tobytes() ==
                    neighbor.crop((0, 0, 32, 1)).tobytes(),
                    "Vertical variant join differs")

    for mask in range(1, 16):
        alpha = load_frame(318 + mask).getchannel("A")
        # Bits N/E/S/W select four-pixel bands in the receiving cell.
        # This checks coverage independently of the recipe's fade and grain.
        for y in range(32):
            for x in range(32):
                distances = (y, 31 - x, 31 - y, x)
                covered = any(mask & (1 << side) and distance < 4
                              for side, distance in enumerate(distances))
                value = alpha.getpixel((x, y))
                require(value < 255, f"Mask {mask}: overlay must blend")
                require(covered or value == 0,
                        f"Mask {mask}: pixel outside selected bands")
                if any(mask & (1 << side) and distance == 0
                       for side, distance in enumerate(distances)):
                    require(value > 0, f"Mask {mask}: selected edge has a gap")
    print("PASS Trail provenance, 16 opaque variants, all joins and 15 edge masks")


if __name__ == "__main__":
    main()
