#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rasterise the strategic-view map icons and the status icons into the game's sprite frames.

Development-only; ordinary builds use the committed PNGs. Needs rsvg-convert
(librsvg). Run from the repository root:

    python3 tools/icons/export_map_icons.py

Sources are content-src/icons/map/<name>.svg. Output is content/data/gfx/mapicon<N>.png,
frame N = icon * len(SIZES) + size, matching MapOverlayQueue's icon lookup:
keep ICONS and SIZES, and STATUS_ICONS, STATUS_SIZES and STATUS_VARIANTS with their
variant-major frame order, in step with src/render/MapOverlayQueue.cpp.

Status icons, the badges that mark a unit or building needing attention, come
from content-src/icons/status/<name>.svg and go to content/data/gfx/statusicon<N>.png,
frame N = variant * len(STATUS_ICONS) * len(STATUS_SIZES) + icon * len(STATUS_SIZES) + size,
in MapOverlayQueue::StatusIcon order. The sources are drawn in white; the export paints
every icon twice, yellow then red, and the game blends between the two by how bad the
condition is. Badges are drawn bare, with no backing disc.
"""
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
# In IntBuildingType order (swarm .. clearing flag), then the market.
ICONS = ["hive", "inn", "hospital", "racetrack", "pool", "barracks", "school", "tower",
         "flag-explore", "flag-war", "flag-clear", "market"]
# Edge lengths in target pixels; the renderer picks the largest that fits a chip.
SIZES = [10, 12, 16, 20, 24, 32, 48]
# In MapOverlayQueue::StatusIcon order. A badge is small, so the sizes start lower.
STATUS_ICONS = ["hurt", "hungry", "damaged", "workers", "unarmed"]
STATUS_SIZES = [8, 10, 14, 16, 20, 28, 32]
# The two variants replacing the sources' white: yellow when a condition has just
# begun, red at its worst.
STATUS_VARIANTS = ["#f7d21e", "#e22d2d"]


def export(icons, sizes, directory, prefix, color=None, first=0):
    for icon, name in enumerate(icons):
        source = (ROOT / "content-src/icons" / directory / f"{name}.svg").read_text()
        if color:
            # Only the exact white token; a longer colour such as #fff8e0 is left alone.
            source, count = re.subn(r"#fff\b", color, source)
            if not count:
                raise SystemExit(f"{name}.svg has no #fff to paint {color}")
        for index, size in enumerate(sizes):
            frame = first + icon * len(sizes) + index
            subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size),
                            "-o", str(ROOT / "content/data/gfx" / f"{prefix}{frame}.png")],
                           input=source.encode(), check=True)
    return len(icons) * len(sizes)


def main():
    frames = export(ICONS, SIZES, "map", "mapicon")
    for variant, color in enumerate(STATUS_VARIANTS):
        frames += export(STATUS_ICONS, STATUS_SIZES, "status", "statusicon", color,
                         variant * len(STATUS_ICONS) * len(STATUS_SIZES))
    print(f"wrote {frames} frames")


if __name__ == "__main__":
    main()
