#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Register generated high-resolution frames in the approved artwork pack.

The pack has two committed copies: the approved inputs under
`datasrc/gfx/production/` (one folder per origin, indexed by `package.json`)
and the assembled source pack `data/highres/v1/` that the game reads. Tools
that generate world art at 4x (terrain synthesis, material and trail exports)
call `register_frames` to write each frame into both, upsert its manifest
entry and `frames.txt` row, and refresh the production index hashes, so
`package_runtime.py --check` stays green without a separate capture step.

Pure Python 3 plus Pillow; no numpy.
"""
import hashlib
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION = Path("datasrc/gfx/production")
RUNTIME = Path("data/highres/v1")
HEADER = "GLOB2_HIGHRES 1"
SCALE = 4


def file_sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_record(path, root=ROOT):
    path = Path(path)
    return {"path": str(path.relative_to(root)) if path.is_absolute() else str(path),
            "sha256": file_sha(path if path.is_absolute() else root / path)}


def _read_rows(path):
    lines = path.read_text().splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError(f"{path}: invalid frame index header")
    return [line.split() for line in lines[1:] if line.strip()]


def _write_rows(path, rows):
    path.write_text(HEADER + "\n" + "".join(" ".join(row) + "\n" for row in rows))


def _encode(image, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGBA").save(path, optimize=False)


def register_frames(frames, category, root=ROOT):
    """Write and register frames: [{id, image, recipe, sources}] at 4x.

    `id` is the native sprite frame name (for example `terrain-gravel3`); its
    classic `data/gfx/<id>.png` must already exist at the logical size. Existing
    entries with the same id are replaced in place; new ones are appended.
    """
    production = root / PRODUCTION
    runtime = root / RUNTIME
    manifest_path = production / "pack-metadata/manifest.json"
    manifest = json.loads(manifest_path.read_text())
    by_id = {frame["id"]: i for i, frame in enumerate(manifest["frames"])}
    rows = _read_rows(production / "pack-metadata/frames.txt")
    row_index = {row[0]: i for i, row in enumerate(rows)}
    index_path = production / "package.json"
    index = json.loads(index_path.read_text())
    records = {record["runtime"]: record for record in index["files"]}
    for frame in frames:
        frame_id = frame["id"]
        image = frame["image"]
        native = root / "data/gfx" / f"{frame_id}.png"
        with Image.open(native) as classic:
            width, height = classic.size
        if image.size != (width * SCALE, height * SCALE):
            raise ValueError(f"{frame_id}: HD frame {image.size} is not {SCALE}x the classic {width}x{height}")
        name = f"{frame_id}.png"
        relative = f"{category}/{name}"
        _encode(image, production / relative)
        digest = file_sha(production / relative)
        (runtime / name).write_bytes((production / relative).read_bytes())
        entry = {
            "id": frame_id,
            "width": width,
            "height": height,
            "scale": SCALE,
            "recipe": frame["recipe"],
            "layers": [{
                "file": name,
                "role": "base",
                "sha256": digest,
                "source_sha256": digest,
                "logical_width": width,
                "logical_height": height,
                "original_sha256": file_sha(native),
            }],
            "sources": frame.get("sources", []),
        }
        if frame_id in by_id:
            manifest["frames"][by_id[frame_id]] = entry
        else:
            by_id[frame_id] = len(manifest["frames"])
            manifest["frames"].append(entry)
        row = [frame_id, str(width), str(height), str(SCALE), name, "-"]
        if frame_id in row_index:
            rows[row_index[frame_id]] = row
        else:
            row_index[frame_id] = len(rows)
            rows.append(row)
        record = records.get(name)
        if record is None:
            record = {"runtime": name, "source": relative, "sha256": digest}
            index["files"].append(record)
            records[name] = record
        elif record["source"] != relative:
            old = production / record["source"]
            record["source"] = relative
            if old.exists() and old != production / relative:
                old.unlink()
        record["sha256"] = digest
    _save_metadata(manifest, rows, index, root)


def unregister_frames(frame_ids, root=ROOT):
    """Remove frames (all layers) from both pack copies and their indexes."""
    production = root / PRODUCTION
    runtime = root / RUNTIME
    manifest_path = production / "pack-metadata/manifest.json"
    manifest = json.loads(manifest_path.read_text())
    doomed = set(frame_ids)
    files = {layer["file"] for frame in manifest["frames"] if frame["id"] in doomed for layer in frame["layers"]}
    manifest["frames"] = [frame for frame in manifest["frames"] if frame["id"] not in doomed]
    rows = [row for row in _read_rows(production / "pack-metadata/frames.txt") if row[0] not in doomed]
    index_path = production / "package.json"
    index = json.loads(index_path.read_text())
    for record in index["files"]:
        if record["runtime"] in files:
            (production / record["source"]).unlink(missing_ok=True)
            (runtime / record["runtime"]).unlink(missing_ok=True)
    index["files"] = [record for record in index["files"] if record["runtime"] not in files]
    _save_metadata(manifest, rows, index, root)


def _save_metadata(manifest, rows, index, root):
    production = root / PRODUCTION
    runtime = root / RUNTIME
    (production / "pack-metadata/manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    _write_rows(production / "pack-metadata/frames.txt", rows)
    for name in ("manifest.json", "frames.txt"):
        (runtime / name).write_bytes((production / "pack-metadata" / name).read_bytes())
    for record in index["files"]:
        if record["runtime"] in ("manifest.json", "frames.txt"):
            record["sha256"] = file_sha(production / record["source"])
    (production / "package.json").write_text(json.dumps(index, indent=2) + "\n")


def registered(frame_id, root=ROOT):
    """The manifest entry for `frame_id`, or None."""
    manifest = json.loads((root / PRODUCTION / "pack-metadata/manifest.json").read_text())
    return next((frame for frame in manifest["frames"] if frame["id"] == frame_id), None)
