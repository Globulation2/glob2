#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install staged GSR1 rig candidates and record them in the colony manifest.

Every ``<name>-rig.json`` in the given directories is validated against its
``.gsr`` bytes and the current repository sources before anything is copied.
The rig is then installed byte-identically for native rendering and the web
designer, and its manifest record keeps the existing acceptance flag (new
candidates start unaccepted). Usage:

  python3 tools/skins/install_rigs.py artifacts/rig/worker artifacts/rig/warrior
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


def validate(record, data, name):
    """Check the record, its bytes and the header against the installed clip."""
    if record.get("format") != "GSR1" or record.get("version") != 1:
        raise ValueError(f"{name}: not a GSR1 version 1 record")
    if record["file"] != name + ".gsr" or name not in UNIT_CLIPS:
        raise ValueError(f"{name}: unexpected rig file name")
    if sha256(data) != record["sha256"]:
        raise ValueError(f"{name}: rig bytes do not match their record")
    for source, digest in record["sources"].items():
        path = ROOT / source
        if not path.is_file() or sha256(path.read_bytes()) != digest:
            raise ValueError(f"{name}: stale or missing source {source}")
    if len(data) < 28 or len(data) > 16 * 1024 * 1024:
        raise ValueError(f"{name}: rig size out of range")
    magic, vertices, indices, bones, clips, size, length = struct.unpack_from("<4s6I", data)
    if magic != b"GSR1" or length != len(data) - 28:
        raise ValueError(f"{name}: bad rig header")
    if not (3 <= vertices <= 8192 and 3 <= indices <= 49152 and indices % 3 == 0):
        raise ValueError(f"{name}: rig dimensions exceed v1 limits")
    if not (1 <= bones <= 32 and clips == 1 and 1 <= size <= 128):
        raise ValueError(f"{name}: rig must hold one clip with at most 32 bones")
    if len(record["clips"]) != 1:
        raise ValueError(f"{name}: record must describe the single stored clip")
    baked = (DESTINATION / (name + ".gsk")).read_bytes()
    baked_vertices, baked_indices, _, baked_size = struct.unpack_from("<4I", baked, 4)
    if (vertices, indices, size) != (baked_vertices, baked_indices, baked_size):
        raise ValueError(f"{name}: rig dimensions differ from the baked clip")
    if data[28 + vertices * 64 : 28 + vertices * 64 + indices * 4] != baked[20 + vertices * 8 : 20 + vertices * 8 + indices * 4]:
        raise ValueError(f"{name}: rig triangles differ from the baked clip")
    for vertex in range(vertices):
        if data[28 + vertex * 64 + 24 : 28 + vertex * 64 + 32] != baked[20 + vertex * 8 : 20 + vertex * 8 + 8]:
            raise ValueError(f"{name}: rig paint coordinates differ from the baked clip")


def install(staged):
    manifest_path = DESTINATION / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    rigs = manifest.setdefault("rigs", {})
    candidates = []
    for folder in staged:
        for record_path in sorted(folder.glob("*-rig.json")):
            name = record_path.name[: -len("-rig.json")]
            record = json.loads(record_path.read_text())
            data = (folder / record["file"]).read_bytes()
            validate(record, data, name)
            candidates.append((name, record, data))
    if not candidates:
        raise ValueError("No staged rig records found")
    for name, record, data in candidates:
        previous = rigs.get(record["file"], {})
        record["accepted"] = bool(previous.get("accepted", False))
        rigs[record["file"]] = record
        for destination in (DESTINATION, DESIGNER):
            (destination / record["file"]).write_bytes(data)
        print(f"installed {record['file']} ({len(data)} bytes, accepted={record['accepted']})")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged", type=Path, nargs="+")
    install(parser.parse_args().staged)
