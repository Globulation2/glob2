"""mesh_shadows.py: alignment recovers a synthetic building's camera, casting follows
the sun and the length, and the gate rejects relief meshes. Needs trimesh
(tools/artwork/ai/requirements-mesh.txt); skipped without it."""

import json
import math
from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image

try:
    import trimesh
except ImportError:  # the optional mesh environment
    trimesh = None

from tools.artwork import cast_shadows
from tools.artwork import mesh_shadows as tool


def sprite_of(points, params, elevation, shape):
    """The silhouette a mesh would draw at a known camera (the test's 'artist')."""
    xy, _ = tool.project(points, params, elevation)
    return tool.splat(xy, shape)


@unittest.skipIf(trimesh is None, "needs the mesh environment")
class MeshTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)

    def box(self, name, extents, tower=False):
        """A GLB (y up): a block, with a tall thin tower at one corner if asked."""
        mesh = trimesh.creation.box(extents=extents)
        mesh.apply_translation((0, extents[1] / 2, 0))
        if tower:
            spire = trimesh.creation.box(extents=(extents[0] / 6, extents[1] * 3, extents[2] / 6))
            spire.apply_translation((extents[0] / 3, extents[1] * 1.5, -extents[2] / 3))
            mesh = trimesh.util.concatenate([mesh, spire])
        path = self.dir / f"{name}.glb"
        mesh.export(path)
        return path

    def test_alignment_recovers_a_known_camera(self):
        points = tool.load_points(self.box("hut", (1.0, 0.4, 0.7), tower=True), count=60000)
        elevation = math.radians(tool.ELEVATION_DEG)
        truth = (math.radians(30), 0.0, math.log(150), 120.0, 140.0)
        target = sprite_of(points, truth, elevation, (256, 256))
        params, mirrored, fit = tool.align(points, target, elevation)
        self.assertGreater(fit, 0.9)
        xy, _ = tool.project(points * ((-1, 1, 1) if mirrored else 1), params, elevation)
        self.assertGreater(tool.tolerant_fit(tool.splat(xy, target.shape), target, 3), 0.97)

    def test_a_point_casts_along_the_sun_by_its_length(self):
        shape = (200, 200)
        for length in (0.45, 1.1):
            shadow = tool.cast(np.array([50.0]), np.array([40.0]), np.array([60.0]), shape, length=length)
            ys, xs = np.nonzero(shadow)
            throw = 61.5 * length  # the height bin's centre (60 // 3 + 0.5) * 3
            self.assertEqual(xs.min(), 50)
            self.assertEqual(ys.min(), 100)  # the ground point: 60 px below
            self.assertAlmostEqual(xs.max(), 50 + round(throw * cast_shadows.SHADOW[0]), delta=1)
            self.assertAlmostEqual(ys.max(), 100 + round(throw * cast_shadows.SHADOW[1]), delta=1)

    def test_throws_are_capped(self):
        shadow = tool.cast(np.array([10.0]), np.array([10.0]), np.array([600.0]), (1000, 1000), length=1.1)
        ys, _ = np.nonzero(shadow)
        self.assertLessEqual(ys.max(), 610 + round(tool.CAP * cast_shadows.SHADOW[1]) + 1)

    def test_gate_rejects_a_relief_and_keeps_a_solid(self):
        sprite_dir = self.dir / "sprites"
        sprite_dir.mkdir()
        results = {}
        for name, extents in (("solid", (1.0, 0.5, 0.9)), ("relief", (1.0, 0.9, 0.08))):
            mesh = self.box(name, extents)
            points = tool.load_points(mesh, count=60000)
            body = sprite_of(points, (0.0, 0.0, math.log(120), 100.0, 110.0), math.radians(tool.ELEVATION_DEG),
                             (224, 224))
            rgba = np.zeros(body.shape + (4,), np.uint8)
            rgba[body] = (180, 160, 140, 255)
            Image.fromarray(rgba).save(sprite_dir / f"{name}.png")
            results[name] = tool.evaluate((name, 1, mesh, sprite_dir / f"{name}.png", None,
                                           tool.ELEVATION_DEG, 0.6, (0.6,)))
        self.assertTrue(results["solid"]["passed"], results["solid"]["reasons"])
        self.assertFalse(results["relief"]["passed"])
        self.assertIn("relief", " ".join(results["relief"]["reasons"]))
        mask = results["solid"]["mask"]
        self.assertGreaterEqual(mask.shape[0], 224)
        self.assertGreater(int(mask.max()), 200)

    def test_record_keeps_mesh_camera_and_length(self):
        mesh = self.box("hut", (1.0, 0.5, 0.9))
        points = tool.load_points(mesh, count=60000)
        body = sprite_of(points, (0.3, 0.0, math.log(120), 100.0, 110.0), math.radians(tool.ELEVATION_DEG),
                         (224, 224))
        rgba = np.zeros(body.shape + (4,), np.uint8)
        rgba[body] = (180, 160, 140, 255)
        Image.fromarray(rgba).save(self.dir / "hut.png")
        (self.dir / "hut.json").write_text(json.dumps(dict(model="m", endpoint="e", mesh_sha256="a" * 64,
                                                           input_sha256="b" * 64, sources={})))
        result = tool.evaluate(("hut", 1, mesh, self.dir / "hut.png", None, tool.ELEVATION_DEG, 0.45, ()))
        entry = tool.record(result, self.dir, self.dir / "hut.png", None, 0.45)
        self.assertEqual(entry["shadow"]["length"], 0.45)
        self.assertEqual(entry["camera"]["elevation_deg"], tool.ELEVATION_DEG)
        self.assertEqual(entry["mesh_sha256"], "a" * 64)


if __name__ == "__main__":
    unittest.main()
