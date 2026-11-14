#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Give the AI-upscaled rocks HD frames crisp outlines and sharper shading.

The `resource constrained` finishing took each frame's alpha from a bilinear
4x resize of the 32 px sprite and kept only damped model detail over a bicubic
base, so the rocks read as soft blobs at close zoom. This pass starts from
those approved finals and:

1. remaps the soft alpha ramp (0.3-0.7 of full coverage) through a smoothstep,
   so the outline is a short anti-aliased edge instead of a 4 px halo;
2. applies a wide local-contrast unsharp mask (sigma 4, amount 0.6), then a
   fine unsharp mask (sigma 1, amount 1.2), to the colour.

The inputs are the `resource constrained` finals as committed at SOURCE_REV and
are read from Git, so rerunning the tool reproduces the committed pixels. Each
frame's manifest layer records the input hash as `source_sha256`. The resource
atlas mips are rebuilt from all 65 frames afterwards.

    python3 tools/artwork/sharpen_resources.py --sheet artifacts/rocks.png  # preview only
    python3 tools/artwork/sharpen_resources.py                              # write frames + atlas
    python3 tools/artwork/package_runtime.py --check
    python3 tools/artwork/validate_runtime.py

Needs NumPy, SciPy and Pillow.
"""
import argparse
import hashlib
import io
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from highres_pack import (RUNTIME, register_frames, registered,  # noqa: E402
                          rebuild_resource_atlas, source_record)

FRAMES = [f"ressource{i}" for i in range(30, 40)]  # rocks, both variants
SOURCE_RECIPE = "resource constrained"
RECIPE = "resource constrained; crisp edges and sharpening v1"
SOURCE_REV = "dfe50af992"  # last commit with the unsharpened finals
ALPHA_RAMP = (0.3, 0.7)
UNSHARP = ((4.0, 0.6), (1.0, 1.2))  # (gaussian sigma in HD px, amount), applied in order


def crisp_alpha(alpha):
    """Smoothstep the soft alpha ramp into a short edge; uint8 in, uint8 out."""
    lo, hi = ALPHA_RAMP
    t = np.clip((np.asarray(alpha, np.float64) / 255 - lo) / (hi - lo), 0, 1)
    return np.rint(t * t * (3 - 2 * t) * 255).astype(np.uint8)


def sharpen(image):
    from scipy.ndimage import gaussian_filter
    pixels = np.asarray(image.convert("RGBA"), np.float64)
    rgb = pixels[..., :3]
    for sigma, amount in UNSHARP:
        rgb = np.clip(rgb + amount * (rgb - gaussian_filter(rgb, (sigma, sigma, 0))), 0, 255)
    out = np.dstack([np.rint(rgb).astype(np.uint8), crisp_alpha(pixels[..., 3])])
    return Image.fromarray(out, "RGBA")


def source_image(frame_id):
    path = f"{RUNTIME}/{frame_id}.png"
    try:
        data = subprocess.run(["git", "show", f"{SOURCE_REV}:{path}"], cwd=ROOT, check=True,
                              capture_output=True).stdout
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"Cannot read {path} at {SOURCE_REV} (fetch full history): "
                         f"{error.stderr.decode().strip()}")
    frame = registered(frame_id)
    expected = frame["layers"][0]["source_sha256"] if frame["recipe"] == RECIPE else frame["layers"][0]["sha256"]
    if frame["recipe"] not in (SOURCE_RECIPE, RECIPE) or hashlib.sha256(data).hexdigest() != expected:
        raise SystemExit(f"{frame_id}: input at {SOURCE_REV} is not the recorded constrained final")
    return Image.open(io.BytesIO(data)).convert("RGBA"), expected


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sheet", type=Path, help="write a before/after preview instead of the pack")
    args = parser.parse_args(argv)
    results = []
    for frame_id in FRAMES:
        before, digest = source_image(frame_id)
        results.append((frame_id, before, sharpen(before), digest))
    if args.sheet:
        size = results[0][1].width
        sheet = Image.new("RGBA", (2 * size + 8, len(results) * (size + 8)), (74, 96, 60, 255))
        for row, (_, before, after, _) in enumerate(results):
            sheet.alpha_composite(before, (0, row * (size + 8)))
            sheet.alpha_composite(after, (size + 8, row * (size + 8)))
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(args.sheet)
        print(f"Wrote {args.sheet}")
        return
    tool = source_record(Path(__file__).resolve(), ROOT)
    register_frames([{"id": frame_id, "image": after, "recipe": RECIPE, "source_sha256": digest,
                      "sources": [tool]} for frame_id, _, after, digest in results], "ai-upscaled")
    rebuild_resource_atlas()
    print(f"Sharpened {len(results)} rocks frames and rebuilt the resource atlas")


if __name__ == "__main__":
    main()
