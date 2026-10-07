#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate generated unit sets without Blender or third-party packages."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from surface_contract import validate_surface


def validate_detail_uv(root, record, vertices):
    blob = (root / record["file"]).read_bytes()
    assert hashlib.sha256(blob).hexdigest() == record["sha256"], "stale detail UV sidecar"
    assert struct.unpack_from("<4sI", blob) == (b"GUV1", vertices)
    assert len(blob) == 8 + vertices * 8
    uv = struct.unpack_from("<" + str(vertices * 2) + "f", blob, 8)
    assert all(math.isfinite(v) and 0 <= v <= 1 for v in uv)


def validate_model(root, path):
    manifest = json.loads(path.read_text())
    reference = None
    contract = None
    if "surfaceContract" in manifest:
        contract_bytes = (root / manifest["surfaceContract"]).read_bytes()
        assert (
            hashlib.sha256(contract_bytes).hexdigest()
            == manifest["surfaceContractSha256"]
        )
        contract = json.loads(contract_bytes)
    for clip in manifest["clips"]:
        record = manifest["clips"][clip]
        data = (root / record["file"]).read_bytes()
        assert hashlib.sha256(data).hexdigest() == record["sha256"]
        magic, vertices, indices, frames, size = struct.unpack_from("<4sIIII", data)
        assert magic == b"GSK1" and frames == 256 and size == manifest["logicalSize"]
        assert 3 <= vertices <= 8192 and indices % 3 == 0
        if "detailUV" in record:
            validate_detail_uv(root, record["detailUV"], vertices)
        end_uv = 20 + vertices * 8
        end_indices = end_uv + indices * 4
        assert len(data) == end_indices + frames * vertices * 24
        if contract:
            validate_surface(data, contract)
        topology = data[20:end_indices]
        if reference is None:
            reference = topology
        assert topology == reference, "UV or vertex identity changed between actions"
        uv = struct.unpack_from("<" + str(vertices * 2) + "f", data, 20)
        assert all(math.isfinite(v) and 0 <= v <= 1 for v in uv)
        index = struct.unpack_from("<" + str(indices) + "I", data, end_uv)
        assert max(index) < vertices
        stride = vertices * 24
        for direction in range(8):
            poses = set()
            for phase in range(32):
                start = end_indices + (direction * 32 + phase) * stride
                pose = data[start : start + stride]
                values = struct.unpack("<" + str(vertices * 6) + "f", pose)
                assert all(math.isfinite(v) for v in values)
                assert all(-1 < values[i] < 1 for i in range(2, len(values), 6))
                poses.add(pose)
            assert len(poses) > 1, "animation froze"
        print(
            manifest["uvLayout"]
            + "/"
            + clip
            + ": finite animated poses and shared UV/index topology verified"
        )


def swarm_catalog(repository):
    """(id, file) pairs from the native catalog, in index order."""
    text = (repository / "src/online/SwarmMeshCatalog.h").read_text()
    return re.findall(r'\{"([a-z]+)", "(swarm[a-z-]*\.gsk)"\}', text)


def validate_installed(root, repository):
    """Check the shipped colony-v1 contract, including retained source provenance."""
    manifest = json.loads((root / "manifest.json").read_text())
    assert manifest["format"] == "GSK1" and manifest["layout"] == "colony-v1"
    groups = {
        "worker": ("walk", "swim", "harvest"),
        "warrior": ("walk", "swim", "fight"),
        "explorer": ("fly",),
    }
    catalog = swarm_catalog(repository)
    assert catalog and catalog[0] == ("classic", "swarm.gsk"), (
        "classic swarm must stay first in the catalog"
    )
    assert all(file == f"swarm-{mesh}.gsk" for mesh, file in catalog[1:]), (
        "generated swarm file names follow their id"
    )
    # The platform contract and the generator name the same meshes in the same order.
    protocol = (repository / "platform/packages/protocol/src/skins.ts").read_text()
    union = protocol[protocol.index("export const SwarmMesh = Type.Union([") :]
    assert re.findall(
        r"Type\.Literal\('([a-z]+)'\)", union[: union.index("]);")]
    ) == [mesh for mesh, _ in catalog], (
        "platform SwarmMesh differs from src/online/SwarmMeshCatalog.h"
    )
    generator = (repository / "tools/skins/generate_swarms.py").read_text()
    designs = generator[generator.index("DESIGNS = {") :]
    assert re.findall(r"'([a-z]+)': \(", designs[: designs.index("}")]) == [
        mesh for mesh, _ in catalog[1:]
    ], "generate_swarms.py DESIGNS differ from the catalog"
    swarms = {file for _, file in catalog}
    expected = {
        f"{model}-{clip}.gsk" for model, clips in groups.items() for clip in clips
    } | swarms
    assert set(manifest["meshes"]) == expected, "missing or unexpected colony mesh"
    assert {p.name for p in root.glob("*.gsk")} == expected, "unlisted installed mesh"
    topology = {}
    for name in sorted(expected):
        record = manifest["meshes"][name]
        source = (repository / record["source"]).resolve()
        if name in swarms and name != "swarm.gsk":
            # Generated swarms record the generator, its helpers and their design.
            assert record["source"] == "tools/skins/generate_swarms.py"
            assert name == f"swarm-{record['design']}.gsk"
            for dependency, digest in record["dependencies"].items():
                assert (
                    hashlib.sha256((repository / dependency).read_bytes()).hexdigest()
                    == digest
                ), (name + ": " + dependency + " changed")
        else:
            assert source.is_relative_to((repository / "datasrc/gfx").resolve())
        assert (
            hashlib.sha256(source.read_bytes()).hexdigest() == record["sourceSha256"]
        ), (name + ": source changed")
        if "surface" in record:
            surface = record["surface"]
            for field, hash_field in (
                ("definition", "definitionSha256"),
                ("exporter", "exporterSha256"),
                ("builder", "builderSha256"),
                ("chart", "chartSha256"),
            ):
                assert (
                    hashlib.sha256(
                        (repository / surface[field]).read_bytes()
                    ).hexdigest()
                    == surface[hash_field]
                ), (name + ": surface source changed")
            for dependency, digest in surface.get("dependencies", {}).items():
                assert hashlib.sha256((repository / dependency).read_bytes()).hexdigest() == digest, name + ": surface dependency changed"
            contract_bytes = (root / surface["contract"]).read_bytes()
            assert (
                hashlib.sha256(contract_bytes).hexdigest() == surface["contractSha256"]
            )
        data = (root / name).read_bytes()
        if "surface" in record:
            validate_surface(data, json.loads(contract_bytes))
        assert hashlib.sha256(data).hexdigest() == record["sha256"], (
            name + ": installed mesh changed"
        )
        magic, vertices, indices, frames, size = struct.unpack_from("<4sIIII", data)
        if "detailUV" in record:
            validate_detail_uv(root, record["detailUV"], vertices)
        assert (
            magic == b"GSK1"
            and vertices == record["vertices"]
            and indices == record["triangles"] * 3
        )
        assert 3 <= vertices <= 8192 and 3 <= indices <= 49152 and indices % 3 == 0
        assert frames == (1 if name in swarms else 256) and 0 < size <= 128
        end_uv = 20 + vertices * 8
        end_indices = end_uv + indices * 4
        assert len(data) == end_indices + frames * vertices * 24
        uv = struct.unpack_from("<" + str(vertices * 2) + "f", data, 20)
        assert all(math.isfinite(v) and 0 <= v <= 1 for v in uv)
        index = struct.unpack_from("<" + str(indices) + "I", data, end_uv)
        assert max(index) < vertices
        model = name if name in swarms else name.split("-")[0]
        contract = (size, data[20:end_indices])
        if model in topology:
            assert topology[model] == contract, (
                name + ": paint vertex identity changed between actions"
            )
        topology[model] = contract
        poses = []
        for frame in range(frames):
            start = end_indices + frame * vertices * 24
            pose = data[start : start + vertices * 24]
            poses.append(hashlib.sha256(pose).digest())
            for x, y, z, nx, ny, nz in struct.iter_unpack("<6f", pose):
                assert all(math.isfinite(v) for v in (x, y, z, nx, ny, nz))
                assert abs(x) <= 1.25 and abs(y) <= 1.25 and abs(z) < 1, (
                    name + ": clipped geometry"
                )
                assert 0.99 <= nx * nx + ny * ny + nz * nz <= 1.01, (
                    name + ": invalid lighting normal"
                )
        if frames == 256:
            for direction in range(8):
                assert len(set(poses[direction * 32 : (direction + 1) * 32])) > 1, (
                    name + ": frozen gait"
                )
        print(
            name + ": installed bytes, source, UV identity, bounds and normals verified"
        )
    previews = repository / "platform/apps/web/public/skins/models"
    for name in sorted(expected):
        assert (
            hashlib.sha256((previews / name).read_bytes()).hexdigest()
            == manifest["meshes"][name]["sha256"]
        ), (name + ": designer/runtime mesh mismatch")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    root = parser.parse_args().directory
    if (root / "manifest.json").exists() and json.loads(
        (root / "manifest.json").read_text()
    ).get("layout") == "colony-v1":
        validate_installed(root, Path(__file__).resolve().parents[2])
        raise SystemExit(0)
    manifests = sorted(root.glob("*-manifest.json"))
    assert manifests, "No unit manifests found"
    for path in manifests:
        validate_model(root, path)
