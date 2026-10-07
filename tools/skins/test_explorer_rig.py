#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks for the authored explorer rig.

Run in Blender 3.6.23:
  blender --background --factory-startup -t 1 --python-exit-code 1 \\
    --python tools/skins/test_explorer_rig.py
"""

import json
from pathlib import Path
import shutil
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
import author_explorer_rig
import install_rigs
from skin_assets import FRAMES, GSK_HEADER, GSR_HEADER, INSTALLED, ROOT, read_gsk

STAGING = ROOT / "artifacts/rig-tests"
NAME = "explorer-fly"


def read_rig(data):
    """Decode the parts of a GSR1 asset the checks need."""
    magic, vertices, indices, bones, clips, size, length = struct.unpack_from("<4s6I", data)
    if magic != b"GSR1" or clips != 1 or length != len(data) - GSR_HEADER:
        raise ValueError("not a single-clip GSR1 asset")
    at = GSR_HEADER
    rest = np.frombuffer(data, "<f4", vertices * 16, at).reshape(vertices, 16)
    joints = np.frombuffer(data, "<u4", vertices * 16, at).reshape(vertices, 16)[:, 8:12]
    at += vertices * 64
    triangles = np.frombuffer(data, "<u4", indices, at)
    at += indices * 4
    skeleton = []
    for _ in range(bones):
        parent = struct.unpack_from("<I", data, at)[0]
        skeleton.append((parent, np.frombuffer(data, "<f4", 16, at + 4)))
        at += 68
    clip_id, samples = struct.unpack_from("<2I", data, at)
    duration = struct.unpack_from("<f", data, at + 8)[0]
    at += 12 + 29 * 4
    frames = np.frombuffer(data, "<f4", FRAMES * 2, at).reshape(FRAMES, 2)
    at += FRAMES * 8
    tracks = np.frombuffer(data, "<f4", samples * bones * 8, at).reshape(samples, bones, 8)
    return {
        "vertices": vertices,
        "indices": indices,
        "size": size,
        "positions": rest[:, :3],
        "normals": rest[:, 3:6],
        "uv": rest[:, 6:8],
        "joints": joints,
        "weights": rest[:, 12:16],
        "triangles": triangles,
        "bones": skeleton,
        "samples": samples,
        "duration": duration,
        "frames": frames,
        "tracks": tracks,
    }


class ExplorerRigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        shutil.rmtree(STAGING, ignore_errors=True)
        author_explorer_rig.author(STAGING / "explorer")
        cls.rig = read_rig((STAGING / f"explorer/{NAME}.gsr").read_bytes())

    def test_rest_surface_keeps_paint_topology(self):
        rig = self.rig
        baked = (INSTALLED / f"{NAME}.gsk").read_bytes()
        count, index_count = struct.unpack_from("<2I", baked, 4)
        self.assertEqual((rig["vertices"], rig["indices"]), (count, index_count))
        self.assertEqual(rig["uv"].tobytes(), baked[GSK_HEADER : GSK_HEADER + count * 8])
        self.assertEqual(
            rig["triangles"].tobytes(),
            baked[GSK_HEADER + count * 8 : GSK_HEADER + count * 8 + index_count * 4],
        )
        self.assertTrue(np.allclose(np.linalg.norm(rig["normals"], axis=1), 1, atol=1e-4))
        # The rest mesh is the faired first baked pose, so it stays close to it.
        _, _, poses, _ = read_gsk(INSTALLED / f"{NAME}.gsk")
        view = json.loads((INSTALLED / f"{NAME}.view.json").read_text())
        clip_to_model = np.array(view["clipToModel"]).reshape(4, 4)
        first = np.concatenate((poses[0, :, :3], np.ones((count, 1))), axis=1) @ clip_to_model.T
        self.assertLess(np.abs(first[:, :3] - rig["positions"]).max(), 0.5)

    def test_weights_are_valid_and_bounded_to_four_bones(self):
        rig = self.rig
        self.assertEqual(len(rig["bones"]), 4)
        self.assertTrue(np.all(rig["joints"] < 4))
        self.assertTrue(np.all(rig["weights"] >= 0))
        self.assertTrue(np.allclose(rig["weights"].sum(axis=1), 1, atol=1e-4))
        # Wings are mirrored across the body: their weights are too.
        mirrored = rig["positions"].copy()
        mirrored[:, 1] *= -1
        for v in np.random.default_rng(0).choice(rig["vertices"], 64, replace=False):
            twin = np.argmin(np.linalg.norm(rig["positions"] - mirrored[v], axis=1))
            dense = np.zeros((2, 4))
            for row, vertex in enumerate((v, twin)):
                np.add.at(dense[row], rig["joints"][vertex], rig["weights"][vertex])
            self.assertLess(abs(dense[0, 2] - dense[1, 3]) + abs(dense[0, 3] - dense[1, 2]), 0.1)

    def test_clip_covers_the_gait_with_bounded_tracks(self):
        rig = self.rig
        self.assertEqual(rig["samples"], 64)
        self.assertAlmostEqual(rig["duration"], 2.0, places=5)
        self.assertTrue(np.all(np.isfinite(rig["tracks"])))
        scales = rig["tracks"][:, :, 7]
        self.assertTrue(np.all(scales > 0.2) and np.all(scales < 5))
        for frame in range(FRAMES):
            direction, phase = divmod(frame, 32)
            self.assertAlmostEqual(rig["frames"][frame, 0], -direction * np.pi / 4, places=5)
            self.assertAlmostEqual(rig["frames"][frame, 1], ((direction % 2) * 32 + phase) / 32, places=5)

    def test_regeneration_is_byte_identical_and_matches_the_installed_asset(self):
        author_explorer_rig.author(STAGING / "explorer-again")
        staged = (STAGING / f"explorer/{NAME}.gsr").read_bytes()
        self.assertEqual(staged, (STAGING / f"explorer-again/{NAME}.gsr").read_bytes())
        self.assertEqual(staged, (INSTALLED / f"{NAME}.gsr").read_bytes())

    def test_edited_action_exports_against_the_same_base(self):
        source = STAGING / "edited-explorer.blend"
        shutil.copyfile(STAGING / "explorer/explorer.blend", source)
        author_explorer_rig.author(STAGING / "explorer-action", source, "Fly")
        original = (STAGING / f"explorer/{NAME}.gsr").read_bytes()
        imported = (STAGING / f"explorer-action/{NAME}.gsr").read_bytes()
        tracks_at = len(original) - self.rig["tracks"].nbytes
        self.assertEqual(original[:tracks_at], imported[:tracks_at])
        self.assertLess(np.abs(self.rig["tracks"] - read_rig(imported)["tracks"]).max(), 2e-4)
        with self.assertRaisesRegex(ValueError, "separate"):
            author_explorer_rig.author(STAGING / "explorer", STAGING / "explorer/explorer.blend", "Fly")

    def test_installer_rejects_stale_sources_and_altered_bytes(self):
        folder = STAGING / "tampered"
        shutil.copytree(STAGING / "explorer", folder)
        record_path = folder / f"{NAME}-rig.json"
        record = json.loads(record_path.read_text())
        record["sources"]["tools/skins/author_explorer_rig.py"] = "0" * 64
        record_path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, "stale"):
            install_rigs.install([folder])
        shutil.rmtree(folder)
        shutil.copytree(STAGING / "explorer", folder)
        rig = folder / f"{NAME}.gsr"
        rig.write_bytes(rig.read_bytes()[:-1] + b"\0")
        with self.assertRaisesRegex(ValueError, "do not match"):
            install_rigs.install([folder])


if __name__ == "__main__":
    if (
        not unittest.TextTestRunner(verbosity=2)
        .run(unittest.defaultTestLoader.loadTestsFromTestCase(ExplorerRigTest))
        .wasSuccessful()
    ):
        raise RuntimeError("Explorer rig authoring contract failed")
