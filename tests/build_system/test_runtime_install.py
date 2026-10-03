"""Release asset upgrades remove stale managed images and retain user content."""

from pathlib import Path
import tempfile
import unittest
from PIL import Image
from tools.package_assets import export_assets
from scons.runtime_assets import install_export, STAMP


class RuntimeInstallTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.source = root / "source"
        self.image = self.source / "data/gfx/frame0.png"
        self.image.parent.mkdir(parents=True)
        Image.new("RGBA", (64, 64), (20, 80, 130, 255)).save(self.image)
        self.exported = root / "exported"
        self.installed = root / "installed"
        self.audit = export_assets(self.source, self.exported, cache=root / "cache")
        self.relative = self.audit["files"][0]["output"]
        self.assertTrue(self.relative.endswith(".webp"))

    def test_legacy_png_does_not_shadow_webp_after_install(self):
        original = self.installed / "data/gfx/frame0.png"
        original.parent.mkdir(parents=True)
        original.write_bytes(self.image.read_bytes())
        install_export(self.exported, self.installed)
        self.assertFalse(original.exists())
        self.assertTrue((self.installed / self.relative).is_file())
        self.assertTrue((self.installed / STAMP).is_file())

    def test_removed_assets_and_format_changes_remove_only_owned_files(self):
        install_export(self.exported, self.installed)
        custom = self.installed / "maps/custom.map.gz"
        custom.parent.mkdir()
        custom.write_bytes(b"user map")
        export_assets(self.source, self.exported, optimized=False)
        install_export(self.exported, self.installed)
        self.assertFalse((self.installed / self.relative).exists())
        self.assertTrue((self.installed / "data/gfx/frame0.png").is_file())
        self.image.unlink()
        export_assets(self.source, self.exported, optimized=False)
        install_export(self.exported, self.installed)
        self.assertFalse((self.installed / "data/gfx/frame0.png").exists())
        self.assertEqual(custom.read_bytes(), b"user map")

    def test_legacy_png_from_older_release_does_not_hide_updated_artwork(self):
        old = self.installed / "data/gfx/frame0.png"
        old.parent.mkdir(parents=True)
        Image.new("RGBA", (64, 64), (180, 30, 90, 255)).save(old)
        unrelated = old.with_name("custom.png")
        unrelated.write_bytes(old.read_bytes())
        install_export(self.exported, self.installed)
        self.assertFalse(old.exists())
        self.assertTrue((self.installed / self.relative).is_file())
        self.assertTrue(unrelated.is_file())

    def test_frames_packed_into_sheets_leave_the_install(self):
        frame = self.source / "data/gfx/unit0r.png"
        Image.new("RGBA", (4, 4), (9, 8, 7, 255)).save(frame)
        install_export(self.exported, self.installed)
        export_assets(self.source, self.exported, optimized=False)
        install_export(self.exported, self.installed)
        legacy = self.installed / "data/gfx/unit0r.png"
        self.assertTrue(legacy.is_file())
        changed = self.installed / "data/gfx/custom.png"
        changed.write_bytes(b"user artwork")
        export_assets(self.source, self.exported)
        install_export(self.exported, self.installed)
        self.assertFalse(legacy.exists())
        self.assertTrue((self.installed / "data/gfx/unit.sheet").is_file())
        self.assertEqual(changed.read_bytes(), b"user artwork")

    def test_legacy_install_frames_packed_into_sheets_are_removed(self):
        frame = self.source / "data/gfx/unit0r.png"
        Image.new("RGBA", (4, 4), (9, 8, 7, 255)).save(frame)
        export_assets(self.source, self.exported)
        legacy = self.installed / "data/gfx/unit0r.png"
        legacy.parent.mkdir(parents=True)
        Image.new("RGBA", (4, 4), (1, 2, 3, 255)).save(legacy)
        install_export(self.exported, self.installed)
        self.assertFalse(legacy.exists())
        self.assertTrue((self.installed / "data/gfx/unit.sheet").is_file())

    def test_modified_old_asset_is_preserved(self):
        install_export(self.exported, self.installed)
        old = self.installed / self.relative
        old.write_bytes(b"custom override")
        self.image.unlink()
        export_assets(self.source, self.exported, optimized=False)
        install_export(self.exported, self.installed)
        self.assertEqual(old.read_bytes(), b"custom override")


if __name__ == "__main__":
    unittest.main()
