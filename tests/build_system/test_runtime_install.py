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
        self.source = root / 'source'
        self.image = self.source / 'data/gfx/frame0.png'
        self.image.parent.mkdir(parents=True)
        Image.new('RGBA', (64, 64), (20, 80, 130, 255)).save(self.image)
        self.exported = root / 'exported'
        self.installed = root / 'installed'
        self.audit = export_assets(self.source, self.exported, cache=root/'cache')
        self.relative = self.audit['files'][0]['output']
        self.assertTrue(self.relative.endswith('.webp'))

    def test_legacy_png_does_not_shadow_webp_after_install(self):
        original = self.installed / 'data/gfx/frame0.png'
        original.parent.mkdir(parents=True)
        original.write_bytes(self.image.read_bytes())
        install_export(self.exported, self.installed)
        self.assertFalse(original.exists())
        self.assertTrue((self.installed / self.relative).is_file())
        self.assertTrue((self.installed / STAMP).is_file())

    def test_removed_assets_and_format_changes_remove_only_owned_files(self):
        install_export(self.exported, self.installed)
        custom = self.installed / 'maps/custom.map.gz'
        custom.parent.mkdir()
        custom.write_bytes(b'user map')
        export_assets(self.source, self.exported, optimized=False)
        install_export(self.exported, self.installed)
        self.assertFalse((self.installed / self.relative).exists())
        self.assertTrue((self.installed / 'data/gfx/frame0.png').is_file())
        self.image.unlink()
        export_assets(self.source, self.exported, optimized=False)
        install_export(self.exported, self.installed)
        self.assertFalse((self.installed / 'data/gfx/frame0.png').exists())
        self.assertEqual(custom.read_bytes(), b'user map')

    def test_modified_old_asset_is_preserved(self):
        install_export(self.exported, self.installed)
        old = self.installed / self.relative
        old.write_bytes(b'custom override')
        self.image.unlink()
        export_assets(self.source, self.exported, optimized=False)
        install_export(self.exported, self.installed)
        self.assertEqual(old.read_bytes(), b'custom override')


if __name__ == '__main__':
    unittest.main()
