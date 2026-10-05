"""Runtime export selection, identity, cleanup and source preservation."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image
from tools import package_assets
from tools.package_assets import export_assets, include_asset


class AssetExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.root.mkdir()
        self.output = Path(self.temp.name) / "output"
        self.cache = Path(self.temp.name) / "cache"
        self.image = self.root / "data/gfx/frame0.png"
        self.image.parent.mkdir(parents=True)
        im = Image.new("RGBA", (64, 64), (20, 80, 130, 0))
        im.putpixel((0, 0), (200, 40, 30, 255))
        im.save(self.image)
        (self.root / "data/fonts").mkdir()
        (self.root / "data/fonts/LICENSE.txt").write_text("notice")

    def export(self, **kwargs):
        return export_assets(self.root, self.output, cache=self.cache, **kwargs)

    def test_exact_pixels_including_invisible_rgb_and_sources_unchanged(self):
        original = self.image.read_bytes()
        r = self.export(lossy=False)
        record = next(x for x in r["files"] if x["source"].endswith(".png"))
        self.assertTrue(record["output"].endswith(".webp"))
        self.assertEqual(
            Image.open(self.output / record["output"]).convert("RGBA").tobytes(),
            Image.open(self.image).convert("RGBA").tobytes(),
        )
        self.assertEqual(self.image.read_bytes(), original)
        self.assertTrue((self.output / "data/fonts/LICENSE.txt").is_file())
        self.assertFalse((self.output / "export.json").exists())

    def test_cached_export_is_repeatable_and_stale_files_disappear(self):
        first = self.export()
        self.assertEqual(first, self.export())
        self.image.unlink()
        self.export()
        self.assertFalse(any((self.output / "data/gfx").glob("*")))

    def test_cache_blob_corruption_is_repaired(self):
        first = self.export()
        for blob in self.cache.glob("*/image.*"):
            blob.write_bytes(b"corrupt")
        self.assertEqual(first, self.export())

    def test_cache_metadata_corruption_is_repaired(self):
        first = self.export()
        for metadata in self.cache.glob("*/selection.json"):
            metadata.write_text("{bad json")
        self.assertEqual(first, self.export())

    def test_source_change_invalidates_cache(self):
        first = self.export()
        Image.new("RGBA", (64, 64), (60, 80, 130, 255)).save(self.image)
        second = self.export()
        self.assertNotEqual(first["files"], second["files"])

    def test_filters_metadata_but_preserves_linux_store_screenshot(self):
        self.assertFalse(include_asset("data/highres/v1/manifest.json"))
        self.assertFalse(include_asset("data/fonts/build_chinese_font.py"))
        self.assertTrue(include_asset("data/highres/v1/frames.txt"))
        self.assertFalse(include_asset("data/screenshots/game.png", "macos"))
        self.assertTrue(include_asset("data/screenshots/game.png", "linux"))
        self.assertTrue(include_asset("data/fonts/README.md"))

    def test_refuses_source_and_unowned_output(self):
        with self.assertRaises(ValueError):
            export_assets(self.root, self.root / "data")
        self.output.mkdir()
        (self.output / "precious").write_text("retain")
        with self.assertRaises(ValueError):
            self.export()
        self.assertEqual((self.output / "precious").read_text(), "retain")

    def test_old_export_upgrade_format_changes_and_unrelated_content(self):
        import json, random
        # Old ownership format, with no global image recipe, remains upgradeable.
        first = self.export(optimized=False)
        first.pop("image_recipe")
        first["lossy_background"] = first.pop("lossy_images")
        self.output.with_suffix(".json").write_text(json.dumps(first))
        (self.output / "user.txt").write_text("retain")
        Image.frombytes("RGB", (64, 64), random.Random(99).randbytes(64*64*3)).save(self.image)
        second = self.export()
        record = next(x for x in second["files"] if x["source"].endswith(".png"))
        self.assertEqual(record["output"], "data/gfx/frame0.webp")
        self.assertFalse((self.output / "data/gfx/frame0.png").exists())
        self.assertEqual((self.output / "user.txt").read_text(), "retain")
        self.export(optimized=False)
        self.assertFalse((self.output / "data/gfx/frame0.webp").exists())
        self.assertTrue((self.output / "data/gfx/frame0.png").exists())

    def test_corrupt_source_keeps_previous_export(self):
        self.export()
        old = (self.output.with_suffix(".json")).read_bytes()
        self.image.write_bytes(b"corrupt image")
        with self.assertRaises(Exception):
            self.export()
        self.assertEqual((self.output.with_suffix(".json")).read_bytes(), old)

    def test_failed_audit_replacement_rolls_back_export(self):
        self.export()
        previous = {
            p.relative_to(self.output): p.read_bytes()
            for p in self.output.rglob("*")
            if p.is_file()
        }
        marker = self.output.with_suffix(".json")
        old_audit = marker.read_bytes()
        self.image.rename(self.image.with_name("renamed.png"))
        import os

        replace = os.replace

        def fail_audit(source, destination):
            if Path(destination).resolve() == marker.resolve():
                raise OSError("Simulated audit replacement failure")
            return replace(source, destination)

        with patch("tools.package_assets.os.replace", side_effect=fail_audit):
            with self.assertRaises(OSError):
                self.export()
        self.assertEqual(marker.read_bytes(), old_audit)
        self.assertEqual(
            {
                p.relative_to(self.output): p.read_bytes()
                for p in self.output.rglob("*")
                if p.is_file()
            },
            previous,
        )
        self.assertFalse(self.output.with_name(self.output.name + "-previous").exists())

    def test_original_profile_needs_no_conversion(self):
        self.export(optimized=False)
        self.assertEqual(
            (self.output / "data/gfx/frame0.png").read_bytes(), self.image.read_bytes()
        )

    def test_high_depth_png_is_not_quantized_to_webp(self):
        im = Image.new("I;16", (32, 32))
        im.putpixel((0, 0), 257)
        im.save(self.image)
        original = self.image.read_bytes()
        with self.assertRaisesRegex(ValueError, "Unsupported higher-depth"):
            self.export()
        self.assertEqual(self.image.read_bytes(), original)

    def test_linux_screenshots_keep_their_png_representation(self):
        folder = self.root / "data/screenshots"
        folder.mkdir()
        screenshot = folder / "game.png"
        screenshot.write_bytes(self.image.read_bytes())
        self.export(platform="linux")
        self.assertEqual(
            (self.output / "data/screenshots/game.png").read_bytes(),
            screenshot.read_bytes(),
        )

    def test_global_q90_policy_preserves_alpha_and_sources(self):
        import random
        for name, mode in (("frame0", "RGBA"), ("menu-colony", "RGB"),
                           ("loading-wordmark", "RGBA"), ("gamegui0", "RGBA")):
            path = self.image.parent / (name + ".png")
            Image.frombytes(mode, (64, 64), random.Random(99).randbytes(64 * 64 * len(mode))).save(path)
        originals = {p: p.read_bytes() for p in self.image.parent.glob("*.png")}
        audit = self.export()
        self.assertTrue(audit["lossy_images"])
        self.assertEqual(audit["image_recipe"], package_assets.image_recipe())
        for record in audit["files"]:
            if not record["source"].endswith(".png"):
                continue
            self.assertTrue(record["lossy"])
            self.assertEqual(record["recipe"]["lossy_quality"], 90)
            self.assertEqual(record["recipe"]["lossy_method"], 6)
            original = Image.open(self.root / record["source"]).convert("RGBA")
            decoded = Image.open(self.output / record["output"]).convert("RGBA")
            self.assertEqual(original.size, decoded.size)
            self.assertEqual(original.getchannel("A").tobytes(), decoded.getchannel("A").tobytes())
        for p, data in originals.items():
            self.assertEqual(p.read_bytes(), data)

    def test_png_cannot_win_even_when_smaller_and_hd_index_uses_webp(self):
        # This artwork's optimized PNG is smaller than either WebP encoding.
        source = self.root / "data/gfx/gamegui47.png"
        actual = package_assets.ROOT / "data/gfx/gamegui47.png"
        source.write_bytes(actual.read_bytes())
        hd = self.root / "data/highres/v1"
        hd.mkdir(parents=True)
        text = "glob2-highres 1\nframe0 64 64 4 base.png team.png\n"
        (hd / "frames.txt").write_text(text)
        audit = self.export()
        self.assertTrue(all(r["output"].endswith(".webp") for r in audit["files"]
                            if r["source"].endswith(".png")))
        self.assertEqual((hd / "frames.txt").read_text(), text)
        self.assertEqual((self.output / "data/highres/v1/frames.txt").read_text(),
                         text.replace(".png", ".webp"))

    def test_source_hd_atlas_placement_is_verified_before_encoding(self):
        directory = self.root / "data/highres/v1"
        directory.mkdir(parents=True)
        frame = Image.new("RGBA", (8, 8), (37, 53, 83, 177))
        frame.save(directory / "terrain0.png")
        (directory / "frames.txt").write_text("GLOB2_HIGHRES 1\nterrain0 2 2 4 terrain0.png -\n")
        atlas = Image.new("RGBA", (256, 256))
        atlas.paste(frame, (64, 64))
        atlas.save(directory / "terrain-atlas-mip0.png")
        package_assets.verify_highres_atlases(self.root)
        atlas.putpixel((64, 64), (99, 99, 99, 177))
        atlas.save(directory / "terrain-atlas-mip0.png")
        with self.assertRaisesRegex(ValueError, "Source atlas frame placement"):
            self.export()

    def test_smaller_lossless_candidate_wins(self):
        record = next(x for x in self.export()["files"] if x["source"].endswith(".png"))
        self.assertFalse(record["lossy"])

    def test_q90_encoder_parameters(self):
        calls = []
        save = Image.Image.save
        def capture(image, fp, format=None, **kwargs):
            if format == "WEBP":
                calls.append(kwargs)
            return save(image, fp, format, **kwargs)
        with patch.object(Image.Image, "save", capture):
            self.export()
        self.assertTrue(any(c.get("lossless") is False and c.get("quality") == 90
                            and c.get("method") == 6 and c.get("exact") is True for c in calls))

    def test_recipe_change_invalidates_cache(self):
        self.export()
        count = len(list(self.cache.glob("*/selection.json")))
        with patch.object(package_assets, "IMAGE_RECIPE", "future-recipe"):
            audit = self.export()
        self.assertGreater(len(list(self.cache.glob("*/selection.json"))), count)
        self.assertEqual(audit["image_recipe"]["image_recipe"], "future-recipe")

    def test_rgba16_png_filters_and_adam7_reference(self):
        import random, struct, zlib
        width, height = 13, 11
        samples = [random.Random(91 + i).randrange(65536) for i in range(width * height * 4)]
        expected = bytes((v + 128) // 257 for v in samples)
        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
        for interlace in (0, 1):
            passes = ((0, 0, 1, 1),) if not interlace else (
                (0,0,8,8), (4,0,8,8), (0,4,4,8), (2,0,4,4), (0,2,2,4), (1,0,2,2), (0,1,1,2))
            scanlines = bytearray()
            for sx, sy, dx, dy in passes:
                previous = bytes(len(range(sx, width, dx)) * 8)
                for row_index, y in enumerate(range(sy, height, dy)):
                    values = [v for x in range(sx, width, dx) for v in samples[(y*width+x)*4:(y*width+x+1)*4]]
                    row = struct.pack(">" + "H" * len(values), *values)
                    filter_type = row_index % 5
                    scanlines.append(filter_type)
                    for i, value in enumerate(row):
                        a, b, c = row[i-8] if i >= 8 else 0, previous[i], previous[i-8] if i >= 8 else 0
                        p = a + b - c
                        paeth = min((a, b, c), key=lambda v: abs(p-v))
                        prediction = (0, a, b, (a+b)//2, paeth)[filter_type]
                        scanlines.append((value - prediction) % 256)
                    previous = row
            raw = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 16, 6, 0, 0, interlace))
            raw += chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")
            self.assertEqual(package_assets.decoded_rgba(raw).tobytes(), expected)

    def test_high_depth_rgba_uses_renderer_reference(self):
        import random, struct, zlib
        width = height = 64
        # Construct 16-bit RGBA without relying on Pillow's 8-bit-only writer.
        rgba = random.Random(42).randbytes(width * height * 4)
        rows = b"".join(b"\0" + b"".join(bytes((v, 123)) for v in rgba[y*width*4:(y+1)*width*4])
                        for y in range(height))
        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
        self.image.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 16, 6, 0, 0, 0))
                              + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
        original = self.image.read_bytes()
        record = next(x for x in self.export()["files"] if x["source"].endswith(".png"))
        self.assertTrue(record["lossy"])
        self.assertEqual(record["recipe"]["depth_action"], "renderer-rgba8-round-nearest-div257")
        decoded = Image.open(self.output / record["output"]).convert("RGBA")
        expected = bytes(((v * 256 + 123 + 128) // 257) for v in rgba)
        self.assertEqual(decoded.getchannel("A").tobytes(), expected[3::4])
        self.assertNotEqual(expected[3::4], rgba[3::4])
        self.assertEqual(self.image.read_bytes(), original)
        audit = self.export(lossy=False)
        record = next(x for x in audit["files"] if x["source"].endswith(".png"))
        self.assertFalse(record["lossy"])
        self.assertTrue(record["output"].endswith(".webp"))
        self.assertEqual(Image.open(self.output / record["output"]).convert("RGBA").tobytes(), expected)
        self.assertEqual(self.image.read_bytes(), original)



class SpriteSheetExportTests(unittest.TestCase):
    """Optimized exports pack data/gfx/unit's frames into sheets for Sprite::load."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.gfx = self.root / "data/gfx"
        self.gfx.mkdir(parents=True)
        self.output = Path(self.temp.name) / "output"
        self.cache = Path(self.temp.name) / "cache"
        self.frames = {}
        # Frames 0-4 recolorable at 3x3 and 5-6 at 4x4; shadows on 2-3 only.
        for index in range(7):
            for suffix in ["r"] + ([""] if index in (2, 3) else []):
                size = 3 if index < 5 else 4
                pixels = bytes((index * 37 + len(suffix) * 11 + p) % 256 for p in range(size * size * 4))
                name = "unit%d%s.png" % (index, suffix)
                Image.frombytes("RGBA", (size, size), pixels).save(self.gfx / name)
                self.frames[name] = pixels
        Image.new("RGBA", (5, 5)).save(self.gfx / "unitmini0.png")

    def export(self, **kwargs):
        return export_assets(self.root, self.output, cache=self.cache, **kwargs)

    def unpack(self):
        """{frame name: RGBA bytes} cut out of the exported sheets like Sprite::load."""
        frames = {}
        index = (self.output / "data/gfx/unit.sheet").read_text()
        for line in index.splitlines():
            if not line or line.startswith("#"):
                continue
            name, layer, first, count, width, height = line.split()
            first, count, width, height = int(first), int(count), int(width), int(height)
            sheet = next((self.output / "data/gfx").glob(Path(name).stem + ".*"))
            with Image.open(sheet) as image:
                image = image.convert("RGBA")
                self.assertEqual(image.width % width, 0)
                columns = image.width // width
                for i in range(count):
                    left, top = (i % columns) * width, (i // columns) * height
                    tile = image.crop((left, top, left + width, top + height))
                    key = "unit%d%s.png" % (first + i, "r" if layer == "rotated" else "")
                    self.assertNotIn(key, frames)
                    frames[key] = tile.tobytes()
        return frames

    def test_sheets_hold_every_frame_exactly_and_replace_the_frames(self):
        audit = self.export(lossy=False)
        self.assertEqual(self.unpack(), self.frames)
        shipped = {p.name for p in (self.output / "data/gfx").iterdir()}
        self.assertFalse(any(name.startswith("unit") and name[4].isdigit() for name in shipped))
        self.assertTrue(any(name.startswith("unitmini0.") for name in shipped))
        packed = [item for item in audit["files"] if "packed_from" in item]
        self.assertEqual(
            sorted(f["source"] for item in packed for f in item["packed_from"]),
            sorted("data/gfx/" + name for name in self.frames),
        )
        self.assertEqual(audit["source_bytes"], sum(p.stat().st_size for p in self.gfx.iterdir()))

    def test_runs_split_by_layer_size_and_sheet_capacity(self):
        with patch.object(package_assets, "SHEET_FRAMES", 2):
            self.export(lossy=False)
            self.assertEqual(self.unpack(), self.frames)
        lines = [
            line.split()[1:]
            for line in (self.output / "data/gfx/unit.sheet").read_text().splitlines()
            if line and not line.startswith("#")
        ]
        self.assertEqual(
            lines,
            [
                ["image", "2", "2", "3", "3"],
                ["rotated", "0", "2", "3", "3"],
                ["rotated", "2", "2", "3", "3"],
                ["rotated", "4", "1", "3", "3"],
                ["rotated", "5", "2", "4", "4"],
            ],
        )

    def test_cached_export_is_repeatable(self):
        first = self.export(lossy=False)
        self.assertEqual(first, self.export(lossy=False))
        Image.new("RGBA", (3, 3), (1, 2, 3, 4)).save(self.gfx / "unit1r.png")
        self.frames["unit1r.png"] = bytes((1, 2, 3, 4)) * 9
        self.assertNotEqual(first, self.export(lossy=False))
        self.assertEqual(self.unpack(), self.frames)

    def test_default_sheet_alpha_and_corrupt_packed_cache(self):
        first = self.export()
        for name, pixels in self.unpack().items():
            self.assertEqual(pixels[3::4], self.frames[name][3::4])
        for packed in self.cache.glob("sheet-*/*.png"):
            packed.write_bytes(b"corrupt")
        self.assertEqual(first, self.export())

    def test_original_profile_keeps_the_frames(self):
        self.export(optimized=False)
        self.assertFalse((self.output / "data/gfx/unit.sheet").exists())
        self.assertEqual((self.output / "data/gfx/unit5r.png").read_bytes(), (self.gfx / "unit5r.png").read_bytes())

    def test_frames_behind_a_gap_are_rejected(self):
        Image.new("RGBA", (4, 4)).save(self.gfx / "unit8r.png")
        with self.assertRaises(ValueError):
            self.export()


if __name__ == "__main__":
    unittest.main()
