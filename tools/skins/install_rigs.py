#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install staged GSR1 rig and GSB1 blend-shape assets into the manifest.

Every ``<name>-rig.json`` (GSR1) or ``<name>-shapes.json`` (GSB1) in the given
directories is validated against its asset bytes, the current repository
sources and the baked clip it animates before anything is copied. The asset is
then installed byte-identically for native rendering and the web designer, and
its manifest record (under ``rigs`` or ``shapes``) keeps the existing
acceptance flag. Usage:

  python3 tools/skins/install_rigs.py artifacts/rig/explorer artifacts/shapes/worker
"""

import argparse
import json
from pathlib import Path
import struct
import sys
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from skin_assets import (
    DESIGNER,
    GSB_HEADER,
    GSK_HEADER,
    GSR_HEADER,
    INSTALLED,
    MAX_BONES,
    MAX_BYTES,
    MAX_INDICES,
    MAX_LOGICAL_SIZE,
    MAX_SHAPES,
    MAX_VERTICES,
    ROOT,
    UNIT_CLIP_NAMES,
    sha256_bytes,
    sha256_file,
    write_json,
)


class Format(NamedTuple):
    extension: str
    table: str  # manifest section
    magic: bytes
    header: int  # header bytes
    record: str  # staged record suffix


FORMATS = {
    "GSR1": Format(".gsr", "rigs", b"GSR1", GSR_HEADER, "-rig.json"),
    "GSB1": Format(".gsb", "shapes", b"GSB1", GSB_HEADER, "-shapes.json"),
}


def validate(record, data, name):
    """Check the record, its bytes and the header against the installed baked clip.

    A rig or blend-shape clip must keep the baked clip's vertices, paint
    coordinates and triangles, so every paint made for one applies to the other.
    """
    if record.get("format") not in FORMATS or record.get("version") != 1:
        raise ValueError(f"{name}: not a GSR1 or GSB1 version 1 record")
    fmt = FORMATS[record["format"]]
    if record["file"] != name + fmt.extension or name not in UNIT_CLIP_NAMES:
        raise ValueError(f"{name}: unexpected candidate file name")
    if sha256_bytes(data) != record["sha256"]:
        raise ValueError(f"{name}: asset bytes do not match their record")
    for source, digest in record["sources"].items():
        path = ROOT / source
        if not path.is_file() or sha256_file(path) != digest:
            raise ValueError(f"{name}: stale or missing source {source}")
    if len(data) < fmt.header or len(data) > MAX_BYTES:
        raise ValueError(f"{name}: candidate size out of range")
    if data[:4] != fmt.magic:
        raise ValueError(f"{name}: bad candidate header")
    if record["format"] == "GSR1":
        vertices, indices, bones, clips, size, length = struct.unpack_from("<6I", data, 4)
        if not (1 <= bones <= MAX_BONES):
            raise ValueError(f"{name}: rig must use between 1 and {MAX_BONES} bones")
        uv_at, uv_stride, index_at = GSR_HEADER + 24, 64, GSR_HEADER + vertices * 64
    else:
        vertices, indices, shapes, normal_shapes, clips, size, length = struct.unpack_from("<7I", data, 4)
        if not (1 <= shapes <= MAX_SHAPES and 1 <= normal_shapes <= MAX_SHAPES):
            raise ValueError(f"{name}: shape counts exceed v1 limits")
        uv_at, uv_stride, index_at = GSB_HEADER, 8, GSB_HEADER + vertices * 8
    if length != len(data) - fmt.header:
        raise ValueError(f"{name}: bad candidate header")
    if not (3 <= vertices <= MAX_VERTICES and 3 <= indices <= MAX_INDICES and indices % 3 == 0):
        raise ValueError(f"{name}: candidate dimensions exceed v1 limits")
    if not (clips == 1 and 1 <= size <= MAX_LOGICAL_SIZE):
        raise ValueError(f"{name}: candidate must hold one clip")
    if len(record["clips"]) != 1:
        raise ValueError(f"{name}: record must describe the single stored clip")
    baked = (INSTALLED / (name + ".gsk")).read_bytes()
    baked_vertices, baked_indices, _, baked_size = struct.unpack_from("<4I", baked, 4)
    if (vertices, indices, size) != (baked_vertices, baked_indices, baked_size):
        raise ValueError(f"{name}: candidate dimensions differ from the baked clip")
    baked_index_at = GSK_HEADER + vertices * 8
    if data[index_at : index_at + indices * 4] != baked[baked_index_at : baked_index_at + indices * 4]:
        raise ValueError(f"{name}: candidate triangles differ from the baked clip")
    for vertex in range(vertices):
        at, baked_at = uv_at + vertex * uv_stride, GSK_HEADER + vertex * 8
        if data[at : at + 8] != baked[baked_at : baked_at + 8]:
            raise ValueError(f"{name}: candidate paint coordinates differ from the baked clip")


def install(staged):
    """Validate every staged candidate, then copy them and update the manifest."""
    manifest_path = INSTALLED / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    candidates = []
    for folder in staged:
        for fmt in FORMATS.values():
            for record_path in sorted(Path(folder).glob("*" + fmt.record)):
                name = record_path.name[: -len(fmt.record)]
                record = json.loads(record_path.read_text())
                data = (Path(folder) / record["file"]).read_bytes()
                validate(record, data, name)
                candidates.append((record, data))
    if not candidates:
        raise ValueError("No staged candidate records found")
    for record, data in candidates:
        table = manifest.setdefault(FORMATS[record["format"]].table, {})
        previous = table.get(record["file"], {})
        record["accepted"] = bool(previous.get("accepted", False))
        table[record["file"]] = record
        for destination in (INSTALLED, DESIGNER):
            (destination / record["file"]).write_bytes(data)
        print(f"installed {record['file']} ({len(data)} bytes, accepted={record['accepted']})")
    write_json(manifest_path, manifest)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("staged", type=Path, nargs="+")
    install(parser.parse_args().staged)
