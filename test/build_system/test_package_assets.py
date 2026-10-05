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
        r = self.export()
        record = next(x for x in r["files"] if x["source"].endswith(".png"))
        self.assertLessEqual(record["output_bytes"], record["source_bytes"])
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
        self.export()
        self.assertEqual((self.output / "data/gfx/frame0.png").read_bytes(), original)

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

    def test_lossy_allowlist_never_changes_sprite_policy(self):
        r = self.export(lossy=True)
        self.assertFalse(
            next(x for x in r["files"] if x["source"].endswith(".png"))["lossy"]
        )

    def test_only_opaque_menu_illustration_can_select_lossy_webp(self):
        import random

        background = self.image.parent / "menu-colony.png"
        Image.frombytes("RGB", (64, 64), random.Random(99).randbytes(64 * 64 * 3)).save(
            background
        )
        wordmark = self.image.parent / "loading-wordmark.png"
        wordmark.write_bytes(background.read_bytes())
        records = {item["source"]: item for item in self.export()["files"]}
        self.assertTrue(records["data/gfx/menu-colony.png"]["lossy"])
        self.assertEqual(
            records["data/gfx/menu-colony.png"]["recipe"]["lossy_quality"], 90
        )
        self.assertFalse(records["data/gfx/loading-wordmark.png"]["lossy"])
        self.assertEqual(
            Image.open(self.output / records["data/gfx/loading-wordmark.png"]["output"])
            .convert("RGBA")
            .tobytes(),
            Image.open(wordmark).convert("RGBA").tobytes(),
        )


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
        audit = self.export()
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
            self.export()
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
        first = self.export()
        self.assertEqual(first, self.export())
        Image.new("RGBA", (3, 3), (1, 2, 3, 4)).save(self.gfx / "unit1r.png")
        self.frames["unit1r.png"] = bytes((1, 2, 3, 4)) * 9
        self.assertNotEqual(first, self.export())
        self.assertEqual(self.unpack(), self.frames)

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
