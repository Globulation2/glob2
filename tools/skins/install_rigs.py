#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install staged GSR1 rig and GSB1 blend-shape candidates into the manifest.

Every ``<name>-rig.json`` (GSR1) or ``<name>-shapes.json`` (GSB1) in the given
directories is validated against its asset bytes and the current repository
sources before anything is copied. The asset is then installed byte-identically
for native rendering and the web designer, and its manifest record (under
``rigs`` or ``shapes``) keeps the existing acceptance flag (new candidates
start unaccepted). Usage:

  python3 tools/skins/install_rigs.py artifacts/rig/worker artifacts/shapes/worker
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
DESTINATION = ROOT / "data/skins/colony-v1"
DESIGNER = ROOT / "platform/apps/web/public/skins/models"
UNIT_CLIPS = (
    "worker-walk",
    "worker-swim",
    "worker-harvest",
    "warrior-walk",
    "warrior-swim",
    "warrior-fight",
    "explorer-fly",
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


FORMATS = {"GSR1": (".gsr", "rigs", b"GSR1", 28), "GSB1": (".gsb", "shapes", b"GSB1", 32)}


def validate(record, data, name):
    """Check the record, its bytes and the header against the installed clip."""
    if record.get("format") not in FORMATS or record.get("version") != 1:
        raise ValueError(f"{name}: not a GSR1 or GSB1 version 1 record")
    extension, _, expected_magic, header_size = FORMATS[record["format"]]
    if record["file"] != name + extension or name not in UNIT_CLIPS:
        raise ValueError(f"{name}: unexpected candidate file name")
    if sha256(data) != record["sha256"]:
        raise ValueError(f"{name}: rig bytes do not match their record")
    for source, digest in record["sources"].items():
        path = ROOT / source
        if not path.is_file() or sha256(path.read_bytes()) != digest:
            raise ValueError(f"{name}: stale or missing source {source}")
    if len(data) < header_size or len(data) > 16 * 1024 * 1024:
        raise ValueError(f"{name}: candidate size out of range")
    if data[:4] != expected_magic:
        raise ValueError(f"{name}: bad candidate header")
    if record["format"] == "GSR1":
        vertices, indices, bones, clips, size, length = struct.unpack_from("<6I", data, 4)
        if not (1 <= bones <= 32):
            raise ValueError(f"{name}: rig must use at most 32 bones")
        uv_at, uv_stride, index_at = 28 + 24, 64, 28 + vertices * 64
    else:
        vertices, indices, shapes, normal_shapes, clips, size, length = struct.unpack_from("<7I", data, 4)
        if not (1 <= shapes <= 128 and 1 <= normal_shapes <= 128):
            raise ValueError(f"{name}: shape counts exceed v1 limits")
        uv_at, uv_stride, index_at = 32, 8, 32 + vertices * 8
    if length != len(data) - header_size:
        raise ValueError(f"{name}: bad candidate header")
    if not (3 <= vertices <= 8192 and 3 <= indices <= 49152 and indices % 3 == 0):
        raise ValueError(f"{name}: candidate dimensions exceed v1 limits")
    if not (clips == 1 and 1 <= size <= 128):
        raise ValueError(f"{name}: candidate must hold one clip")
    if len(record["clips"]) != 1:
        raise ValueError(f"{name}: record must describe the single stored clip")
    baked = (DESTINATION / (name + ".gsk")).read_bytes()
    baked_vertices, baked_indices, _, baked_size = struct.unpack_from("<4I", baked, 4)
    if (vertices, indices, size) != (baked_vertices, baked_indices, baked_size):
        raise ValueError(f"{name}: candidate dimensions differ from the baked clip")
    if data[index_at : index_at + indices * 4] != baked[20 + vertices * 8 : 20 + vertices * 8 + indices * 4]:
        raise ValueError(f"{name}: candidate triangles differ from the baked clip")
    for vertex in range(vertices):
        if data[uv_at + vertex * uv_stride : uv_at + vertex * uv_stride + 8] != baked[20 + vertex * 8 : 20 + vertex * 8 + 8]:
            raise ValueError(f"{name}: candidate paint coordinates differ from the baked clip")


def install(staged):
    manifest_path = DESTINATION / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    candidates = []
    for folder in staged:
        for suffix in ("-rig.json", "-shapes.json"):
            for record_path in sorted(folder.glob("*" + suffix)):
                name = record_path.name[: -len(suffix)]
                record = json.loads(record_path.read_text())
                data = (folder / record["file"]).read_bytes()
                validate(record, data, name)
                candidates.append((name, record, data))
    if not candidates:
        raise ValueError("No staged candidate records found")
    for name, record, data in candidates:
        table = manifest.setdefault(FORMATS[record["format"]][1], {})
        previous = table.get(record["file"], {})
        record["accepted"] = bool(previous.get("accepted", False))
        table[record["file"]] = record
        for destination in (DESTINATION, DESIGNER):
            (destination / record["file"]).write_bytes(data)
        print(f"installed {record['file']} ({len(data)} bytes, accepted={record['accepted']})")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged", type=Path, nargs="+")
    install(parser.parse_args().staged)
