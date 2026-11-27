"""cast_shadows.py: the shadow geometry, de-shadowing, classic = HD area-averaged,
registration in both pack copies, and a rerun that changes nothing."""

import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image

from tools.artwork import cast_shadows as tool
from tools.artwork import highres_pack


def block(width, height, colour=(200, 180, 160, 255)):
    image = np.zeros((height, width, 4), np.uint8)
    image[:] = colour
    return image


class GeometryTests(unittest.TestCase):
    def test_sun_comes_from_sunlight_header(self):
        self.assertAlmostEqual(tool.SHADOW[0], 0.6, places=6)
        self.assertAlmostEqual(tool.SHADOW[1], 0.8, places=6)
        self.assertEqual(tool.FLIGHT_HEIGHT, 52)

    def test_a_block_casts_its_height_times_ratio_to_the_lower_right(self):
        body = np.zeros((200, 200), bool)
        body[20:60, 20:40] = True  # a 20 px wide sprite, 40 px tall
        spec = dict(tool.DEFAULTS, method="block", height=20, blur=0.01)
        shadow = tool.block_shadow(body, spec) > 0.5
        ys, xs = np.nonzero(shadow & ~body)
        self.assertGreater(len(xs), 0)
        self.assertGreaterEqual(xs.min(), 20)
        self.assertGreaterEqual(ys.min(), 20)
        # The far corner moves 20 * 1.1 = 22 px along (0.6, 0.8).
        self.assertEqual(xs.max(), 39 + round(22 * 0.6))
        self.assertEqual(ys.max(), 59 + round(22 * 0.8))

    def test_spires_are_capped_under_the_flight_height(self):
        body = np.zeros((900, 300), bool)
        body[0:600, 50:54] = True  # a 600 px mast
        spec = dict(tool.DEFAULTS, method="block", height=0, spires=[[40, 0, 60, 600, 600, 0]], blur=0.01)
        ys, xs = np.nonzero(tool.block_shadow(body, spec) > 0.5)
        self.assertLessEqual(ys.max(), 600 + round(180 * 0.8) + 2)

    def test_canopy_shadow_lies_below_and_right_of_the_trunk(self):
        body = np.zeros((300, 300), bool)
        body[20:100, 40:80] = True
        mask = tool.canopy_shadow(body, dict(tool.DEFAULTS, method="canopy", ratio=0.5, blur=7))
        ys, xs = np.nonzero(mask > 0.5)
        self.assertGreater(ys.max(), 100)
        self.assertGreater(xs.max(), 80)

    def test_cast_pads_whole_steps_and_classic_is_hd_area_averaged(self):
        base = np.zeros((64, 64, 4), np.uint8)
        base[8:56, 8:56] = (150, 120, 90, 255)
        hd, classic = tool.cast(base, None, dict(tool.DEFAULTS, method="block", height=30))
        self.assertEqual((hd.shape[0] - 64) % 16, 0)
        self.assertEqual((hd.shape[1] - 64) % 16, 0)
        self.assertGreater(hd.shape[0], 64)
        self.assertEqual(classic.shape, (hd.shape[0] // 4, hd.shape[1] // 4))
        expected = np.rint(hd.reshape(classic.shape[0], 4, classic.shape[1], 4).mean((1, 3)) + 1e-9)
        self.assertTrue(np.array_equal(classic, expected.astype(np.uint8)))
        self.assertTrue(np.array_equal(tool.downscale(hd), classic))

    def test_team_layer_pixels_cast_even_when_smaller(self):
        base = np.zeros((64, 64, 4), np.uint8)
        team = np.zeros((60, 60, 4), np.uint8)
        team[10:40, 10:40, 3] = 40  # translucent team art still casts
        hd, _ = tool.cast(base, team, dict(tool.DEFAULTS, method="block", height=10))
        self.assertGreater(hd.max(), 0)


class StripTests(unittest.TestCase):
    def test_strip_removes_only_dark_translucent_pixels_and_is_idempotent(self):
        image = np.zeros((1, 4, 4), np.uint8)
        image[0] = [(10, 10, 10, 120),    # baked shadow: removed
                    (10, 10, 10, 255),    # opaque dark outline: kept
                    (200, 200, 230, 90),  # translucent bright blade: kept
                    (50, 40, 40, 100)]    # translucent, not dark enough: kept
        once = tool.strip(image)
        self.assertEqual(list(once[0, :, 3]), [0, 255, 90, 100])
        self.assertTrue(np.array_equal(tool.strip(once), once))


class RegistrationTests(unittest.TestCase):
    """A one-frame pack: write, then rerun with no change."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = self.root = Path(self.temp.name)
        production = root / highres_pack.PRODUCTION
        runtime = root / highres_pack.RUNTIME
        gfx = root / "content/data/gfx"
        for directory in (production / "pack-metadata", production / "ai-upscaled", runtime, gfx,
                          root / tool.SPECS.parent):
            directory.mkdir(parents=True)
        classic = block(16, 16)
        classic[12:16, 12:16] = (5, 5, 5, 100)  # a baked shadow
        Image.fromarray(classic).save(gfx / "hut0.png")
        hd = np.kron(classic, np.ones((4, 4, 1), np.uint8))
        Image.fromarray(hd).save(production / "ai-upscaled/hut0.png")
        (runtime / "hut0.png").write_bytes((production / "ai-upscaled/hut0.png").read_bytes())
        digest = highres_pack.file_sha(runtime / "hut0.png")
        manifest = dict(version=1, frames=[dict(id="hut0", width=16, height=16, scale=4, recipe="current",
                        layers=[dict(file="hut0.png", role="base", sha256=digest)], sources=[])])
        (production / "pack-metadata/manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        (production / "pack-metadata/frames.txt").write_text("GLOB2_HIGHRES 1\nhut0 16 16 4 hut0.png -\n")
        index = dict(version=1, files=[
            dict(runtime="hut0.png", source="ai-upscaled/hut0.png", sha256=digest),
            dict(runtime="manifest.json", source="pack-metadata/manifest.json", sha256=""),
            dict(runtime="frames.txt", source="pack-metadata/frames.txt", sha256="")])
        (production / "package.json").write_text(json.dumps(index, indent=2) + "\n")
        for name in ("manifest.json", "frames.txt"):
            (runtime / name).write_bytes((production / "pack-metadata" / name).read_bytes())
        (root / tool.SPECS).write_text(json.dumps(dict(version=1, families=[
            dict(frames=["hut0"], method="block", height=20)])))

    def cast(self):
        specs = tool.load_specs(self.root)
        return tool.write(tool.run(list(specs), specs, self.root, jobs=1), specs, self.root)

    def test_registers_both_copies_and_reruns_without_change(self):
        changed = self.cast()
        self.assertTrue(changed)
        root = self.root
        runtime = root / highres_pack.RUNTIME
        rows = (runtime / "frames.txt").read_text().splitlines()
        self.assertEqual(rows[2].split()[0], "hut0s")
        self.assertEqual(rows[2].split()[3:], ["4", "hut0s.png", "-"])
        manifest = json.loads((runtime / "manifest.json").read_text())
        shadow = manifest["frames"][1]
        self.assertEqual(shadow["id"], "hut0s")
        self.assertTrue(shadow["recipe"].startswith(tool.RECIPE))
        self.assertEqual({s["path"] for s in shadow["sources"]},
                         {"tools/artwork/cast_shadows.py", str(tool.SPECS), "content/data/highres/v1/hut0.png",
                          "content/data/gfx/hut0.png"})
        hd = np.asarray(Image.open(runtime / "hut0s.png"))
        classic = np.asarray(Image.open(root / "content/data/gfx/hut0s.png"))
        self.assertTrue(np.array_equal(tool.downscale(hd[..., 3]), classic[..., 3]))
        self.assertFalse(hd[..., :3].any())
        # The baked shadow left both bases; the production copy follows.
        for path in (root / "content/data/gfx/hut0.png", runtime / "hut0.png"):
            self.assertFalse(tool.strip_mask(np.asarray(Image.open(path).convert("RGBA"))).any())
        index = json.loads((root / highres_pack.PRODUCTION / "package.json").read_text())
        records = {r["runtime"]: r for r in index["files"]}
        self.assertEqual(records["hut0s.png"]["source"], "shadows/hut0s.png")
        self.assertEqual(records["hut0.png"]["sha256"], highres_pack.file_sha(runtime / "hut0.png"))
        before = {p: p.read_bytes() for p in root.rglob("*") if p.is_file()}
        self.assertEqual(self.cast(), [])
        self.assertEqual(before, {p: p.read_bytes() for p in root.rglob("*") if p.is_file()})

    def test_frames_leaving_the_spec_are_pruned(self):
        self.cast()
        (self.root / tool.SPECS).write_text(json.dumps(dict(version=1, families=[])))
        self.assertEqual(tool.prune({}, self.root), ["hut0s"])
        self.assertFalse((self.root / "content/data/gfx/hut0s.png").exists())
        self.assertFalse((self.root / highres_pack.RUNTIME / "hut0s.png").exists())


class SpecTests(unittest.TestCase):
    def test_committed_specs_load_and_name_existing_frames(self):
        specs = tool.load_specs()
        self.assertIn("barracks1b0", specs)
        for frame_id in specs:
            self.assertTrue((tool.ROOT / tool.GFX / f"{frame_id}.png").exists(), frame_id)

    def test_unknown_fields_and_high_caps_are_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / tool.SPECS.parent).mkdir(parents=True)
            for family in (dict(frames=["a0"], method="block", tall=3), dict(frames=["a0"], method="block", cap=999)):
                (root / tool.SPECS).write_text(json.dumps(dict(version=1, families=[family])))
                with self.assertRaises(ValueError):
                    tool.load_specs(root)


if __name__ == "__main__":
    unittest.main()
