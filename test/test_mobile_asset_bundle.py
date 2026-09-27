"""Regress the on-device startup failure caused by an AAPT-filtered cache file."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mobile'))
from asset_bundle import include_asset, verify_apk_assets


class AssetBundleTest(unittest.TestCase):
    def test_cache_and_metadata_are_not_indexed(self):
        for name in ('data/__pycache__/check_translations.cpython-314.pyc',
                     'maps/.DS_Store', 'data/CVS/Entries', 'scripts/editor~'):
            self.assertFalse(include_asset(name), name)
        for name in ('data/gfx/inn.png', 'maps/tutorial.map', 'scripts/test.txt'):
            self.assertTrue(include_asset(name), name)

    def test_final_package_matches_index(self):
        entries = {'data/a.txt': b'game data', 'maps/test.map': b'map'}
        digest = hashlib.sha256()
        for name, contents in entries.items():
            digest.update(name.encode() + b'\0' + hashlib.sha256(contents).digest())
        index = digest.hexdigest() + '\n' + '\n'.join(entries) + '\n'
        with tempfile.TemporaryDirectory() as directory:
            apk = Path(directory) / 'app.apk'
            for variant in ('valid', 'missing', 'changed'):
                with zipfile.ZipFile(apk, 'w') as package:
                    package.writestr('assets/glob2-bundle/index.list', index)
                    for name, contents in entries.items():
                        if variant == 'missing' and name == 'maps/test.map':
                            continue
                        package.writestr('assets/glob2-bundle/' + name,
                                         b'wrong' if variant == 'changed' else contents)
                if variant == 'valid':
                    verify_apk_assets(apk)
                else:
                    with self.assertRaisesRegex(ValueError, 'missing indexed|identity'):
                        verify_apk_assets(apk)


if __name__ == '__main__':
    unittest.main()
