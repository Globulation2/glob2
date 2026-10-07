#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Layout, limits and provenance helpers shared by the unit skin tools.

The unit mesh formats are GSK1 (every posed vertex of every frame), GSR1 (a
bone rig) and GSB1 (a blend-shape clip). Their binary layouts and the limits
the native and Studio decoders enforce are collected here so the authoring
scripts, installers and tests agree with libgag/src/SkinModel.cpp and
libgag/src/SkinShapeModel.cpp. Only the clip readers need NumPy.
"""

import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from typing import NamedTuple

ROOT = Path(__file__).resolve().parents[2]
INSTALLED = ROOT / "data/skins/colony-v1"
DESIGNER = ROOT / "platform/apps/web/public/skins/models"

# Every unit clip holds eight headings of 32 animation phases.
PHASES = 32
DIRECTIONS = 8
FRAMES = PHASES * DIRECTIONS
# The clips each animated model owns, in clip-id order.
UNIT_CLIPS = {"worker": ("walk", "swim", "harvest"), "warrior": ("walk", "swim", "fight"), "explorer": ("fly",)}
UNIT_CLIP_NAMES = tuple(f"{model}-{clip}" for model, clips in UNIT_CLIPS.items() for clip in clips)

# Limits every decoder enforces on serialized assets.
MAX_BYTES = 16 * 1024 * 1024
MAX_VERTICES = 8192
MAX_INDICES = 49152
MAX_CLIPS = 8
MAX_LOGICAL_SIZE = 128
MAX_BONES = 32
MAX_SHAPES = 128
MAX_SCALAR = 10000.0


def f32(value):
    """Round to the binary32 the asset stores; reject values a decoder refuses."""
    if not math.isfinite(value) or abs(value) > MAX_SCALAR:
        raise ValueError("Invalid skin asset scalar")
    return struct.unpack("<f", struct.pack("<f", value))[0]


MAX_HEADING = f32(6.283186)

# Header sizes in bytes. GSK1: magic, vertices, indices, frames, logical size.
# GSR1: magic, vertices, indices, bones, clips, logical size, payload.
# GSB1: magic, vertices, indices, shapes, normal shapes, clips, logical size, payload.
GSK_HEADER = 20
GSR_HEADER = 28
GSB_HEADER = 32


def heading_of_frame(frame):
    """Model heading of one of the 256 gameplay poses, in radians about Z."""
    return -(frame // PHASES) * math.pi / 4


def rotation_z(angle):
    """Rotation about Z with binary32 cosine and sine, as the source scenes use."""
    import numpy as np

    c, s = float(np.float32(math.cos(angle))), float(np.float32(math.sin(angle)))
    return np.array([[c, -s, 0.0], [s, c, 0.0], [0.0, 0.0, 1.0]])


class BakedClip(NamedTuple):
    """An installed GSK1 clip with its poses taken back into model space."""

    path: Path
    uv: "np.ndarray"  # (vertices, 2) paint coordinates
    indices: "np.ndarray"  # (triangles, 3)
    positions: "np.ndarray"  # (256, vertices, 3) model space, heading undone
    normals: "np.ndarray"  # (256, vertices, 3) model space, heading undone
    size: int  # logical sprite size
    view: dict  # the clip's .view.json camera


def read_gsk(path):
    """Raw GSK1 contents: paint UVs, triangles, logical size and clip-space poses."""
    import numpy as np

    data = Path(path).read_bytes()
    magic, count, index_count, frames, size = struct.unpack_from("<4s4I", data)
    if magic != b"GSK1":
        raise ValueError(f"{path}: not a GSK1 clip")
    uv = np.frombuffer(data, "<f4", count * 2, GSK_HEADER).reshape(-1, 2)
    indices = np.frombuffer(data, "<u4", index_count, GSK_HEADER + count * 8).reshape(-1, 3)
    poses = np.frombuffer(
        data, "<f4", count * 6 * frames, GSK_HEADER + count * 8 + index_count * 4
    ).reshape(frames, count, 6)
    return uv, indices, poses, size


def baked_clip(model, clip):
    """Installed clip in model space, every frame's heading rotation undone.

    The result is in the same body-relative frame as the source components, so
    rigs and blend shapes fitted to it reproduce every heading from one model.
    """
    import numpy as np

    path = INSTALLED / f"{model}-{clip}.gsk"
    uv, indices, poses, size = read_gsk(path)
    if len(poses) != FRAMES:
        raise ValueError(f"{path.name} is not a {FRAMES}-pose unit clip")
    view = json.loads(path.with_suffix(".view.json").read_text())
    clip_to_model = np.array(view["clipToModel"]).reshape(4, 4)
    normal_to_model = np.array(view["normalToModel"]).reshape(3, 3)
    count = poses.shape[1]
    homogeneous = np.concatenate((poses[:, :, :3].astype(np.float64), np.ones((FRAMES, count, 1))), axis=2)
    positions = np.einsum("ij,fvj->fvi", clip_to_model, homogeneous)[:, :, :3]
    normals = poses[:, :, 3:].astype(np.float64) @ normal_to_model.T
    for frame in range(FRAMES):
        undo = rotation_z(-heading_of_frame(frame)).T
        positions[frame] = positions[frame] @ undo
        normals[frame] = normals[frame] @ undo
    return BakedClip(path, uv, indices, positions, normals, size, view)


def normal_to_camera(view):
    """The clip camera's normal matrix, the transpose of the exported inverse."""
    n = view["normalToModel"]
    return [n[(k % 3) * 3 + k // 3] for k in range(9)]


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    return sha256_bytes(Path(path).read_bytes())


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def source_digests(paths):
    """Repository-relative path to SHA-256 for every authoring input."""
    return {str(Path(p).resolve().relative_to(ROOT)): sha256_file(p) for p in paths}


def write_candidate(output, name, data, sources, clips, format_name):
    """Stage an asset with the record install_rigs.py validates.

    ``format_name`` is ``GSR1`` or ``GSB1``; the record lists every source the
    bytes were built from, so the installer and the asset contract test can
    tell a stale candidate from a current one.
    """
    extension, suffix = {"GSR1": (".gsr", "-rig.json"), "GSB1": (".gsb", "-shapes.json")}[format_name]
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / (name + extension)).write_bytes(data)
    record = {
        "format": format_name,
        "version": 1,
        "file": name + extension,
        "sha256": sha256_bytes(data),
        "clips": clips,
        "sources": source_digests(sources),
    }
    write_json(output / (name + suffix), record)
    return record


def script_arguments(argv=None):
    """Arguments after Blender's ``--`` separator, or all of them when run plainly."""
    argv = sys.argv if argv is None else argv
    return argv[argv.index("--") + 1 :] if "--" in argv else argv[1:]
