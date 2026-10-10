# SPDX-License-Identifier: GPL-3.0-or-later
"""Join the native generator catalog to maintained design descriptions."""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess

from .common import digest, glob2_source


def descriptions(repo):
    """Use the maintained design-guide descriptions, not generator names alone."""
    rows = {}
    references = repo / ".agents/skills/glob2-map-design/references"
    for name in ("landscape-generators.md", "shaped-generators.md"):
        path = references / name
        for line in path.read_text(encoding="utf-8").splitlines():
            source = re.search(r"src/map/generator/generators/([^()]+\.cpp)", line)
            if not line.startswith("| [") or not source:
                continue
            columns = line.split("|")
            rows[source[1]] = (columns[2].strip(), str(path.relative_to(repo)))
    return rows


def build_catalog(binary, repo=None):
    """Describe only generators exposed by this executable's native catalog.

    Source/binary hashes identify inputs; they cannot prove a binary was built
    from those sources. Callers must use a matching checkout and build.
    """
    repo = Path(repo).resolve() if repo is not None else glob2_source()
    result = subprocess.run(
        [str(binary), "info", "catalog", "--format", "json"],
        cwd=repo,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=True,
        timeout=60,
    )
    native = json.loads(result.stdout)
    if native.get("schema_version") != 1:
        raise ValueError("Unsupported native catalog schema")
    guide = descriptions(repo)
    sources = {}
    for path in (repo / "src/map/generator/generators").glob("*.cpp"):
        text = path.read_text(encoding="utf-8")
        # Definitions have comments interspersed between registration fields.
        clean = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        match = re.search(
            r'(?:return|GeneratorDefinition\s+\w+)\s*\{\s*"([a-z0-9-]+)"\s*,\s*\d+\s*,\s*"',
            clean,
        )
        if match:
            sources[match[1]] = (path, text)
    entries = []
    for definition in native["generators"]:
        if definition["editorOnly"]:
            continue
        identifier = definition["id"]
        if identifier not in sources:
            raise ValueError(f"Missing source description for {identifier}")
        path, text = sources[identifier]
        description, origin = guide.get(path.name, (None, None))
        if not description:
            # Newer generators have their design brief in the leading comment.
            chunks = re.findall(r"(?m)(?:^//[^\n]*\n)+", text)
            chunks = [chunk for chunk in chunks if not chunk.startswith("// SPDX")]
            if not chunks:
                raise ValueError(f"Missing design brief for {identifier}")
            description = " ".join(
                line.removeprefix("//").strip() for line in chunks[0].splitlines()
            )[:2400]
            origin = str(path.relative_to(repo))
        entries.append(
            {
                "id": identifier,
                "name": definition["nameKey"],
                "revision": definition["revision"],
                "controls": definition["controls"],
                "description": description,
                "description_source": origin,
                "source_sha256": digest(path),
                "tags": sorted(
                    set(
                        re.findall(
                            r'"((?:terrain|feature|style|fairness):[^"\n]+)"', text
                        )
                    )
                ),
            }
        )
    return {
        "schema_version": 1,
        "binary_sha256": digest(binary),
        "generators": sorted(entries, key=lambda item: item["id"]),
    }
