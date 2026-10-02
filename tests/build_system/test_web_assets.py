"""Browser data packages: what ships, where it goes, and how it is installed."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
import web_assets


class WebAssetPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packages, cls.skipped = web_assets.plan(ROOT)
        cls.owner = {path: name for name, paths in cls.packages.items() for path in paths}

    def test_simulation_data_starts_with_the_game(self):
        # The sim version key and the WebAssembly checksum traces read these files.
        for path in web_assets.sim_data_files(ROOT):
            self.assertEqual(self.owner.get(path), 'core', path)

    def test_build_files_and_documentation_are_not_shipped(self):
        for path in ('data/SConscript', 'data/check_translations.py', 'data/fonts/build_chinese_font.py',
                     'data/fonts/README.md', 'campaigns/SConscript', 'data/screenshots/globulation2-gameplay.png'):
            self.assertIn(path, self.skipped)
        self.assertFalse([p for p in self.owner if p.endswith(('SConscript', '.py', '.md'))])

    def test_runtime_files_the_menus_need_are_core(self):
        for path in ('data/fonts/sans.ttf', 'data/fonts/LICENSE-DejaVu.txt', 'data/texts.list.txt',
                     'data/texts.keys.txt', 'data/authors.txt', 'data/zik/menu.ogg', 'data/gfx/menu-wordmark.png',
                     'data/gfx/loading-wordmark.png', 'maps/FourSquares1.map.gz'):
            self.assertEqual(self.owner.get(path), 'core', path)

    def test_music_and_artwork_the_game_reloads_per_match_are_optional(self):
        self.assertEqual(self.owner['data/zik/original/a1.ogg'], 'music')
        self.assertEqual(self.owner['data/highres/v1/frames.txt'], 'hd')
        self.assertTrue(all(p.startswith('data/highres/') for p in self.packages['hd']))

    def test_parts_respect_the_size_limit(self):
        sizes = {'a': 3, 'b': 3, 'c': 7, 'd': 1}
        self.assertEqual(web_assets.split(['a', 'b', 'c', 'd'], sizes, 6), [['a', 'b'], ['c'], ['d']])


class WebAssetPackageTests(unittest.TestCase):
    def test_packages_round_trip_and_are_content_addressed(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            stale = output / 'assets/core.0000000000000000.data'
            stale.parent.mkdir()
            stale.write_bytes(b'old')
            stale.with_name(stale.name + '.br').write_bytes(b'old')
            manifest = web_assets.build(ROOT, output, output / 'asset-manifest.js')
            self.assertFalse(stale.exists())
            self.assertEqual(sorted(p.name for p in (output / 'assets').iterdir()),
                             sorted(part['url'].split('/')[1] for entry in manifest['packages'] for part in entry['parts']))
            hd = next(entry for entry in manifest['packages'] if entry['name'] == 'hd')
            self.assertGreater(len(hd['parts']), 1)
            for entry in manifest['packages']:
                self.assertEqual(entry['size'], sum(part['size'] for part in entry['parts']))
                for part in entry['parts']:
                    blob = (output / part['url']).read_bytes()
                    self.assertEqual(len(blob), part['size'])
                    if entry['optional']:
                        self.assertLessEqual(part['size'], web_assets.PART_BYTES + 2_000_000)
                    for path, start, end in part['files'][:50]:
                        self.assertEqual(blob[start:end], (ROOT / path.lstrip('/')).read_bytes(), path)
            script = (output / 'asset-manifest.js').read_text()
            self.assertIn('Module["glob2AssetManifest"] ??= ', script)
            # A rebuild without changes writes the same names.
            again = web_assets.build(ROOT, output, output / 'asset-manifest.js')
            self.assertEqual(again, manifest)


class InstallWebClientTests(unittest.TestCase):
    def test_installs_entry_files_last_and_keeps_one_previous_release(self):
        script = ROOT / 'deploy/install-web-client.py'
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            served = base / 'served'
            served.mkdir()
            (served / 'index.data').write_bytes(b'legacy')
            (served / 'index.js.br').write_bytes(b'stale')

            def release(name, packages, compressed=True):
                path = base / name
                (path / 'assets').mkdir(parents=True)
                (path / 'threaded').mkdir()
                for file in ('index.html', 'index.js', 'index.wasm', 'loader.js', 'threaded/index.js', 'threaded/index.wasm'):
                    (path / file).write_text(name + file)
                    if compressed and file != 'index.html':
                        (path / (file + '.br')).write_text(name + file + '.br')
                for package in packages:
                    (path / 'assets' / package).write_text(name + package)
                return path

            for name, packages, compressed in (('one', ['core.1111111111111111.data'], False),
                                               ('two', ['core.2222222222222222.data'], True),
                                               ('three', ['core.3333333333333333.data'], True)):
                subprocess.run([sys.executable, str(script), str(release(name, packages, compressed)), str(served)],
                               check=True, capture_output=True)
                if name == 'one':
                    # A release without a precompressed copy removes the stale one.
                    self.assertFalse((served / 'index.js.br').exists())
                    self.assertFalse((served / 'index.data').exists())
            self.assertEqual((served / 'index.js.br').read_text(), 'threeindex.js.br')
            self.assertEqual((served / 'index.html').read_text(), 'threeindex.html')
            self.assertEqual(sorted(p.name for p in (served / 'assets').iterdir()),
                             ['core.2222222222222222.data', 'core.3333333333333333.data'])
            self.assertEqual(json.loads((served / '.installed-assets.json').read_text()), ['core.3333333333333333.data'])


if __name__ == '__main__':
    unittest.main()
