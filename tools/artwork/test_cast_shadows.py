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


class PaintOutTests(unittest.TestCase):
    """A shadow painted opaque into the art is relit to the ground around it, once."""

    def sprite(self):
        image = np.zeros((160, 160, 4), np.uint8)
        image[..., :3] = (225, 210, 160)  # lit sand
        image[..., 3] = 255
        noise = np.random.default_rng(1).integers(-6, 7, (160, 160, 1))
        image[..., :3] = np.clip(image[..., :3] + noise, 0, 255)
        image[40:120, 20:110, :3] = np.clip(np.array([40, 36, 30]) + noise[40:120, 20:110], 0, 255)  # the shadow
        image[60:70, 130:140, :3] = 20  # a small dark detail: kept
        return image

    def test_relights_large_dark_regions_only_and_is_idempotent(self):
        spec = {"strip": True, "paint_out": {"method": "relight"}}
        hd = self.sprite()
        classic = np.asarray(Image.fromarray(hd).resize((40, 40), Image.Resampling.BOX))
        once_hd, once_classic = tool.deshadow(hd, classic, spec)
        self.assertGreater(once_hd[80, 60, :3].min(), 150)  # relit to sand
        self.assertEqual(int(once_hd[65, 135, 0]), 20)      # the small detail stays
        self.assertFalse(tool.paint_out_mask(once_hd, {}).any())
        self.assertGreater(once_classic[20, 15, :3].min(), 150)
        twice_hd, twice_classic = tool.deshadow(once_hd, once_classic, spec)
        self.assertTrue(np.array_equal(once_hd, twice_hd))
        self.assertTrue(np.array_equal(once_classic, twice_classic))


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
        self.assertLessEqual({"tools/artwork/cast_shadows.py", str(tool.SPECS), "content/data/highres/v1/hut0.png",
                              "content/data/gfx/hut0.png"}, {s["path"] for s in shadow["sources"]})
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


class MeshMethodTests(RegistrationTests):
    """`mesh` specs register mesh_shadows.py's committed mask, padded, offline."""

    def setUp(self):
        super().setUp()
        mesh = self.root / tool.MESH
        mesh.mkdir(parents=True)
        mask = np.zeros((70, 75), np.uint8)  # the 64x64 frame plus a reach past it
        mask[30:70, 30:75] = 200
        alpha = np.zeros(mask.shape + (4,), np.uint8)
        alpha[..., 3] = mask
        Image.fromarray(alpha).save(mesh / "hut0s.png")
        (mesh / "hut0.json").write_text(json.dumps(dict(frame="hut0", shadow=dict(length=0.45))))
        (self.root / tool.SPECS).write_text(json.dumps(dict(version=1, families=[
            dict(frames=["hut0"], method="mesh", length=0.45)])))

    def test_registers_both_copies_and_reruns_without_change(self):
        super().test_registers_both_copies_and_reruns_without_change()
        hd = np.asarray(Image.open(self.root / highres_pack.RUNTIME / "hut0s.png"))[..., 3]
        self.assertEqual(hd.shape, (80, 80))  # padded to whole 16 px steps
        self.assertEqual(int(hd[69, 74]), 200)
        self.assertEqual(int(hd[75, 79]), 0)
        manifest = json.loads((self.root / highres_pack.RUNTIME / "manifest.json").read_text())
        sources = {s["path"] for s in manifest["frames"][1]["sources"]}
        self.assertTrue({str(tool.MESH / "hut0s.png"), str(tool.MESH / "hut0.json")} <= sources)
        self.assertEqual(manifest["frames"][1]["recipe"], tool.RECIPE + ": mesh")

    def test_a_mask_cast_at_another_length_is_refused(self):
        (self.root / tool.SPECS).write_text(json.dumps(dict(version=1, families=[
            dict(frames=["hut0"], method="mesh", length=0.6)])))
        with self.assertRaisesRegex(ValueError, "rerun"):
            self.cast()

    def test_variants_share_a_mask(self):
        spec = dict(tool.DEFAULTS, method="mesh", mesh="hut0", length=0.45)
        self.assertEqual(tool.mesh_layer(spec, (64, 64), self.root).shape, (80, 80))

    def test_frames_leaving_the_spec_are_pruned(self):
        pass


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


class ProxyTests(unittest.TestCase):
    FRUSTUM = [100.0, 80.0, 60.0, 60.0 * 0.8625, 100.0, 170.0, 40.0, 40.0 * 0.8625]

    def test_the_fit_recovers_a_round_frustum(self):
        body = tool.frustum_mask(self.FRUSTUM, (260, 200))
        start = [v + d for v, d in zip(self.FRUSTUM, (4, -6, 5, 0, -3, 5, -4, 0))]
        fit, overlap = tool.fit_frustum(body, start)
        self.assertGreater(overlap, 0.97)
        for got, want in zip(fit, self.FRUSTUM):
            self.assertAlmostEqual(got, want, delta=4)

    def test_a_banner_held_up_casts_apart_from_the_base(self):
        team = np.zeros((260, 200), bool)
        team[10:40, 150:160] = True  # a banner well above the rim's right side
        spec = dict(tool.DEFAULTS, method="proxy", length=0.8,
                    proxy={"frustum": self.FRUSTUM, "banners": [[140, 0, 170, 45]]})
        canvas = np.zeros((260 + tool.MARGIN, 200 + tool.MARGIN), bool)
        canvas[:260, :200] = tool.frustum_mask(self.FRUSTUM, (260, 200))
        banner = np.zeros(canvas.shape, bool)
        banner[:260, :200] = team
        with_banner = tool.proxy_shadow(canvas, banner, spec) > 0.5
        without = tool.proxy_shadow(canvas, np.zeros_like(banner), spec) > 0.5
        extra = with_banner & ~without
        self.assertTrue(extra.any())
        # Its shadow lands down-right of the banner's own ground point, not on a
        # column running down from the banner to the ground.
        self.assertFalse(extra[40:60, 150:160].any())


if __name__ == "__main__":
    unittest.main()
