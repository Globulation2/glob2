#!/usr/bin/env python3
"""The compile-profile groups in scons/sources.py partition CLIENT_SOURCES exactly."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scons'))
import sources  # noqa: E402


class SourcesManifestTest(unittest.TestCase):
    def groups(self):
        return {'core': sources.CORE_SOURCES, **sources.OPTIONAL_SOURCES}

    def test_groups_cover_client_sources_exactly(self):
        union = [source for group in self.groups().values() for source in group]
        self.assertEqual(sorted(union), sorted(sources.CLIENT_SOURCES))
        self.assertEqual(len(set(sources.CLIENT_SOURCES)), len(sources.CLIENT_SOURCES))

    def test_groups_are_pairwise_disjoint(self):
        groups = list(self.groups().items())
        for index, (name, group) in enumerate(groups):
            self.assertTrue(group, name + ' is empty')
            for other, members in groups[index + 1:]:
                self.assertFalse(set(group) & set(members), name + ' overlaps ' + other)

    def test_profiles_keep_manifest_order(self):
        self.assertEqual(sources.profile_sources('full'), sources.CLIENT_SOURCES)
        core = sources.profile_sources('core')
        self.assertEqual(core, sources.CORE_SOURCES)
        position = {source: index for index, source in enumerate(sources.CLIENT_SOURCES)}
        self.assertEqual(list(core), sorted(core, key=position.__getitem__))
        with self.assertRaises(ValueError):
            sources.profile_sources('tiny')

    def test_core_keeps_what_the_game_and_golden_match_need(self):
        core = set(sources.CORE_SOURCES)
        for prefix in ('ai/nicowar/', 'ai/warrush/', 'ai/shared_runtime/', 'ai/maxima/',
                       'ai/javascript/', 'ai/simple/AINull', 'map/generator/core/',
                       'map/generator/shared/', 'map/generator/javascript/',
                       'map/generator/compatibility/'):
            self.assertTrue(any(source.startswith(prefix) for source in core), prefix)
        # Game_editor.cpp holds Game's own placement and script queries, not editor UI.
        self.assertIn('game/Game_editor.cpp', core)
        registry = (ROOT / 'src/map/generator/core/GeneratorRegistry.cpp').read_text()
        catalog = registry[registry.index('static const GeneratorRegistry registry({'):]
        catalog = catalog[:catalog.index('});')]
        kept = re.findall(r'(?<!OPTIONAL_GENERATOR\()\b(\w+)Definition\(\)', catalog)
        names = {re.sub(r'(?<!^)([A-Z])', r'-\1', name).lower() for name in kept}
        self.assertEqual(names, set(sources.CORE_GENERATORS))

    def test_core_profile_builds_in_its_own_directory(self):
        from build_layout import build_identity, default_directory
        development = {'dev_fast': '1', 'linker': 'auto'}
        full = build_identity(development, 'linux')
        self.assertNotIn('compile_profile', full)
        self.assertEqual(full, build_identity({**development, 'compile_profile': 'full'}, 'linux'))
        core = build_identity({**development, 'compile_profile': 'core'}, 'linux')
        self.assertEqual(core['compile_profile'], 'core')
        self.assertEqual(default_directory(full), Path('build/linux/client/debug/dev-dev_fast-true-linker-auto'))
        self.assertEqual(default_directory(core),
                         Path('build/linux/client/debug/dev-dev_fast-true-linker-auto-compile_profile-core'))
        for refused in ({'target': 'web'}, {'release': '1'}, {'perf': '1'}, {'role': 'relay'}):
            with self.assertRaises(ValueError):
                build_identity({**refused, 'compile_profile': 'core'}, 'linux')
        with self.assertRaises(ValueError):
            build_identity({'compile_profile': 'tiny'}, 'linux')

    def test_header_names_every_optional_group(self):
        header = sources.profile_header('core')
        self.assertIn('#define GLOB2_COMPILE_PROFILE "core"', header)
        for name in sources.OPTIONAL_SOURCES:
            macro = 'GLOB2_PROFILE_HAS_' + name.replace('ai:', 'AI_').upper()
            self.assertIn('#define %s 0' % macro, header)
            self.assertIn('#define %s 1' % macro, sources.profile_header('full'))


if __name__ == '__main__':
    unittest.main()
