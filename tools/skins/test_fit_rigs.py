#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks for the fitted worker/warrior rigs and the explorer rig.

Run in Blender 3.6.23:
  blender --background --factory-startup -t 1 --python-exit-code 1 \
    --python tools/skins/test_fit_rigs.py
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
import fit_unit_rigs
import install_rigs

ROOT = Path(__file__).resolve().parents[2]
STAGING = ROOT / "artifacts/rig-tests"
INSTALLED = ROOT / "data/skins/colony-v1"
# Distance to the baked frames in model units (the models are about 16 units
# across) and vertices whose rig-rotated normal strays from the posed surface.
# The warrior's swim stroke merges all four limbs into one ball, which linear
# skinning cannot follow; its looser bounds record that known residual.
THRESHOLDS = {"rms": 0.7, "p95": 1.6, "max": 7.0, "normalsStrayedMax": 650}
CLIP_THRESHOLDS = {"warrior-swim": {"rms": 1.2, "p95": 2.7, "max": 9.0, "normalsStrayedMax": 650}}


def read_rig(data):
    """Decode the parts of a GSR1 asset the checks need."""
    magic, vertices, indices, bones, clips, size, length = struct.unpack_from("<4s6I", data)
    assert magic == b"GSR1" and clips == 1 and length == len(data) - 28
    at = 28
    rest = np.frombuffer(data, "<f4", vertices * 16, at).reshape(vertices, 16)
    positions, normals, uv = rest[:, :3], rest[:, 3:6], rest[:, 6:8]
    joints = np.frombuffer(data, "<u4", vertices * 16, at).reshape(vertices, 16)[:, 8:12]
    weights = rest[:, 12:16]
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
    frames = np.frombuffer(data, "<f4", 512, at).reshape(256, 2)
    at += 2048
    tracks = np.frombuffer(data, "<f4", samples * bones * 8, at).reshape(samples, bones, 8)
    return {
        "vertices": vertices,
        "indices": indices,
        "size": size,
        "positions": positions,
        "normals": normals,
        "uv": uv,
        "joints": joints,
        "weights": weights,
        "triangles": triangles,
        "bones": skeleton,
        "samples": samples,
        "duration": duration,
        "frames": frames,
        "tracks": tracks,
        "trackOffset": at,
    }


class FittedRigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        shutil.rmtree(STAGING, ignore_errors=True)
        cls.results = {}
        for model in ("worker", "warrior"):
            cls.results[model] = fit_unit_rigs.fit(model)
            fit_unit_rigs.author(model, STAGING / model, fit_unit_rigs.DEFAULT_ITERATIONS, result=cls.results[model])
        author_explorer_rig.author(STAGING / "explorer")

    def clips(self):
        for model, clips in fit_unit_rigs.CLIPS.items():
            for clip in clips:
                yield model, clip, read_rig((STAGING / model / f"{model}-{clip}.gsr").read_bytes())

    def test_rest_surface_keeps_paint_topology_and_both_reflections(self):
        for model, clip, rig in self.clips():
            baked = (INSTALLED / f"{model}-{clip}.gsk").read_bytes()
            count, index_count = struct.unpack_from("<2I", baked, 4)
            self.assertEqual((rig["vertices"], rig["indices"]), (count, index_count))
            self.assertEqual(rig["uv"].tobytes(), baked[20 : 20 + count * 8])
            self.assertEqual(rig["triangles"].tobytes(), baked[20 + count * 8 : 20 + count * 8 + index_count * 4])
            self.assertTrue(np.allclose(np.linalg.norm(rig["normals"], axis=1), 1, atol=1e-4))
            result = self.results[model]
            reflections = result["surface"].contract()["reflections"]
            centre = result["bind"][0][:3, 3]
            basis = result["bind"][0][:3, :3]
            local = (rig["positions"].astype(np.float64) - centre) @ basis
            for axis, name in ((0, "frontBack"), (2, "topBottom")):
                mirrored = local[reflections[name]].copy()
                mirrored[:, axis] *= -1
                self.assertLess(np.abs(mirrored - local).max(), 1e-4, f"{model} {name}")

    def test_weights_are_valid_mirrored_and_bounded_to_four_bones(self):
        for model, clip, rig in self.clips():
            bones = len(rig["bones"])
            self.assertTrue(np.all(rig["joints"] < bones))
            self.assertTrue(np.all(rig["weights"] >= 0))
            self.assertTrue(np.allclose(rig["weights"].sum(axis=1), 1, atol=1e-4))
            dense = np.zeros((rig["vertices"], bones))
            for v in range(rig["vertices"]):
                np.add.at(dense[v], rig["joints"][v], rig["weights"][v])
            self.assertLessEqual((dense > 1e-6).sum(axis=1).max(), 4)
            result = self.results[model]
            reflections = result["surface"].contract()["reflections"]
            mirrors = fit_unit_rigs.mirror_maps(
                result["layout"],
                json.loads(fit_unit_rigs.DEFINITION_PATH.read_text())[model]["paths"],
                result["bind"],
                result["bind"][0][:3, :3],
                result["bind"][0][:3, 3],
            )
            for name in ("frontBack", "topBottom"):
                mirrored = dense[reflections[name]][:, mirrors[name]]
                self.assertLess(np.abs(mirrored - dense).max(), 1e-4, f"{model} {name}")

    def test_every_clip_fits_the_baked_frames(self):
        for model, clip, rig in self.clips():
            report = json.loads((STAGING / model / f"{model}-{clip}-fit.json").read_text())
            for key, limit in CLIP_THRESHOLDS.get(f"{model}-{clip}", THRESHOLDS).items():
                self.assertLess(report["frames"][key], limit, f"{model}-{clip} {key}")
            self.assertEqual(report["samples"], rig["samples"])
            self.assertAlmostEqual(rig["duration"], rig["samples"] / 32, places=5)
            self.assertLess(report["restAsymmetry"], 1e-3)
            self.assertTrue(np.all(np.isfinite(rig["tracks"])))
            scales = rig["tracks"][:, :, 7]
            self.assertTrue(np.all(scales > 0.2) and np.all(scales < 5))
        # Walk gaits repeat every two directions; the warrior's swim stroke never
        # repeats within the eight headings and keeps every frame's own sample.
        self.assertEqual(self.results["warrior"]["samples"], {"walk": 64, "swim": 256, "fight": 64})
        self.assertEqual(self.results["worker"]["samples"]["walk"], 64)

    def test_regeneration_is_byte_identical(self):
        for model in ("worker", "warrior"):
            fit_unit_rigs.author(model, STAGING / f"{model}-again", fit_unit_rigs.DEFAULT_ITERATIONS)
            for clip in fit_unit_rigs.CLIPS[model]:
                self.assertEqual(
                    (STAGING / model / f"{model}-{clip}.gsr").read_bytes(),
                    (STAGING / f"{model}-again" / f"{model}-{clip}.gsr").read_bytes(),
                    f"{model}-{clip}",
                )
        author_explorer_rig.author(STAGING / "explorer-again")
        self.assertEqual(
            (STAGING / "explorer/explorer-fly.gsr").read_bytes(),
            (STAGING / "explorer-again/explorer-fly.gsr").read_bytes(),
        )

    def test_installed_rigs_match_the_authoring_scripts(self):
        for model, clip, rig in self.clips():
            name = f"{model}-{clip}.gsr"
            self.assertEqual((INSTALLED / name).read_bytes(), (STAGING / model / name).read_bytes(), name)
        self.assertEqual(
            (INSTALLED / "explorer-fly.gsr").read_bytes(),
            (STAGING / "explorer/explorer-fly.gsr").read_bytes(),
        )

    def test_edited_action_exports_against_the_same_base(self):
        source = STAGING / "edited-worker.blend"
        shutil.copyfile(STAGING / "worker/worker.blend", source)
        fit_unit_rigs.author(
            "worker", STAGING / "worker-action", fit_unit_rigs.DEFAULT_ITERATIONS, source, "Swim", "swim", result=self.results["worker"]
        )
        original = read_rig((STAGING / "worker/worker-swim.gsr").read_bytes())
        imported = read_rig((STAGING / "worker-action/worker-swim.gsr").read_bytes())
        at = original["trackOffset"]
        self.assertEqual(
            (STAGING / "worker/worker-swim.gsr").read_bytes()[:at],
            (STAGING / "worker-action/worker-swim.gsr").read_bytes()[:at],
        )
        self.assertLess(np.abs(original["tracks"] - imported["tracks"]).max(), 2e-4)
        with self.assertRaisesRegex(ValueError, "separate"):
            fit_unit_rigs.author(
                "worker", STAGING / "worker", fit_unit_rigs.DEFAULT_ITERATIONS, STAGING / "worker/worker.blend", "Walk", "walk",
                result=self.results["worker"],
            )

    def test_installer_rejects_stale_sources_and_altered_bytes(self):
        folder = STAGING / "tampered"
        shutil.copytree(STAGING / "warrior", folder)
        record_path = folder / "warrior-walk-rig.json"
        record = json.loads(record_path.read_text())
        record["sources"]["tools/skins/fit_unit_rigs.py"] = "0" * 64
        record_path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, "stale"):
            install_rigs.install([folder])
        shutil.rmtree(folder)
        shutil.copytree(STAGING / "warrior", folder)
        rig = folder / "warrior-walk.gsr"
        rig.write_bytes(rig.read_bytes()[:-1] + b"\0")
        with self.assertRaisesRegex(ValueError, "do not match"):
            install_rigs.install([folder])


if __name__ == "__main__":
    if (
        not unittest.TextTestRunner(verbosity=2)
        .run(unittest.defaultTestLoader.loadTestsFromTestCase(FittedRigTest))
        .wasSuccessful()
    ):
        raise RuntimeError("Rig authoring contract failed")
