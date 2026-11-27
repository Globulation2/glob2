#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mirror building frames whose shadow falls the wrong way.

The world light comes from the left, so building shadows fall to the right
(hospital, school, defence tower). A few frames were drawn lit from the other
side; this tool mirrors them horizontally so their shadows agree:

* `barracks0b0` (barracks level 1): the arena's shadow fell to the left.

Every layer of the frame is mirrored together: the classic sprite and its
team layer, the minimap/panel `mini` sprites, and the HD base and team layers.
Footprints are rectangles, so gameplay is unchanged.

The inputs are read from Git at SOURCE_REV (the last commit before mirroring),
so rerunning the tool reproduces the committed pixels instead of flipping
them back. sharpen_upscales.py mirrors the same frames after sharpening, so
rerunning it also reproduces them.

    python3 tools/artwork/mirror_buildings.py --sheet artifacts/mirror.png  # preview only
    python3 tools/artwork/mirror_buildings.py                                # write frames
    python3 tools/artwork/package_runtime.py --check
    python3 tools/artwork/validate_runtime.py

Needs Pillow.
"""
import argparse
import io
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
SOURCE_REV = "273d3f5738"  # last commit with the unmirrored frames
MIRRORED = ("barracks0b0",)
CLASSIC = Path("content/data/gfx")


def _git(path):
    try:
        return subprocess.run(["git", "show", f"{SOURCE_REV}:{path}"], cwd=ROOT, check=True,
                              capture_output=True).stdout
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"Cannot read {path} at {SOURCE_REV} (fetch full history): "
                         f"{error.stderr.decode().strip()}")


def _mirrored(path):
    with Image.open(io.BytesIO(_git(path))) as image:
        return ImageOps.mirror(image.convert("RGBA"))


def classic_files(frame_id):
    """Classic sprite paths for a frame: base, team and mini variants that exist at SOURCE_REV."""
    names = [f"{frame_id}.png", f"{frame_id}r.png", f"mini{frame_id}.png", f"mini{frame_id}r.png"]
    listed = set(subprocess.run(["git", "ls-tree", "--name-only", SOURCE_REV, f"{CLASSIC}/"], cwd=ROOT,
                                check=True, capture_output=True, text=True).stdout.split())
    return [CLASSIC / name for name in names if str(CLASSIC / name) in listed]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sheet", type=Path, help="write a before/after preview instead of the frames")
    args = parser.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from highres_pack import PRODUCTION, RUNTIME, _save_metadata, _read_rows, file_sha

    manifest_path = f"{PRODUCTION}/pack-metadata/manifest.json"
    frames = {frame["id"]: frame for frame in json.loads(_git(manifest_path))["frames"]}
    index = {record["runtime"]: record for record in json.loads(_git(f"{PRODUCTION}/package.json"))["files"]}

    if args.sheet:
        rows = []
        for frame_id in MIRRORED:
            layer = next(l for l in frames[frame_id]["layers"] if l["role"] == "base")
            before = Image.open(io.BytesIO(_git(f"{PRODUCTION}/{index[layer['file']]['source']}"))).convert("RGBA")
            rows.append((before, ImageOps.mirror(before)))
        size = max(b.width for b, _ in rows)
        sheet = Image.new("RGBA", (2 * size + 8, sum(b.height + 8 for b, _ in rows)), (74, 96, 60, 255))
        y = 0
        for before, after in rows:
            sheet.alpha_composite(before, (0, y))
            sheet.alpha_composite(after, (size + 8, y))
            y += before.height + 8
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(args.sheet)
        print(f"Wrote {args.sheet}")
        return

    for frame_id in MIRRORED:
        for path in classic_files(frame_id):
            _mirrored(path).save(ROOT / path, optimize=False)

    manifest = json.loads((ROOT / manifest_path).read_text())
    live = {frame["id"]: frame for frame in manifest["frames"]}
    package = json.loads((ROOT / PRODUCTION / "package.json").read_text())
    records = {record["runtime"]: record for record in package["files"]}
    tool = {"path": str(Path(__file__).resolve().relative_to(ROOT)), "sha256": file_sha(Path(__file__).resolve())}
    for frame_id in MIRRORED:
        frame = live[frame_id]
        for layer in frame["layers"]:
            source = PRODUCTION / records[layer["file"]]["source"]
            image = _mirrored(f"{PRODUCTION}/{index[layer['file']]['source']}")
            image.save(ROOT / source, optimize=False)
            (ROOT / RUNTIME / layer["file"]).write_bytes((ROOT / source).read_bytes())
            native = ROOT / CLASSIC / (layer["file"])
            layer["sha256"] = records[layer["file"]]["sha256"] = file_sha(ROOT / source)
            layer["original_sha256"] = file_sha(native)
        frame["sources"] = [s for s in frame.get("sources", []) if s["path"] != tool["path"]] + [tool]
    _save_metadata(manifest, _read_rows(ROOT / PRODUCTION / "pack-metadata/frames.txt"), package, ROOT)
    print(f"Mirrored {len(MIRRORED)} building frame(s): {', '.join(MIRRORED)}")


if __name__ == "__main__":
    main()
