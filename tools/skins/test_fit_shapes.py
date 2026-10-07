#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks for the fitted worker and warrior blend-shape clips.

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
import shape_fixture
from skin_assets import FRAMES, GSB_HEADER, GSK_HEADER, INSTALLED, ROOT, baked_clip, read_gsk

STAGING = ROOT / "artifacts/shape-tests"
# Distance to the baked frames in model units (the models are about 16 units
# across) and the angle between rebuilt and baked normals.
THRESHOLDS = {"rms": 0.15, "p95": 0.3, "max": 2.5, "normalMedianDegrees": 2.0, "normalP99Degrees": 25.0}


class FittedShapesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        shutil.rmtree(STAGING, ignore_errors=True)
        cls.results = {model: fit_unit_shapes.fit(model) for model in fit_unit_shapes.MODELS}
        for model, result in cls.results.items():
            fit_unit_shapes.author(model, STAGING / model, fit_result=result)

    def clips(self):
        for model, clips in fit_unit_shapes.MODELS.items():
            for index, clip in enumerate(clips):
                yield model, index, clip, (STAGING / model / f"{model}-{clip}.gsb").read_bytes()

    def test_assets_keep_paint_topology_and_describe_their_shapes(self):
        for model, index, clip, data in self.clips():
            magic, vertices, indices, shapes, normal_shapes, clips, size, length = struct.unpack_from("<4s7I", data)
            self.assertEqual((magic, clips, length), (b"GSB1", 1, len(data) - GSB_HEADER))
            baked = (INSTALLED / f"{model}-{clip}.gsk").read_bytes()
            count, index_count, _, baked_size = struct.unpack_from("<4I", baked, 4)
            self.assertEqual((vertices, indices, size), (count, index_count, baked_size))
            uv_bytes, index_bytes = count * 8, index_count * 4
            self.assertEqual(data[GSB_HEADER : GSB_HEADER + uv_bytes], baked[GSK_HEADER : GSK_HEADER + uv_bytes])
            self.assertEqual(
                data[GSB_HEADER + uv_bytes : GSB_HEADER + uv_bytes + index_bytes],
                baked[GSK_HEADER + uv_bytes : GSK_HEADER + uv_bytes + index_bytes],
            )
            self.assertEqual((shapes, normal_shapes), (fit_unit_shapes.DEFAULT_SHAPES, fit_unit_shapes.DEFAULT_NORMAL_SHAPES))
            record = json.loads((STAGING / model / f"{model}-{clip}-shapes.json").read_text())
            self.assertEqual(record["clips"][0]["id"], index)
            self.assertNotIn("accepted", record)

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
            _, _, poses, _ = read_gsk(baked_clip(model, clip).path)
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

    def test_encoder_reproduces_the_shared_decoder_fixture(self):
        # The analytic fixture is built by hand; the production encoder must
        # serialize the same inputs to the same bytes, and the reference
        # evaluator must agree with the fixture's plain-Python evaluation.
        fixture = shape_fixture.fixture()
        vertices = [(0, 0), (1, 0), (0, 1), (1, 1)]
        indices = [0, 1, 2, 2, 1, 3]
        mean = [(1, 0, 0), (0, 1, 0), (-1, 0, 0), (0, -1, 0.5)]
        normal_mean = [(1, 0, 0), (0, 1, 0), (-1, 0, 0), (0, 0, 1)]
        shapes = [
            (0.001, [(1000, 0, 0), (0, 1000, 0), (-1000, 0, 0), (0, -1000, 0)]),
            (0.0005, [(0, 0, 2000), (0, 0, 2000), (0, 0, -2000), (0, 0, 0)]),
        ]
        normal_shapes = [(0.002, [(0, 0, 500), (0, 0, 500), (0, 0, 500), (500, 0, 0)])]
        clips = shape_fixture.fixture_clips()
        data = fit_unit_shapes.encode(
            vertices,
            indices,
            mean,
            [s for s, _ in shapes],
            [d for _, d in shapes],
            normal_mean,
            [s for s, _ in normal_shapes],
            [d for _, d in normal_shapes],
            clips,
            16,
        )
        self.assertEqual(data.hex(), fixture["hex"])
        basis = np.array([d for _, d in shapes], dtype=np.float64) * np.array([s for s, _ in shapes])[:, None, None]
        normal_basis = np.array([d for _, d in normal_shapes], dtype=np.float64) * np.array([s for s, _ in normal_shapes])[:, None, None]
        for key, expected in fixture["expected"].items():
            clip, frame = (int(v) for v in key.split(":"))
            out = fit_unit_shapes.evaluate(
                np.array(mean, dtype=np.float64),
                basis,
                np.array(clips[clip]["coefficients"][frame]),
                np.array(normal_mean, dtype=np.float64),
                normal_basis,
                np.array(clips[clip]["normalCoefficients"][frame]),
                clips[clip]["headings"][frame],
                clips[clip]["modelToClip"],
                clips[clip]["normalToCamera"],
            )
            self.assertLess(np.abs(out - np.array(expected)).max(), 1e-6, key)

    def test_encoder_rejects_invalid_inputs(self):
        result = self.results["worker"]
        valid = dict(
            uv=result["uv"],
            indices=result["indices"].ravel(),
            mean=result["mean"],
            scales=result["scales"],
            deltas=result["deltas"],
            normal_mean=result["normalMean"],
            normal_scales=result["normalScales"],
            normal_deltas=result["normalDeltas"],
            clips=[result["clips"][0]],
            logical_size=result["size"],
        )
        fit_unit_shapes.encode(**valid)
        bad_clip = dict(result["clips"][0], coefficients=np.full((FRAMES, len(result["scales"])), 20000.0))
        for name, overrides in (
            ("zero scale", dict(scales=np.zeros_like(result["scales"]))),
            ("bad index", dict(indices=np.full_like(result["indices"].ravel(), len(result["mean"])))),
            ("duplicate clip", dict(clips=[result["clips"][0], result["clips"][0]])),
            ("out of range", dict(clips=[bad_clip])),
            ("bad size", dict(logical_size=0)),
        ):
            with self.subTest(name), self.assertRaises(ValueError):
                fit_unit_shapes.encode(**dict(valid, **overrides))

    def test_regeneration_is_byte_identical_and_matches_the_installed_assets(self):
        for model in fit_unit_shapes.MODELS:
            fit_unit_shapes.author(model, STAGING / f"{model}-again")
            for clip in fit_unit_shapes.MODELS[model]:
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
