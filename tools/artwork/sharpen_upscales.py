#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sharpen the AI-upscaled HD frames that read as soft at close zoom.

The AI-upscale finishing (`resource constrained`, `world constrained`,
`outline_repair`, ...) kept only damped model detail over a smooth base, and
the resource frames took their outline from a bilinear 4x resize of the 32 px
alpha, so they read as soft blobs. This pass starts from those approved finals
and sharpens every AI-upscaled base layer:

* colour: a wide local-contrast unsharp mask (sigma 4, amount 0.6), then a
  fine unsharp mask (sigma 1, amount 1.2);
* resource frames (`ressource<N>`) also remap the soft alpha ramp (0.3-0.7 of
  full coverage) through a smoothstep, so the outline is a short anti-aliased
  edge instead of a 4 px halo. Buildings, walls and effects keep their alpha,
  so soft shadows, smoke and glow stay as drawn.

Team-colour layers are masks that the game recolours; they stay unchanged.
The ripe wheat frames are rebuilt by ripen_wheat.py instead.

The inputs are the AI-upscaled finals as committed at SOURCE_REV and are read
from Git, so rerunning the tool reproduces the committed pixels. Each frame's
base layer records its input hash as `source_sha256`. The resource atlas mips
are rebuilt from all 65 resource frames afterwards.

    python3 tools/artwork/sharpen_upscales.py --sheet artifacts/sharpen.png  # preview only
    python3 tools/artwork/sharpen_upscales.py                                # write frames + atlas
    python3 tools/artwork/package_runtime.py --check
    python3 tools/artwork/validate_runtime.py

Needs NumPy, SciPy and Pillow.
"""
import argparse
import hashlib
import io
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from highres_pack import (RUNTIME, register_frames, registered,  # noqa: E402
                          rebuild_resource_atlas, source_record)

SOURCE_REV = "dfe50af992"  # last commit with the unsharpened AI finals
SOURCE_RECIPES = ("current", "outline_repair", "painted_repair", "crystal_repair",
                  "resource constrained", "world constrained")
SKIP = {"ressource14", "ressource19"}  # ripe wheat: ripen_wheat.py
RECIPE = "resource constrained; crisp edges and sharpening v1"
COLOUR_SUFFIX = "; colour sharpening v1"
RECIPES = {RECIPE} | {recipe + COLOUR_SUFFIX for recipe in SOURCE_RECIPES if recipe != "resource constrained"}
ALPHA_RAMP = (0.3, 0.7)
UNSHARP = ((4.0, 0.6), (1.0, 1.2))  # (gaussian sigma in HD px, amount), applied in order


def crisp_alpha(alpha):
    """Smoothstep the soft alpha ramp into a short edge; uint8 in, uint8 out."""
    lo, hi = ALPHA_RAMP
    t = np.clip((np.asarray(alpha, np.float64) / 255 - lo) / (hi - lo), 0, 1)
    return np.rint(t * t * (3 - 2 * t) * 255).astype(np.uint8)


def sharpen(image, crisp=True):
    """Unsharp the colour; with `crisp`, also tighten the alpha edge."""
    from scipy.ndimage import gaussian_filter
    pixels = np.asarray(image.convert("RGBA"), np.float64)
    rgb = pixels[..., :3]
    for sigma, amount in UNSHARP:
        rgb = np.clip(rgb + amount * (rgb - gaussian_filter(rgb, (sigma, sigma, 0))), 0, 255)
    alpha = crisp_alpha(pixels[..., 3]) if crisp else pixels[..., 3].astype(np.uint8)
    return Image.fromarray(np.dstack([np.rint(rgb).astype(np.uint8), alpha]), "RGBA")


def target_recipe(source_recipe):
    return RECIPE if source_recipe == "resource constrained" else source_recipe + COLOUR_SUFFIX


def _git(path):
    try:
        return subprocess.run(["git", "show", f"{SOURCE_REV}:{path}"], cwd=ROOT, check=True,
                              capture_output=True).stdout
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"Cannot read {path} at {SOURCE_REV} (fetch full history): "
                         f"{error.stderr.decode().strip()}")


def sources():
    """[(frame id, source recipe, source base-layer bytes)] for every AI frame at SOURCE_REV."""
    manifest = json.loads(_git(f"{RUNTIME}/manifest.json"))
    out = []
    for frame in manifest["frames"]:
        if frame["recipe"] not in SOURCE_RECIPES or frame["id"] in SKIP:
            continue
        base = next(layer for layer in frame["layers"] if layer["role"] == "base")
        data = _git(f"{RUNTIME}/{base['file']}")
        if hashlib.sha256(data).hexdigest() != base["sha256"]:
            raise SystemExit(f"{frame['id']}: input at {SOURCE_REV} differs from its manifest hash")
        out.append((frame["id"], frame["recipe"], data))
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sheet", type=Path, help="write a before/after preview instead of the pack")
    args = parser.parse_args(argv)
    results = []
    for frame_id, recipe, data in sources():
        current = registered(frame_id)
        if current["recipe"] not in (recipe, target_recipe(recipe)):
            raise SystemExit(f"{frame_id}: recipe changed since {SOURCE_REV}: {current['recipe']}")
        before = Image.open(io.BytesIO(data)).convert("RGBA")
        after = sharpen(before, crisp=frame_id.startswith("ressource"))
        results.append((frame_id, recipe, before, after, hashlib.sha256(data).hexdigest()))
    if args.sheet:
        size = 128
        sheet = Image.new("RGBA", (2 * size + 8, len(results) * (size + 8)), (74, 96, 60, 255))
        for row, (_, _, before, after, _) in enumerate(results):
            for column, image in enumerate((before, after)):
                image = image.copy()
                image.thumbnail((size, size))
                sheet.alpha_composite(image, (column * (size + 8), row * (size + 8)))
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(args.sheet)
        print(f"Wrote {args.sheet}")
        return
    tool = source_record(Path(__file__).resolve(), ROOT)
    register_frames([{"id": frame_id, "image": after, "recipe": target_recipe(recipe), "source_sha256": digest,
                      "sources": [tool]} for frame_id, recipe, _, after, digest in results], "ai-upscaled")
    rebuild_resource_atlas()
    print(f"Sharpened {len(results)} AI-upscaled frames and rebuilt the resource atlas")


if __name__ == "__main__":
    main()
