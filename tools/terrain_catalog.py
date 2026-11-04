# SPDX-License-Identifier: GPL-3.0-or-later
"""Read and write the terrain catalog as one document.

The catalog is stored as one small file per terrain: data/terrain/tileset.json
holds the shared tables (profiles, border classes, bindings, warp) and lists
the materials in catalog order by key, and data/terrain/materials/<key>.json
holds each material's block. A material entry in tileset.json may also be an
inline block, as map bundles and test fixtures use.

Standard library only: the build reads the catalog without Pillow.
"""
import json
import re
from pathlib import Path

INDEX = "data/terrain/tileset.json"
MATERIALS = "data/terrain/materials"


def assemble(index, read):
    """The catalog document with every listed key replaced by its block."""
    materials = []
    for entry in index["materials"]:
        if isinstance(entry, str):
            block = read(entry)
            if block.get("key") != entry:
                raise ValueError(f"{MATERIALS}/{entry}.json must have key {entry!r}")
            entry = block
        materials.append(entry)
    return dict(index, materials=materials)


def load(content):
    """The catalog under a runtime tree (a checkout's content/ directory)."""
    content = Path(content)
    index = json.loads((content / INDEX).read_text())
    return assemble(index, lambda key: json.loads((content / MATERIALS / f"{key}.json").read_text()))


def sources(content):
    """The files the catalog is read from, index first."""
    content = Path(content)
    index = json.loads((content / INDEX).read_text())
    return [content / INDEX] + [content / MATERIALS / f"{key}.json"
                                for key in index["materials"] if isinstance(key, str)]


def dump(value):
    """JSON in the catalog's layout: two-space indent, integer lists on one line."""
    text = json.dumps(value, indent=2)
    text = re.sub(
        r"\[\s+((?:-?\d+,\s+)*-?\d+)\s+\]",
        lambda m: "[" + re.sub(r",\s+", ", ", m.group(1)) + "]",
        text,
    )
    return text + "\n"


def save(content, document):
    """Write each material to its own file and the shared tables to the index.

    Material files no longer listed are removed."""
    content = Path(content)
    directory = content / MATERIALS
    directory.mkdir(parents=True, exist_ok=True)
    keys = [material["key"] for material in document["materials"]]
    if len(set(keys)) != len(keys):
        raise ValueError("Duplicate terrain material keys")
    for material in document["materials"]:
        path = directory / f"{material['key']}.json"
        text = dump(material)
        if not path.is_file() or path.read_text() != text:
            path.write_text(text)
    for path in directory.glob("*.json"):
        if path.stem not in keys:
            path.unlink()
    (content / INDEX).write_text(dump(dict(document, materials=keys)))
