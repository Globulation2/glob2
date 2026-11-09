#!/usr/bin/env python3
"""Tests for test/check_sim_revision.py on throwaway git repositories."""
import importlib.util
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_sim_revision', ROOT / 'test/check_sim_revision.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
sim_version = checker.load_sim_version()


class CheckSimRevisionTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        for path in ('src/app/Version.h', 'src/game/SimRevision.h') + tuple(
                'content/' + name for name in sim_version.SIM_DATA_FILES):
            (self.root / path).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, self.root / path)
        self.set_revision(5)
        self.git('init', '-q')
        self.git('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '--allow-empty', '-m', 'empty')
        self.commit('base')
        self.base = self.git('rev-parse', 'HEAD').stdout.decode().strip()

    def tearDown(self):
        self.scratch.cleanup()

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.root), *args], check=True, capture_output=True)

    def commit(self, message):
        self.git('add', '-A')
        self.git('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-m', message)

    def set_revision(self, revision):
        (self.root / 'src/game/SimRevision.h').write_text(f'// test\n#define SIM_REVISION {revision}\n')

    def problems(self, base=True):
        return checker.check(self.root, self.base if base else None)

    def key_at(self, revision):
        return checker.key_at(self.root, revision, checker.load_sim_version_at(self.root, revision, sim_version))

    def head(self):
        return self.git('rev-parse', 'HEAD').stdout.decode().strip()

    def test_an_integer_revision_passes(self):
        self.assertEqual(self.problems(base=False), [])
        self.assertEqual(self.problems(), [])

    def test_a_bump_passes_and_moves_the_key(self):
        self.set_revision(6)
        self.commit('bump')
        self.assertEqual(self.problems(), [])
        self.assertNotEqual(self.key_at(self.head()), self.key_at(self.base))

    def test_revision_must_not_go_down(self):
        self.set_revision(4)
        problems = self.problems()
        self.assertEqual(len(problems), 1)
        self.assertIn('went down', problems[0])

    def test_a_malformed_revision_fails(self):
        (self.root / 'src/game/SimRevision.h').write_text('#define SIM_REVISION five\n')
        problems = self.problems(base=False)
        self.assertEqual(len(problems), 1)
        self.assertIn('cannot read SIM_REVISION', problems[0])

    def test_key_at_a_revision_uses_its_own_files(self):
        self.assertEqual(self.key_at(self.base), sim_version.sim_version_key(self.root))
        self.set_revision(6)
        self.commit('bump')
        self.assertEqual(self.key_at(self.head()), sim_version.sim_version_key(self.root))

    def test_a_new_hashed_file_moves_the_key_without_a_bump(self):
        # The base hashed one file fewer; its own deploy/sim_version.py computes its key.
        source = (ROOT / 'deploy/sim_version.py').read_text()
        self.assertIn("    'data/texts.list.txt',\n", source)
        (self.root / 'deploy').mkdir()
        (self.root / 'deploy/sim_version.py').write_text(source.replace("    'data/texts.list.txt',\n", ''))
        self.commit('base without the language list')
        before = self.head()
        (self.root / 'deploy/sim_version.py').write_text(source)
        self.commit('hash the language list')
        self.assertNotEqual(self.key_at(self.head()), self.key_at(before))

    def test_revision_feeds_the_key(self):
        before = sim_version.sim_version_key(self.root)
        self.set_revision(6)
        self.assertNotEqual(sim_version.sim_version_key(self.root), before)
        self.assertEqual(sim_version.sim_version_key(self.root).split('-')[:2], before.split('-')[:2])

    def test_catalog_files_are_discovered_and_hashed(self):
        directory = self.root / 'content/data/buildings'
        directory.mkdir(parents=True)
        manifest = directory / 'manifest.json'
        manifest.write_text(json.dumps({'files': ['second.json', 'first.json']}))
        (directory / 'first.json').write_text('{"variants":[]}')
        (directory / 'second.json').write_text('{"variants":[]}')
        files = sim_version.sim_data_files(self.root)
        self.assertEqual(files, tuple(sorted(files)))
        self.assertIn('data/buildings/first.json', files)
        before = sim_version.data_hash(self.root)
        (directory / 'first.json').write_text('{"variants":[1]}')
        self.assertNotEqual(sim_version.data_hash(self.root), before)
        self.commit('catalog')
        head = self.git('rev-parse', 'HEAD').stdout.decode().strip()
        self.assertEqual(checker.key_at(self.root, head, sim_version), sim_version.sim_version_key(self.root))
        manifest.write_text(json.dumps({'files': ['../outside.json']}))
        with self.assertRaises(ValueError):
            sim_version.sim_data_files(self.root)

    def test_revisions_from_before_the_content_move_keep_their_key(self):
        key = sim_version.sim_version_key(self.root)
        self.git('mv', 'content/data', 'data')
        self.commit('old layout')
        old = self.git('rev-parse', 'HEAD').stdout.decode().strip()
        self.assertEqual(sim_version.sim_version_key(self.root), key)
        self.assertEqual(checker.key_at(self.root, old, sim_version), key)

    def test_unit_catalog_is_hashed_and_historical_keys_copy_its_bytes(self):
        directory = self.root / 'content/data/buildings'
        directory.mkdir(parents=True)
        (directory / 'manifest.json').write_text(json.dumps({'files': []}))
        legacy = sim_version.sim_version_key(self.root)
        self.commit('pre-unit-catalog')
        before = self.git('rev-parse', 'HEAD').stdout.decode().strip()
        self.assertEqual(checker.key_at(self.root, before, sim_version), legacy)
        units = self.root / 'content/data/units/registry.json'
        units.parent.mkdir(parents=True)
        units.write_text('{"schemaVersion":1,"units":[]}')
        self.assertIn('data/units/registry.json', sim_version.sim_data_files(self.root))
        key = sim_version.sim_version_key(self.root)
        self.assertNotEqual(key, legacy)
        self.commit('unit-catalog')
        after = self.git('rev-parse', 'HEAD').stdout.decode().strip()
        self.assertEqual(checker.key_at(self.root, after, sim_version), key)
        self.assertEqual(checker.key_at(self.root, before, sim_version), legacy)
        units.write_text('{"schemaVersion":1,"units":[{"key":"worker"}]}')
        self.assertNotEqual(sim_version.sim_version_key(self.root), key)


if __name__ == '__main__':
    unittest.main()
