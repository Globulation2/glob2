#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rasterise the strategic-view map icons into the game's sprite frames.

Development-only; ordinary builds use the committed PNGs. Needs rsvg-convert
(librsvg). Run from the repository root:

    python3 tools/icons/export_map_icons.py

Sources are datasrc/icons/map/<name>.svg. Output is data/gfx/mapicon<N>.png,
frame N = icon * len(SIZES) + size, matching MapOverlayQueue's icon lookup:
keep ICONS and SIZES in step with src/render/MapOverlayQueue.cpp.
"""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
# In IntBuildingType order (swarm .. clearing flag), then the market.
ICONS = ["hive", "inn", "hospital", "racetrack", "pool", "barracks", "school", "tower",
         "flag-explore", "flag-war", "flag-clear", "market"]
# Edge lengths in target pixels; the renderer picks the largest that fits a chip.
SIZES = [10, 12, 16, 20, 24, 32, 48]


def main():
    for icon, name in enumerate(ICONS):
        source = ROOT / "datasrc/icons/map" / f"{name}.svg"
        for index, size in enumerate(SIZES):
            frame = icon * len(SIZES) + index
            subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), str(source),
                            "-o", str(ROOT / "data/gfx" / f"mapicon{frame}.png")], check=True)
    print(f"wrote {len(ICONS) * len(SIZES)} frames")


if __name__ == "__main__":
    main()
