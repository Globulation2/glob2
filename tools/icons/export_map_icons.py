#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rasterise the strategic-view map icons and the status icons into the game's sprite frames.

Development-only; ordinary builds use the committed PNGs. Needs rsvg-convert
(librsvg). Run from the repository root:

    python3 tools/icons/export_map_icons.py

Sources are content-src/icons/map/<name>.svg. Output is content/data/gfx/mapicon<N>.png,
frame N = icon * len(SIZES) + size, matching MapOverlayQueue's icon lookup:
keep ICONS and SIZES in step with src/render/MapOverlayQueue.cpp.

Status icons, the badges that mark a unit or building needing attention, come
from content-src/icons/status/<name>.svg and go to content/data/gfx/statusicon<N>.png,
frame N = icon * len(STATUS_SIZES) + size, in MapOverlayQueue::StatusIcon order.
"""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
# In IntBuildingType order (swarm .. clearing flag), then the market.
ICONS = ["hive", "inn", "hospital", "racetrack", "pool", "barracks", "school", "tower",
         "flag-explore", "flag-war", "flag-clear", "market"]
# Edge lengths in target pixels; the renderer picks the largest that fits a chip.
SIZES = [10, 12, 16, 20, 24, 32, 48]
# In MapOverlayQueue::StatusIcon order. A badge is small, so the sizes start lower.
STATUS_ICONS = ["hurt", "hungry", "damaged", "workers", "unarmed"]
STATUS_SIZES = [8, 10, 12, 16, 20, 24, 32]


def export(icons, sizes, directory, prefix):
    for icon, name in enumerate(icons):
        source = ROOT / "content-src/icons" / directory / f"{name}.svg"
        for index, size in enumerate(sizes):
            frame = icon * len(sizes) + index
            subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), str(source),
                            "-o", str(ROOT / "content/data/gfx" / f"{prefix}{frame}.png")], check=True)
    return len(icons) * len(sizes)


def main():
    frames = export(ICONS, SIZES, "map", "mapicon")
    frames += export(STATUS_ICONS, STATUS_SIZES, "status", "statusicon")
    print(f"wrote {frames} frames")


if __name__ == "__main__":
    main()
