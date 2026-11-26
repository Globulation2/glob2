"""Browser editions (buildsys/edition.py): the standard build is untouched, every
switch is independent, and an edition's export plan follows its switches."""
from pathlib import Path
import json
import sys
import unittest
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
sys.path.insert(0, str(ROOT))
import edition  # noqa: E402
import sources  # noqa: E402
import web_assets  # noqa: E402
from build_layout import build_identity, default_directory  # noqa: E402

WEB = dict(target='web', release='1')


class EditionTests(unittest.TestCase):
    def test_standard_records_nothing(self):
        identity = build_identity(WEB)
        self.assertNotIn('edition', identity)
        self.assertEqual(edition.header(identity), '')
        self.assertEqual(default_directory(identity).as_posix(), 'build/emscripten/client/release')
        self.assertEqual(edition.settings(identity)['features'], {name: True for name in edition.FEATURES})

    def test_lite_preset_and_directory(self):
        identity = build_identity(dict(WEB, edition='lite'))
        self.assertEqual(default_directory(identity).as_posix(), 'build/emscripten/client/release/lite')
        settings = edition.settings(identity)
        self.assertFalse(any(settings['features'].values()))
        self.assertEqual(settings['webp_quality'], 70)
        self.assertEqual(settings['web_opt'], 'O3')
        self.assertIn('#define GLOB2_HAS_EDITOR 0', edition.header(identity))
        # Ruled-out client capabilities follow the switches.
        self.assertFalse(any(identity['client_features'].values()))

    def test_every_switch_overrides_alone(self):
        for feature in edition.FEATURES:
            with self.subTest(feature=feature):
                lite = build_identity(dict(WEB, edition='lite', **{'feature_' + feature: '1'}))
                self.assertTrue(edition.enabled(lite, feature))
                self.assertEqual(default_directory(lite).name, 'lite-with_' + feature)
                standard = build_identity(dict(WEB, **{'feature_' + feature: '0'}))
                self.assertFalse(edition.enabled(standard, feature))
                self.assertEqual(default_directory(standard).name, 'edition-no_' + feature)
        for key, (standard, lite, kind) in edition.SETTINGS.items():
            with self.subTest(setting=key):
                value = {'bool': '1' if not lite else '0'}.get(kind) if kind == 'bool' else \
                    str(standard) if lite != standard else None
                if value is None:
                    continue
                identity = build_identity(dict(WEB, edition='lite', **{key: value}))
                self.assertTrue(default_directory(identity).name.startswith('lite-' + key + '_'))

    def test_invalid_values_are_rejected(self):
        for arguments in (dict(edition='huge'), dict(webp_quality='0'), dict(web_opt='O1'),
                          dict(feature_editor='maybe'), dict(variant_blocks='5')):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                build_identity(dict(WEB, **arguments))

    def test_native_takes_compile_switches_only(self):
        identity = build_identity(dict(target='native', edition='lite'))
        self.assertEqual(default_directory(identity).name, 'lite')
        for feature in edition.WEB_FEATURES:
            self.assertTrue(edition.enabled(identity, feature))
        self.assertFalse(edition.enabled(identity, 'editor'))
        with self.assertRaises(ValueError):
            build_identity(dict(target='native', edition='lite', webp_quality='50'))

    def test_feature_sources_exist_and_belong_to_known_features(self):
        paths = ['src/' + name for name in sources.CLIENT_SOURCES] + \
            ['libs/gag/src/' + name for name in sources.GAG_SOURCES] + list(sources.WEB_SOURCES)
        for name, group in sources.FEATURE_SOURCES.items():
            with self.subTest(group=name):
                self.assertTrue(set(name.split('+')) <= set(edition.FEATURES), name)
                for prefix in group:
                    self.assertTrue(any(path.startswith(prefix) for path in paths), prefix)
        # A shared group stays while any of its features does.
        self.assertNotIn('src/online/MapCache.cpp', sources.feature_sources(['lan'], paths))
        self.assertIn('src/online/MapCache.cpp', sources.feature_sources([], paths))

    def test_lite_export_plan(self):
        identity = build_identity(dict(WEB, edition='lite'))
        exclude, drop = web_assets.edition_export(ROOT, identity)
        self.assertIn('data/highres/', exclude)
        self.assertIn('maps/', exclude)
        self.assertIn('data/gfx/cursor/', exclude)
        self.assertNotIn('data/zik/original/', exclude)
        self.assertIn('data/texts.fr.txt', exclude)
        self.assertNotIn('data/texts.en.txt', exclude)
        self.assertTrue({'translations', 'font-cjk', 'hd'} <= set(drop))
        overrides = web_assets.edition_overrides(ROOT, identity)
        self.assertEqual(overrides['data/texts.list.txt'], b'data/texts.keys.txt\ndata/texts.en.txt\n')
        self.assertIn('data/zik/original/a1.opus', overrides)
        self.assertEqual(web_assets.edition_export(ROOT, build_identity(WEB)), ((), ()))
        self.assertEqual(web_assets.edition_overrides(ROOT, build_identity(WEB)), {})

    def test_small_build_terrain_is_valid_and_consistent(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        import terrain_catalog
        import terrain_tileset
        from tools.package_assets import small_build_terrain
        content = (ROOT / 'content').resolve()
        document = terrain_catalog.load(content)
        rewritten, dropped, renames = small_build_terrain(
            document, content, fold=True, single_colour_decor=True, variant_blocks=1,
            packed={'data/gfx/terrain-decor'})
        terrain_tileset.validate(document)
        index = json.loads(rewritten['data/terrain/tileset.json'])
        kept = set(index['materials'])
        self.assertFalse(any('small_build_fallback' in m for m in document['materials']))
        self.assertTrue(set(index['bindings'].values()) <= kept)
        self.assertEqual(index['bindings']['grass'], 'lush_grass')
        # Renumbered decor frames are consecutive and every kept list names one.
        numbers = sorted({int(path.rsplit('decor', 1)[1].rstrip('r.png') or 0)
                          for path in renames.values() if not path.endswith('r.png')})
        self.assertEqual(numbers, list(range(len(numbers))))
        for key in kept:
            material = json.loads(rewritten.get(f'data/terrain/materials/{key}.json') or
                                  (content / f'data/terrain/materials/{key}.json').read_bytes())
            for frames in material.get('decor', {}).values():
                if isinstance(frames, list):
                    self.assertTrue(set(frames) <= set(numbers), key)
            self.assertNotIn(f'data/terrain/materials/{key}.json', dropped)

    def test_music_copies_match_their_sources(self):
        import hashlib
        directory = ROOT / web_assets.MUSIC_COPIES / f"{edition.PRESETS['lite']['music_bitrate']}k"
        recorded = json.loads((directory / 'sources.json').read_text())
        self.assertEqual({entry['source'] for entry in recorded.values()},
                         {'content/data/zik/original/a1.opus', 'content/data/zik/original/a2.opus',
                          'content/data/zik/original/a3.opus', 'content/data/zik/intro.opus',
                          'content/data/zik/menu.opus'})
        for name, entry in recorded.items():
            self.assertEqual(hashlib.sha256((ROOT / entry['source']).read_bytes()).hexdigest(), entry['sha256'],
                             f'{entry["source"]} changed: run platforms/web/derive_music.py')
            self.assertEqual(hashlib.sha256((directory / name).read_bytes()).hexdigest(), entry['output_sha256'])


if __name__ == '__main__':
    unittest.main()
