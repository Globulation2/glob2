#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks for the fitted worker and warrior blend-shape models.

Needs NumPy; run under Blender's Python when the system one lacks it:
  blender --background --python tools/skins/test_fit_shapes.py
"""

import json
from pathlib import Path
import shutil
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
import fit_unit_shapes
import install_rigs
from rig_scene import baked_frames

ROOT = Path(__file__).resolve().parents[2]
STAGING = ROOT / "artifacts/shape-tests"
INSTALLED = ROOT / "data/skins/colony-v1"
# Distance to the baked frames in model units (the models are about 16 units
# across) and the angle between rebuilt and baked normals.
THRESHOLDS = {"rms": 0.15, "p95": 0.3, "max": 2.5, "normalMedianDegrees": 2.0, "normalP99Degrees": 25.0}


class FittedShapesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        shutil.rmtree(STAGING, ignore_errors=True)
        cls.results = {}
        for model in fit_unit_shapes.CLIPS:
            cls.results[model] = fit_unit_shapes.fit(model)
            fit_unit_shapes.author(model, STAGING / model)

    def clips(self):
        for model, clips in fit_unit_shapes.CLIPS.items():
            for index, clip in enumerate(clips):
                yield model, index, clip, (STAGING / model / f"{model}-{clip}.gsb").read_bytes()

    def test_assets_keep_paint_topology_and_describe_their_shapes(self):
        for model, index, clip, data in self.clips():
            magic, vertices, indices, shapes, normal_shapes, clips, size, length = struct.unpack_from("<4s7I", data)
            self.assertEqual((magic, clips, length), (b"GSB1", 1, len(data) - 32))
            baked = (INSTALLED / f"{model}-{clip}.gsk").read_bytes()
            count, index_count, _, baked_size = struct.unpack_from("<4I", baked, 4)
            self.assertEqual((vertices, indices, size), (count, index_count, baked_size))
            self.assertEqual(data[32 : 32 + count * 8], baked[20 : 20 + count * 8])
            self.assertEqual(
                data[32 + count * 8 : 32 + count * 8 + index_count * 4],
                baked[20 + count * 8 : 20 + count * 8 + index_count * 4],
            )
            self.assertEqual((shapes, normal_shapes), (fit_unit_shapes.DEFAULT_SHAPES, fit_unit_shapes.DEFAULT_NORMAL_SHAPES))
            record = json.loads((STAGING / model / f"{model}-{clip}-shapes.json").read_text())
            self.assertEqual(record["clips"][0]["id"], index)

    def test_every_clip_reproduces_the_baked_frames(self):
        for model, index, clip, data in self.clips():
            report = json.loads((STAGING / model / f"{model}-{clip}-fit.json").read_text())["frames"]
            for key, limit in THRESHOLDS.items():
                self.assertLess(report[key], limit, f"{model}-{clip} {key}")

    def test_reference_evaluation_matches_the_baked_poses(self):
        # The reference evaluator (the contract the native and Studio decoders
        # follow) reproduces the baked camera-space poses up to the fit error.
        for model, index, clip, data in self.clips():
            result = self.results[model]
            path, _, _, _, _, _ = baked_frames(model, clip)
            baked = path.read_bytes()
            count, index_count = struct.unpack_from("<2I", baked, 4)
            poses = np.frombuffer(baked, "<f4", count * 6 * 256, 20 + count * 8 + index_count * 4).reshape(256, count, 6)
            record = result["clips"][index]
            for frame in (0, 40, 130, 255):
                out = fit_unit_shapes.evaluate(
                    result["mean"],
                    result["basis"],
                    record["coefficients"][frame].astype(np.float64),
                    result["normalMean"],
                    result["normalBasis"],
                    record["normalCoefficients"][frame].astype(np.float64),
                    record["headings"][frame],
                    record["modelToClip"],
                    record["normalToCamera"],
                )
                self.assertLess(np.abs(out[:, :3] - poses[frame, :, :3]).max(), 0.25, f"{model}-{clip} {frame}")
                angles = np.degrees(np.arccos(np.clip((out[:, 3:] * poses[frame, :, 3:]).sum(axis=1), -1, 1)))
                self.assertLess(np.median(angles), 3.0, f"{model}-{clip} {frame}")

    def test_regeneration_is_byte_identical_and_matches_the_installed_assets(self):
        for model in fit_unit_shapes.CLIPS:
            fit_unit_shapes.author(model, STAGING / f"{model}-again")
            for clip in fit_unit_shapes.CLIPS[model]:
                name = f"{model}-{clip}.gsb"
                staged = (STAGING / model / name).read_bytes()
                self.assertEqual(staged, (STAGING / f"{model}-again" / name).read_bytes(), name)
                self.assertEqual(staged, (INSTALLED / name).read_bytes(), name)

    def test_installer_rejects_stale_sources_and_altered_bytes(self):
        folder = STAGING / "tampered"
        shutil.copytree(STAGING / "warrior", folder)
        record_path = folder / "warrior-walk-shapes.json"
        record = json.loads(record_path.read_text())
        record["sources"]["tools/skins/fit_unit_shapes.py"] = "0" * 64
        record_path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, "stale"):
            install_rigs.install([folder])
        shutil.rmtree(folder)
        shutil.copytree(STAGING / "warrior", folder)
        asset = folder / "warrior-walk.gsb"
        asset.write_bytes(asset.read_bytes()[:-1] + b"\0")
        with self.assertRaisesRegex(ValueError, "do not match"):
            install_rigs.install([folder])


if __name__ == "__main__":
    if (
        not unittest.TextTestRunner(verbosity=2)
        .run(unittest.defaultTestLoader.loadTestsFromTestCase(FittedShapesTest))
        .wasSuccessful()
    ):
        raise RuntimeError("Shape authoring contract failed")
