#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the HD ripe wheat frames by recolouring the recovered stage-4 art.

Wheat at full stock draws `ressource14` and `ressource19`. The archive has no
larger source for that ripe colour state, but the classic 34 px sprites show it
is stage 4 (`ressource13`, `ressource18`) with identical alpha and new dot
colours. This tool takes the stage-4 HD frames recovered from the original GIMP
files and recolours each dot:

1. every dot whose classic colour changes is a connected component of the
   classic stage-4 sprite, and each HD pixel belongs to the nearest one;
2. a dot's HD pixels keep their brightness relative to the classic stage-4 dot
   colour, applied to the classic stage-5 dot colour;
3. the change is weighted by each pixel's chroma relative to the dot, so grey
   shadows and white highlights stay as drawn.

Deterministic, without AI. Rerunning reproduces the committed pixels, and
validate_runtime.py recomputes them.

    python3 tools/artwork/ripen_wheat.py --sheet artifacts/wheat.png  # preview only
    python3 tools/artwork/ripen_wheat.py                               # write frames + atlas
    python3 tools/artwork/package_runtime.py --check
    python3 tools/artwork/validate_runtime.py

Needs NumPy, SciPy and Pillow.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RIPE = {14: 13, 19: 18}  # ripe frame -> recovered stage-4 frame with the same shape
RECIPE = ("recovered original wheat, ripe recolour: stage-4 GIMP layers with the classic "
          "stage-5 dot colours")
STAGE4_RECIPE = "recovered original wheat"


def _luma(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def _chroma(rgb):
    return rgb.max(-1) - rgb.min(-1)


def ripen(stage4_hd, classic4, classic5):
    """Recolour the HD stage-4 frame to the classic stage-5 dot colours."""
    from scipy import ndimage
    hd = np.asarray(stage4_hd.convert("RGBA"), np.float64)
    c4 = np.asarray(classic4.convert("RGBA"), np.float64)
    c5 = np.asarray(classic5.convert("RGBA"), np.float64)
    if not np.array_equal(c4[..., 3], c5[..., 3]):
        raise ValueError("Classic stage-4 and ripe sprites differ in shape")
    changed = (np.abs(c4[..., :3] - c5[..., :3]).sum(-1) > 0) & (c4[..., 3] > 0)
    labels, count = ndimage.label(changed)
    nearest = ndimage.distance_transform_edt(labels == 0, return_distances=False, return_indices=True)
    nearest = labels[nearest[0], nearest[1]]
    height, width = hd.shape[:2]
    rows = np.minimum(np.arange(height) * c4.shape[0] // height, c4.shape[0] - 1)
    cols = np.minimum(np.arange(width) * c4.shape[1] // width, c4.shape[1] - 1)
    dot = nearest[rows[:, None], cols[None, :]]
    out = hd.copy()
    for d in range(1, count + 1):
        old = c4[labels == d][:, :3].mean(0)
        new = c5[labels == d][:, :3].mean(0)
        sel = (dot == d) & (hd[..., 3] > 0)
        pixels = hd[sel][:, :3]
        brightness = (_luma(pixels) / max(_luma(old), 1.0))[:, None]
        weight = np.clip(_chroma(pixels) / max(_chroma(old), 1.0), 0, 1)[:, None]
        out[sel, :3] = np.clip(pixels * (1 - weight) + new * brightness * weight, 0, 255)
    return Image.fromarray(np.rint(out).astype(np.uint8), "RGBA")


def _open(path):
    with Image.open(path) as image:
        return image.convert("RGBA")


def build(root=ROOT):
    """{ripe frame index: (stage-4 HD, ripened HD)} from the committed pack."""
    pack = root / "content/data/highres/v1"
    classic = root / "content/data/gfx"
    return {ripe: (_open(pack / f"ressource{stage4}.png"),
                   ripen(_open(pack / f"ressource{stage4}.png"), _open(classic / f"ressource{stage4}.png"),
                         _open(classic / f"ressource{ripe}.png")))
            for ripe, stage4 in RIPE.items()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sheet", type=Path, help="write a stage-4/ripe preview instead of the pack")
    args = parser.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from highres_pack import rebuild_resource_atlas, register_frames, registered, source_record
    results = build()
    if args.sheet:
        size = next(iter(results.values()))[0].width
        sheet = Image.new("RGBA", (2 * size + 8, len(results) * (size + 8)), (74, 96, 60, 255))
        for row, (before, after) in enumerate(results.values()):
            sheet.alpha_composite(before, (0, row * (size + 8)))
            sheet.alpha_composite(after, (size + 8, row * (size + 8)))
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(args.sheet)
        print(f"Wrote {args.sheet}")
        return
    frames = []
    for ripe, stage4 in RIPE.items():
        source = registered(f"ressource{stage4}")
        if not source or not source["recipe"].startswith(STAGE4_RECIPE):
            raise SystemExit(f"ressource{stage4} is not the recovered stage-4 wheat frame")
        frames.append({"id": f"ressource{ripe}", "image": results[ripe][1], "recipe": RECIPE,
                       "source_sha256": source["layers"][0]["sha256"],
                       "sources": [source["sources"][1],  # native 128 px stage-4 export
                                   source_record(f"content/data/gfx/ressource{stage4}.png"),
                                   source_record(f"content/data/gfx/ressource{ripe}.png"),
                                   source_record(Path(__file__).resolve(), ROOT)]})
    register_frames(frames, "original-derived")
    rebuild_resource_atlas()
    print(f"Ripened {len(frames)} wheat frames and rebuilt the resource atlas")


if __name__ == "__main__":
    main()
