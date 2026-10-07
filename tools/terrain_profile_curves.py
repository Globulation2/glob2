#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate the boundary contour curves of the shipped terrain profiles.

Usage: python3 tools/terrain_profile_curves.py           # print a summary
       python3 tools/terrain_profile_curves.py --write   # update tileset.json

The sand profile traces the original hand-drawn grass/sand transition tiles
(frames 48 to 79: four straight-edge groups of eight variants). Every 32-pixel
edge yields two 17-point curves, one per 16-pixel patch, with the linear trend
between the patch endpoints removed so shared edges keep their zero endpoints.
The fractured and cobblestone profiles use seeded random walks: angular jumps
for ice, broad plateaus for cobblestone chips. The catalogue profiles added for
the new terrain groups are also seeded random walks: `rock` (angular, strong
speckle) for boulders, ridges, scree, gravel and chasms; `soft` (gentle, wide
bridges) for mud, marsh, loam, moss, snow, clay, dirt and deep water; `crisp`
(short, no speckle) for holes and boardwalks; `brush` (leafy) for hedges,
thickets and meadows. For shipped profiles only the `contours_q12` arrays are
replaced; roughness, amplitude, feather, speckle and bridge stay as authored.
A profile listed in NEW_PROFILES that is missing from the catalog is appended
with its authored parameters.
"""

import argparse
import json
import random
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 1024
# Group index in the legacy sixteen-frame-per-row atlas and which side is grass.
STRAIGHT_GROUPS = ((4, "top"), (5, "bottom"), (6, "left"), (7, "right"))


def clamp(curve, limit=LIMIT):
    return [max(-limit, min(limit, int(v))) for v in curve]


def is_grass(pixel):
    r, g, b, _ = pixel
    return g > r * 1.12 and g > b


def trace_sand(root, count):
    """Boundary displacement of each original straight edge, in Q8 pixels."""
    curves, seen = [], set()
    for group, side in STRAIGHT_GROUPS:
        for variant in range(8):
            frame = 16 + group * 8 + variant
            with Image.open(root / f"data/gfx/terrain{frame}.png") as image:
                tile = image.convert("RGBA")
                positions = []
                for i in range(32):
                    horizontal = side in ("top", "bottom")
                    column = [tile.getpixel((i, j) if horizontal else (j, i)) for j in range(32)]
                    grass = sum(1 for p in column if is_grass(p))
                    if side in ("bottom", "right"):
                        grass = 32 - grass
                    positions.append(grass - 16)
            positions.append(positions[0])
            for start in (0, 16):
                segment = positions[start : start + 17]
                a, b = segment[0], segment[16]
                curve = clamp(round((segment[i] - (a + (b - a) * i / 16)) * 256) for i in range(17))
                curve[0] = curve[16] = 0
                if max(abs(v) for v in curve) >= 256 and tuple(curve) not in seen:
                    seen.add(tuple(curve))
                    curves.append(curve)
    rng = random.Random(11)
    rng.shuffle(curves)
    return curves[:count]


def random_walk(rng, points, low, high, kink):
    curve = [0]
    for _ in range(1, points - 1):
        step = rng.choice((-1, 1)) * rng.randint(low, high)
        if rng.random() < kink:
            step *= 2
        curve.append(max(-LIMIT, min(LIMIT, curve[-1] + step)))
    curve.append(0)
    last = len(curve) - 1
    return clamp(
        [round(curve[i] - curve[last - 1] * i / (last - 1)) if i < last else 0 for i in range(len(curve))]
    )


# Catalogue profiles: authored parameters and random-walk shapes
# (seed, curve count, points per curve, step low, step high, kink chance).
NEW_PROFILES = {
    "rock": dict(roughness_q8=384, amplitude_q8=896, feather_q8=192, speckle_q8=448, bridge_q8=192,
                 walk=(12, 16, 9, 200, 520, 0.5)),
    "soft": dict(roughness_q8=160, amplitude_q8=512, feather_q8=448, speckle_q8=128, bridge_q8=640,
                 walk=(13, 12, 17, 40, 160, 0.1)),
    "crisp": dict(roughness_q8=96, amplitude_q8=256, feather_q8=128, speckle_q8=0, bridge_q8=256,
                  walk=(14, 8, 9, 60, 200, 0.2)),
    "brush": dict(roughness_q8=320, amplitude_q8=768, feather_q8=320, speckle_q8=384, bridge_q8=512,
                  walk=(15, 16, 17, 100, 300, 0.4)),
}


def generate(root, sand_count=24):
    rng = random.Random(11)
    fractured = [random_walk(rng, 9, 150, 450, 0.3) for _ in range(12)]
    cobblestone = []
    for _ in range(12):
        curve = [0]
        for _ in range(1, 8):
            curve.append(rng.choice((-640, -400, -200, 0, 200, 400, 640)) + rng.randint(-80, 80))
        curve.append(0)
        cobblestone.append(clamp(curve))
    curves = {"sand": trace_sand(root, sand_count), "fractured": fractured, "cobblestone": cobblestone}
    for key, spec in NEW_PROFILES.items():
        seed, count, points, low, high, kink = spec["walk"]
        walk = random.Random(seed)
        curves[key] = [random_walk(walk, points, low, high, kink) for _ in range(count)]
    return curves


def dump_catalog(document):
    """Serialise a catalog the way the shipped tileset.json is formatted."""
    text = json.dumps(document, indent=2)
    text = re.sub(
        r"\[\s+((?:-?\d+,\s+)*-?\d+)\s+\]",
        lambda m: "[" + re.sub(r",\s+", ", ", m.group(1)) + "]",
        text,
    )
    return text + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "data/terrain/tileset.json")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    document = json.loads(args.manifest.read_text())
    curves = generate(ROOT)
    present = {profile["key"] for profile in document["profiles"]}
    for profile in document["profiles"]:
        if profile["key"] in curves:
            profile["contours_q12"] = curves[profile["key"]]
    for key, spec in NEW_PROFILES.items():
        if key not in present:
            parameters = {k: v for k, v in spec.items() if k != "walk"}
            document["profiles"].append({"key": key, **parameters, "contours_q12": curves[key]})
    for key, value in curves.items():
        peak = max(abs(v) for curve in value for v in curve)
        print(f"{key}: {len(value)} curves, peak displacement {peak / 256:.2f} px")
    if args.write:
        args.manifest.write_text(dump_catalog(document))


if __name__ == "__main__":
    main()
