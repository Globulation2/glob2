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


def record(key, match_id='fixture'):
    def text(value):
        return len(value).to_bytes(4, 'big') + value.encode()
    return b'G2MR' + (1).to_bytes(2, 'big') + (0).to_bytes(4, 'big') + text(match_id) + text(key) + b'rest'


class CheckSimRevisionTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        for path in ('src/app/Version.h', 'src/game/SimRevision.h') + sim_version.SIM_DATA_FILES:
            (self.root / path).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, self.root / path)
        self.set_revision(5)
        self.write_fixtures('0 aaaa\n')
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

    def write_fixtures(self, trace, key=None):
        (self.root / checker.TRACE).parent.mkdir(parents=True, exist_ok=True)
        (self.root / checker.TRACE).write_text(trace)
        (self.root / checker.RECORD).write_bytes(record(key or sim_version.sim_version_key(self.root)))

    def problems(self, base=True):
        return checker.check(self.root, self.base if base else None)

    def test_record_matching_the_tree_passes(self):
        self.assertEqual(self.problems(), [])
        self.assertEqual(checker.record_sim_version(record('1-2-abc')), '1-2-abc')

    def test_bump_without_a_new_record_fails(self):
        self.set_revision(6)
        problems = self.problems(base=False)
        self.assertEqual(len(problems), 1)
        self.assertIn('regenerate', problems[0])

    def test_trace_change_without_bump_fails(self):
        self.write_fixtures('0 bbbb\n')
        problems = self.problems()
        self.assertEqual(len(problems), 1)
        self.assertIn('bump SIM_REVISION', problems[0])

    def test_trace_change_with_bump_and_new_record_passes(self):
        self.set_revision(6)
        self.write_fixtures('0 bbbb\n')
        self.assertEqual(self.problems(), [])

    def test_bump_alone_with_new_record_passes(self):
        self.set_revision(6)
        self.write_fixtures('0 aaaa\n')
        self.assertEqual(self.problems(), [])

    def test_revision_must_not_go_down(self):
        self.set_revision(4)
        self.write_fixtures('0 aaaa\n')
        problems = self.problems()
        self.assertEqual(len(problems), 1)
        self.assertIn('went down', problems[0])

    def test_revision_feeds_the_key(self):
        before = sim_version.sim_version_key(self.root)
        self.set_revision(6)
        self.assertNotEqual(sim_version.sim_version_key(self.root), before)
        self.assertEqual(sim_version.sim_version_key(self.root).split('-')[:2], before.split('-')[:2])

    def test_catalog_files_are_discovered_and_hashed(self):
        directory = self.root / 'data/buildings'
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


if __name__ == '__main__':
    unittest.main()
