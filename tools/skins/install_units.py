#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install a validated staged unit export and identical designer models."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from test_export import validate_model

ROOT = Path(__file__).resolve().parents[2]


def install(staged):
    destination = ROOT / "data/skins/colony-v1"
    designer = ROOT / "platform/apps/web/public/skins/models"
    manifest = json.loads((destination / "manifest.json").read_text())
    # Validate the complete set and provenance before replacing any assets.
    exports = {}
    sets = {
        "worker": {"walk", "swim", "harvest"},
        "warrior": {"walk", "swim", "fight"},
        "explorer": {"fly"},
    }
    for model, clips in sets.items():
        path = staged / (model + "-manifest.json")
        exported = json.loads(path.read_text())
        if set(exported["clips"]) != clips:
            raise ValueError(f"Incomplete {model} clips")
        for clip, record in exported["clips"].items():
            if record["file"] != f"{model}-{clip}.gsk":
                raise ValueError("Unexpected staged mesh name")
            source = ROOT / record["source"]
            assert source.resolve().is_relative_to(
                (ROOT / "datasrc/gfx/originals/units").resolve()
            )
            assert (
                hashlib.sha256(source.read_bytes()).hexdigest()
                == record["sourceSha256"]
            ), "Staged export has stale animation sources"
        validate_model(staged, path)
        if model != "explorer":
            for path_key, hash_key in (
                ("tools/skins/export_units.py", "exporterSha256"),
                ("tools/skins/limb_surface.py", "surfaceBuilderSha256"),
                ("tools/skins/chart.py", "surfaceChartSha256"),
                (exported["surfaceDefinition"], "surfaceDefinitionSha256"),
            ):
                assert (
                    hashlib.sha256((ROOT / path_key).read_bytes()).hexdigest()
                    == exported[hash_key]
                ), "Staged export has stale surface sources"
        for dependency, digest in exported.get("surfaceDependencies", {}).items():
            assert hashlib.sha256((ROOT / dependency).read_bytes()).hexdigest() == digest, "Staged export has stale surface dependency"
        exports[model] = exported
    for model, exported in exports.items():
        for clip in exported["clips"].values():
            name = clip["file"]
            record = {
                field: clip[field]
                for field in (
                    "sha256",
                    "source",
                    "sourceSha256",
                    "vertices",
                    "triangles",
                )
            }
            if model != "explorer":
                record["surface"] = {
                    "definition": exported["surfaceDefinition"],
                    "definitionSha256": exported["surfaceDefinitionSha256"],
                    "exporter": "tools/skins/export_units.py",
                    "exporterSha256": exported["exporterSha256"],
                    "builder": "tools/skins/limb_surface.py",
                    "builderSha256": exported["surfaceBuilderSha256"],
                    "chart": "tools/skins/chart.py",
                    "chartSha256": exported["surfaceChartSha256"],
                    "contract": exported["surfaceContract"],
                    "contractSha256": exported["surfaceContractSha256"],
                    "dependencies": exported["surfaceDependencies"],
                }
            if "detailUV" in clip:
                record["detailUV"] = clip["detailUV"]
                for folder in (destination, designer):
                    shutil.copyfile(staged / clip["detailUV"]["file"], folder / clip["detailUV"]["file"])
            manifest["meshes"][name] = record
            shutil.copyfile(staged / name, destination / name)
            shutil.copyfile(staged / name, designer / name)
        if model != "explorer":
            shutil.copyfile(
                staged / exported["surfaceContract"],
                destination / exported["surfaceContract"],
            )
    # Swarm geometry and texture coordinates are intentionally preserved.
    shutil.copyfile(destination / "swarm.gsk", designer / "swarm.gsk")
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged", type=Path)
    install(parser.parse_args().staged)
