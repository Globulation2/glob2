"""Unapproved generator observations retain strict coverage and comparison rules."""
import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
from unittest.mock import patch
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'collect_generator_evidence.py'
spec = importlib.util.spec_from_file_location('generator_evidence', SCRIPT)
evidence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evidence)


class GeneratorEvidenceTest(unittest.TestCase):
    def test_rows_ignore_logs_but_reject_missing_malformed_or_duplicate_output(self):
        valid = 'macos-arm64 1 2 8 8 4 1 ok 123\n'
        self.assertEqual(len(evidence.rows('loading data\n' + valid, 'macos-arm64')), 1)
        for text in ('loading data\n', valid + valid, valid.replace('ok', 'unknown'), valid.replace(' 123', '')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                evidence.rows(text, 'macos-arm64')

    def test_inventory_requires_exact_current_rows_and_revisions(self):
        reference = evidence.rows('linux-x86_64 1 2 8 8 4 1 ok 123\n', 'linux-x86_64')
        actual = evidence.rows('macos-arm64 1 2 8 8 4 1 ok 456\n', 'macos-arm64')
        evidence.check_inventory(actual, reference)
        for text in ('macos-arm64 1 3 8 8 4 1 ok 456\n', 'macos-arm64 1 2 8 8 4 2 ok 456\n'):
            with self.assertRaises(ValueError):
                evidence.check_inventory(evidence.rows(text, 'macos-arm64'), reference)
        with self.assertRaises(ValueError):
            evidence.check_inventory({}, reference)

    def test_complete_inventory_includes_unapproved_extended_team_cases(self):
        # The current Linux epoch table omits these valid 13--16 team requests.
        committed = evidence.rows('linux-x86_64 59 2 9 9 12 1 ok 123\n', 'linux-x86_64')
        requests = evidence.inventory('macos-arm64 59 2 9 9 12 1\nmacos-arm64 59 2 9 9 13 1\n', 'macos-arm64')
        evidence.require_reference_inventory(requests, committed)
        observed = evidence.rows('macos-arm64 59 2 9 9 12 1 ok 456\nmacos-arm64 59 2 9 9 13 1 ok 789\n', 'macos-arm64')
        evidence.check_inventory(observed, requests)
        with self.assertRaisesRegex(ValueError, 'missing='):
            evidence.check_inventory(committed, requests)
        with self.assertRaisesRegex(ValueError, 'extra='):
            evidence.check_inventory(observed | {(59, 2, 9, 9, 17, 1): ('ok', 1)}, requests)
        with self.assertRaisesRegex(ValueError, 'omits committed'):
            evidence.require_reference_inventory({}, committed)
        for malformed in ('', 'macos-arm64 59 2 9 9 12\n', 'macos-arm64 59 2 9 9 12 1\n' * 2):
            with self.assertRaises(ValueError):
                evidence.inventory(malformed, 'macos-arm64')

    def test_collection_rejects_binary_mutation_and_cleans_profiles(self):
        for mutate in (False, True):
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                table = root / 'test/map-generator-golden.txt'
                table.parent.mkdir()
                table.write_text('linux-x86_64 1 2 8 8 4 1 ok 123\n')
                historical = root / 'test/fixtures/map-generators/pre-resource-epoch-golden.txt'
                historical.parent.mkdir(parents=True)
                historical.write_text('preserved historical rows')
                (root / 'test/map-generator-resource-epoch.json').write_text('{}')
                binary = root / 'golden-test'
                binary.write_bytes(b'original binary')
                profiles = []

                def run(command, **options):
                    profiles.append(Path(command[1]))
                    self.assertTrue(profiles[-1].exists())
                    self.assertIn(command[2], ('--inventory', '--print'))
                    options['stdout'].write('macos-arm64 1 2 8 8 4 1' + ('\n' if command[2] == '--inventory' else ' ok 456\n'))
                    if mutate:
                        binary.write_bytes(b'changed binary')
                    return subprocess.CompletedProcess(command, 0)

                def output(command, **options):
                    return 'host clang version' if command[0] == 'clang++' else b'{"revision":"same"}'

                destination = root / 'evidence'
                with patch.object(evidence.platform, 'platform', return_value='test-host'), patch.object(evidence, 'ROOT', root), patch.object(evidence.subprocess, 'run', side_effect=run), patch.object(evidence.subprocess, 'check_output', side_effect=output):
                    if mutate:
                        with self.assertRaisesRegex(ValueError, 'binary changed'):
                            evidence.collect(binary, destination, 'macos-arm64')
                        self.assertFalse((destination / 'manifest.json').exists())
                    else:
                        evidence.collect(binary, destination, 'macos-arm64')
                        manifest = json.loads((destination / 'manifest.json').read_text())
                        self.assertIn('unverified', manifest['source_binary_association'])
                        self.assertEqual(manifest['row_count'], 1)
                self.assertTrue(all(not profile.exists() for profile in profiles))

    def test_independent_comparison_requires_same_inputs_and_every_fingerprint(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / 'first', Path(directory) / 'second'
            manifest = dict(platform='macos-arm64', source={'revision': 'same'}, expected_tables={'table': 'hash'}, inventory_sha256='inventory')
            for path in (first, second):
                path.mkdir()
                (path / 'manifest.json').write_text(json.dumps(manifest))
                (path / 'rows.txt').write_text('macos-arm64 1 2 8 8 4 1 ok 123\n')
            self.assertEqual(evidence.compare(first, second), 1)
            (second / 'rows.txt').write_text('macos-arm64 1 2 8 8 4 1 ok 124\n')
            with self.assertRaises(ValueError):
                evidence.compare(first, second)
            (second / 'rows.txt').write_text((first / 'rows.txt').read_text())
            for key in ('platform', 'source', 'expected_tables', 'inventory_sha256'):
                (second / 'manifest.json').write_text(json.dumps(dict(manifest, **{key: 'changed'})))
                with self.assertRaises(ValueError):
                    evidence.compare(first, second)


if __name__ == '__main__':
    unittest.main()
