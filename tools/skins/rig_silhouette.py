#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Per-frame silhouette agreement between two `skin-preview --rig-review` captures.

Both prefixes name the six 2048x2048 BMP sheets a review capture writes
(`PREFIX-0.bmp` ... `PREFIX-5.bmp`, 16 frames of 128 pixels per row). The
intersection over union of the alpha masks is reported for all 256 frames of
one style sheet, for example a fitted rig against the baked clip it was fitted
to, or against a previous candidate. Plain Python; no NumPy or Pillow needed.

  python3 tools/skins/rig_silhouette.py artifacts/review/baked artifacts/review/rig --json out.json
"""

import argparse
import json
from pathlib import Path
import struct

TILE, PER_ROW, SHEET = 128, 16, 2048


def alpha_tiles(path):
    data = Path(path).read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if (width, abs(height), bits) != (SHEET, SHEET, 32):
        raise ValueError(f"{path}: expected a 2048x2048 32-bit review sheet")
    alpha = data[offset + 3 : offset + SHEET * SHEET * 4 : 4]
    opaque = alpha.translate(bytes([0] + [1] * 255))
    rows = [opaque[y * SHEET : (y + 1) * SHEET] for y in range(SHEET)]
    if height > 0:
        rows.reverse()  # bottom-up BMP
    tiles = []
    for frame in range(256):
        x = frame % PER_ROW * TILE
        y = frame // PER_ROW * TILE
        tiles.append(b"".join(rows[y + line][x : x + TILE] for line in range(TILE)))
    return tiles


def agreement(reference, candidate):
    scores = []
    for a, b in zip(reference, candidate):
        both = sum(x & y for x, y in zip(a, b))
        either = sum(x | y for x, y in zip(a, b))
        scores.append(both / either if either else 1.0)
    return scores


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--style", type=int, default=1, help="sheet index 0-5")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    scores = agreement(
        alpha_tiles(f"{args.reference}-{args.style}.bmp"),
        alpha_tiles(f"{args.candidate}-{args.style}.bmp"),
    )
    ordered = sorted(scores)
    summary = {
        "mean": sum(scores) / len(scores),
        "minimum": ordered[0],
        "p5": ordered[len(ordered) // 20],
        "worstFrame": scores.index(ordered[0]),
        "perFrame": scores,
    }
    print(
        f"silhouette IoU mean {summary['mean']:.4f} p5 {summary['p5']:.4f} "
        f"min {summary['minimum']:.4f} (frame {summary['worstFrame']})"
    )
    if args.json:
        args.json.write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
