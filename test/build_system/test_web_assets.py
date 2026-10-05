"""Browser data packages: what ships, where it goes, and how it is installed."""
import ast
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
        cls.packages, cls.skipped, cls.substitutes = web_assets.plan(ROOT)
        # The first package a path is in (the font is in core and font-cjk).
        cls.owner = {}
        for name, paths in cls.packages.items():
            for path in paths:
                cls.owner.setdefault(path, name)

    def test_skin_meshes_are_only_in_the_on_demand_package(self):
        meshes = list((ROOT / 'data/skins/colony-v1').glob('*.gsk'))
        manifest = json.loads((ROOT / 'data/skins/colony-v1/manifest.json').read_text())
        self.assertEqual(sorted(p.name for p in meshes), sorted(manifest['meshes']))
        for path in meshes:
            self.assertEqual(self.owner[path.relative_to(ROOT).as_posix()], 'skins')
        self.assertNotIn('data/skins/colony-v1/manifest.json', self.packages['core'])

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
                     'data/texts.keys.txt', 'data/authors.txt', 'data/gfx/menu-wordmark.png',
                     'data/gfx/menu-colony.png', 'data/gfx/loading-wordmark.png', 'data/gfx/guitheme0.png',
                     'data/gfx/rotatingEarth0.png', 'data/menu/colony.bin',
                     'data/gui/editor0.png', 'maps/FourSquares1.map.gz'):
            self.assertEqual(self.owner.get(path), 'core', path)

    def test_in_game_sprites_follow_the_main_menu(self):
        # GlobalContainer::loadGameGraphics, including every building's artwork.
        for path in ('data/gfx/unit0r.png', 'data/gfx/unit1000.png', 'data/gfx/terrain0.png', 'data/gfx/terrain333.png', 'data/gfx/water0.png', 'data/gfx/gamegui0.png',
                     'data/gfx/ressource0.png', 'data/gfx/particle0.png', 'data/gfx/swarm0b0.png',
                     'data/gfx/inn0b0r.png', 'data/gfx/racetrack2b0.png', 'data/gfx/minibuildingsite5.png',
                     'data/gfx/explorationflag0r.png', 'data/gfx/wallc0.png'):
            self.assertEqual(self.owner.get(path), 'game', path)
        # The browser never draws the game cursor (it always shows the system one).
        self.assertEqual(self.owner['data/gfx/cursor/normal0r.png'], 'game')
        # Nothing else ends up there: every game file is a frame of a game sprite.
        self.assertTrue(all(p.startswith('data/gfx/') and p.endswith('.png') for p in self.packages['game']))
        self.assertFalse(web_assets.game_files(['data/gfx/unitmini0.png'], {'unit'}))
        self.assertEqual(web_assets.game_files(['data/terrain/compiled/atlas.json', 'data/terrain/compiled/textures-0-mip0.webp'],
                                               {'data/terrain/compiled/'}),
                         {'data/terrain/compiled/atlas.json', 'data/terrain/compiled/textures-0-mip0.webp'})
        self.assertTrue(web_assets.game_files(['data/gfx/inn0b12r.png'], {'inn0b'}))
        # The runtime export's sheets (tools/package_assets.py) replace a sprite's frames.
        self.assertEqual(web_assets.game_files(['data/gfx/unit.sheet', 'data/gfx/unit-sheet-3.webp',
                                                'data/gfx/unitmini.sheet', 'data/gfx/unitmini-sheet-0.png'], {'unit'}),
                         {'data/gfx/unit.sheet', 'data/gfx/unit-sheet-3.webp'})

    def test_terrain_registry_atlases_and_backdrops_are_game_sprites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'src/app').mkdir(parents=True)
            (root / 'src/map').mkdir(parents=True)
            (root / 'src/app/GlobalContainer.cpp').write_text(
                'void GlobalContainer::loadGameGraphics() {\n'
                'sprite("data/gfx/unit"); sprite("data/gfx/terrain");\n'
                'sprite("data/gfx/gamegui"); sprite("data/gfx/swarm0b");\n}\n')
            (root / 'src/map/TerrainPresentation.h').write_text(
                'constexpr auto atlas = "data/gfx/future-terrain";\n'
                'constexpr auto backdrop = "data/gfx/future-backdrop";\n')
            (root / 'data/terrain').mkdir(parents=True)
            (root / 'data/terrain/tileset.json').write_text(json.dumps({'materials': [
                {'sprite': 'data/materials/rock', 'backdrop': {'sprite': 'data/gfx/glow'}}]}))
            names = web_assets.game_sprites(root)
            self.assertEqual(names, {'unit', 'terrain', 'gamegui', 'swarm0b',
                                     'future-terrain', 'future-backdrop', 'data/materials/rock', 'glow'})
            self.assertEqual(web_assets.game_files(
                ['data/gfx/future-terrain0.png', 'data/gfx/future-backdrop.sheet', 'data/materials/rock12.png'], names),
                {'data/gfx/future-terrain0.png', 'data/gfx/future-backdrop.sheet', 'data/materials/rock12.png'})

    def test_building_definition_artwork_including_experiments_is_packaged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'src/app').mkdir(parents=True)
            (root / 'src/map').mkdir(parents=True)
            (root / 'src/app/GlobalContainer.cpp').write_text(
                'void GlobalContainer::loadGameGraphics() {\n'
                'sprite("data/gfx/unit"); sprite("data/gfx/terrain");\n'
                'sprite("data/gfx/gamegui"); sprite("data/gfx/swarm0b");\n}\n')
            (root / 'src/map/TerrainPresentation.h').write_text('')
            definitions = root / 'data/buildings/experimental'
            definitions.mkdir(parents=True)
            (definitions / 'manifest.json').write_text('{"files":["refuge.json"]}')
            (definitions / 'refuge.json').write_text(json.dumps({'variants': [{
                'requiredExperiment': 'refuge', 'properties': {
                    'gameSprite': 'data/custom/refuge', 'miniSprite': 'data/gfx/refuge-icon'}}]}))
            names = web_assets.game_sprites(root)
            self.assertIn('data/custom/refuge', names)
            self.assertIn('refuge-icon', names)
            self.assertEqual(web_assets.game_files(
                ['data/custom/refuge0.png', 'data/gfx/refuge-icon3r.png', 'data/custom/unrelated0.png'], names),
                {'data/custom/refuge0.png', 'data/gfx/refuge-icon3r.png'})

    def test_terrain_registry_changes_invalidate_browser_asset_plan(self):
        tree = ast.parse((ROOT / 'scons/web_build.py').read_text())
        inputs = [node.value for node in ast.walk(tree) if isinstance(node, ast.Assign)
                  and any(isinstance(target, ast.Name) and target.id == 'plan_inputs'
                          for target in node.targets)]
        self.assertEqual(len(inputs), 1)
        self.assertIn('data/terrain/tileset.json', ast.literal_eval(inputs[0]))
        self.assertEqual(self.owner['data/terrain/tileset.json'], 'core')

    def test_core_ships_the_browser_copies_and_font_cjk_the_full_font(self):
        self.assertEqual(sorted(self.substitutes), ['data/fonts/sans.ttf', 'data/gfx/menu-colony.png',
                                                    'data/gfx/menu-wordmark.png'],
                         'browser/assets is stale: run python3 browser/derive_assets.py')
        self.assertEqual(self.packages['font-cjk'], ['data/fonts/sans.ttf'])
        self.assertIn('data/fonts/sans.ttf', self.packages['core'])

    def test_core_has_english_and_every_language_name(self):
        self.assertEqual(self.owner['data/texts.en.txt'], 'core')
        self.assertIn('data/texts.ja.txt', self.packages['translations'])
        self.assertNotIn('data/texts.en.txt', self.packages['translations'])
        stub = web_assets.contents(ROOT, 'core', 'data/texts.ja.txt', {}).decode()
        lines = stub.split('\n')
        pairs = dict(zip(lines[0::2], lines[1::2]))
        self.assertEqual(pairs['[language-code]'], 'ja')
        self.assertEqual(pairs['[language]'], '日本語')
        self.assertEqual(set(pairs) - {''}, set(web_assets.STUB_KEYS))
        full = web_assets.contents(ROOT, 'translations', 'data/texts.ja.txt', {})
        self.assertEqual(full, (ROOT / 'data/texts.ja.txt').read_bytes())

    def test_music_and_artwork_the_game_reloads_per_match_are_optional(self):
        self.assertEqual(self.owner['data/zik/intro.opus'], 'menu-music')
        self.assertEqual(self.owner['data/zik/menu.opus'], 'menu-music')
        self.assertEqual(self.owner['data/zik/original/a1.opus'], 'music')
        self.assertEqual(self.owner['data/zik/woodland/a1.opus'], 'music-sets')
        self.assertEqual(self.owner['data/highres/v1/frames.txt'], 'hd')
        self.assertTrue(all(p.startswith('data/highres/') for p in self.packages['hd']))

    def test_every_installed_music_set_is_owned_and_opus_only(self):
        sets = [p for p in (ROOT / 'data/zik').iterdir() if p.is_dir()]
        self.assertEqual(len(sets), 10)
        for directory in sets:
            package = 'music' if directory.name == 'original' else 'music-sets'
            for slot in range(1, 4):
                path = f'data/zik/{directory.name}/a{slot}.opus'
                self.assertEqual(self.owner[path], package)
            self.assertFalse(list(directory.glob('*.ogg')))
            if directory.name != 'original':
                self.assertEqual(self.owner[f'data/zik/{directory.name}/LICENSE.txt'], package)

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
                self.assertEqual(entry['optional'], entry['name'] not in ('core', 'game'))
                self.assertEqual(entry['size'], sum(part['size'] for part in entry['parts']))
                for part in entry['parts']:
                    blob = (output / part['url']).read_bytes()
                    self.assertEqual(len(blob), part['size'])
                    if entry['name'] != 'core' and len(part['files']) > 1:
                        self.assertLessEqual(part['size'], web_assets.PART_BYTES + 2_000_000)
                    for path, start, end in part['files'][:50]:
                        self.assertEqual(blob[start:end], web_assets.contents(ROOT, entry['name'], path.lstrip('/'),
                                                                              web_assets.derived_assets(ROOT)), path)
            script = (output / 'asset-manifest.js').read_text()
            self.assertIn('Module["glob2AssetManifest"] ??= ', script)
            # A rebuild without changes writes the same names.
            again = web_assets.build(ROOT, output, output / 'asset-manifest.js')
            self.assertEqual(again, manifest)


class BrowserCopyTests(unittest.TestCase):
    def test_a_stale_copy_falls_back_to_the_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            texts = [p.relative_to(ROOT).as_posix() for p in (ROOT / 'data').glob('texts.*.txt')]
            for path in texts + ['browser/derive_assets.py', 'tools/package_assets.py', 'browser/assets/sources.json',
                         'data/gfx/menu-colony.png', 'data/gfx/menu-wordmark.png', 'data/fonts/sans.ttf',
                         'browser/assets/sans-core.ttf', 'browser/assets/menu-colony.webp', 'browser/assets/menu-wordmark.webp']:
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_bytes((ROOT / path).read_bytes())
            self.assertEqual(len(web_assets.derived_assets(root)), 3)
            (root / 'data/gfx/menu-colony.png').write_bytes(b'changed')
            self.assertEqual(sorted(web_assets.derived_assets(root)), ['data/fonts/sans.ttf', 'data/gfx/menu-wordmark.png'])

    def test_a_copy_stands_in_for_a_re_encoded_export_when_smaller(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / 'data/gfx').mkdir(parents=True)
            (source / 'data/gfx/menu-wordmark.webp').write_bytes(b'x' * 2_000_000)
            (source / 'data/gfx/menu-colony.webp').write_bytes(b'x' * 1000)
            files = ['data/gfx/menu-wordmark.webp', 'data/gfx/menu-colony.webp', 'data/fonts/sans.ttf']
            derived = web_assets.derived_assets(ROOT)
            self.assertEqual(web_assets.exported_substitutes(ROOT, source, files, derived),
                             {'data/gfx/menu-wordmark.webp': 'browser/assets/menu-wordmark.webp',
                              'data/fonts/sans.ttf': 'browser/assets/sans-core.ttf'})

    def test_png_derivative_is_rejected_and_original_profile_keeps_font(self):
        from unittest.mock import patch
        current = json.loads((ROOT / 'browser/assets/sources.json').read_text())
        entry = current.pop('browser/assets/menu-colony.webp')
        current['browser/assets/menu-colony.png'] = entry
        exists = Path.is_file
        def selected_file(path):
            return str(path).endswith('browser/assets/menu-colony.png') or exists(path)
        derive = web_assets.load_module(ROOT, 'selected_derive', 'browser/derive_assets.py')
        digest = derive.digest
        def selected_digest(path):
            return entry['output_sha256'] if str(path).endswith('browser/assets/menu-colony.png') else digest(path)
        with patch.object(web_assets.json, 'loads', return_value=current), \
                patch.object(Path, 'is_file', selected_file), \
                patch.object(derive, 'digest', selected_digest), \
                patch.object(web_assets, 'load_module', return_value=derive):
            self.assertNotIn('data/gfx/menu-colony.png', web_assets.derived_assets(ROOT))
        self.assertEqual(list(web_assets.derived_assets(ROOT, images=False)), ['data/fonts/sans.ttf'])

    def test_q85_or_corrupt_derivatives_are_rejected(self):
        import json
        current = json.loads((ROOT / 'browser/assets/sources.json').read_text())
        stale = json.loads(json.dumps(current))
        stale['browser/assets/menu-colony.webp']['image_recipe']['lossy_quality'] = 85
        from unittest.mock import patch
        with patch.object(web_assets.json, 'loads', return_value=stale):
            self.assertNotIn('data/gfx/menu-colony.png', web_assets.derived_assets(ROOT))
        self.assertNotIn('data/gfx/menu-colony.png', web_assets.derived_assets(ROOT, lossy=False))
        stale = json.loads(json.dumps(current))
        stale['browser/assets/menu-colony.webp']['output_sha256'] = 'bad'
        with patch.object(web_assets.json, 'loads', return_value=stale):
            self.assertNotIn('data/gfx/menu-colony.png', web_assets.derived_assets(ROOT))

    def test_the_core_font_has_every_glyph_but_the_appended_cjk_ones(self):
        try:
            from fontTools.pens.recordingPen import DecomposingRecordingPen
            from fontTools.ttLib import TTFont
        except ImportError:
            self.skipTest('fontTools is not installed')
        full = TTFont(ROOT / 'data/fonts/sans.ttf', lazy=True)
        core = TTFont(ROOT / 'browser/assets/sans-core.ttf', lazy=True)
        full_map, core_map = full.getBestCmap(), core.getBestCmap()
        original = {cp for cp, name in full_map.items() if not name.startswith('cjk')}
        self.assertLessEqual(original, set(core_map))
        # Outlines and advances of the original glyphs are unchanged.
        full_glyphs, core_glyphs = full.getGlyphSet(), core.getGlyphSet()
        for cp in sorted(original)[::7]:
            name, kept = full_map[cp], core_map[cp]
            self.assertEqual(full['hmtx'][name], core['hmtx'][kept], hex(cp))
            outlines = []
            for glyphs, glyph in ((full_glyphs, name), (core_glyphs, kept)):
                pen = DecomposingRecordingPen(glyphs)
                glyphs[glyph].draw(pen)
                outlines.append(pen.value)
            self.assertEqual(outlines[0], outlines[1], hex(cp))
        for table in ('GSUB', 'GPOS', 'kern', 'hhea', 'OS/2', 'fpgm', 'prep', 'cvt '):
            self.assertEqual(table in full, table in core, table)


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
                for file in ('index.html', 'studio.html', 'index.js', 'index.wasm', 'loader.js', 'threaded/index.js', 'threaded/index.wasm'):
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
            self.assertEqual((served / 'studio.html').read_text(), 'threestudio.html')
            self.assertEqual((served / 'studio.html.br').read_text(), 'threestudio.html.br')
            self.assertEqual(sorted(p.name for p in (served / 'assets').iterdir()),
                             ['core.2222222222222222.data', 'core.3333333333333333.data'])
            self.assertEqual(json.loads((served / '.installed-assets.json').read_text()), ['core.3333333333333333.data'])


if __name__ == '__main__':
    unittest.main()
