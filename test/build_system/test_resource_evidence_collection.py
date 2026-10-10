"""Native resource trace collection must fail closed and isolate scratch state."""
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class ResourceCollectionTest(unittest.TestCase):
    def collect(self, output, *, failure=None, truncate=False, target='resources'):
        profiles = []
        references = {
            ROOT / 'test/fixtures/resources/seeded-compositions-units.trace': b'1 2 3 checksum\n' * 150,
            ROOT / 'test/fixtures/unit-catalog/ablation-checksums.txt': b'1 2 3 checksum\n' * 97,
        }
        original_read = Path.read_bytes

        def read_bytes(path):
            return references[path] if path in references else original_read(path)

        def run(command, **options):
            if 'repeat' in command:
                replay = Path(options['env']['GLOB2_REPLAY_PATH'])
                Path(str(replay) + '.checksums').write_bytes(b'x' * 1501)
            elif 'verify' in command:
                directory = Path(command[command.index('--output-dir') + 1])
                directory.mkdir()
                (directory / 'checksums.txt').write_bytes(b'x\n' * 601)
                (directory / 'verdict.json').write_text('{"verdict":"verified"}')
                (directory / 'match.replay').touch()
            else:
                env = options['env']
                profiles.append(Path(env['GLOB2_USER_DATA_DIR']))
                self.assertNotIn('GLOB2_TEST_UPDATE_FIXTURES', env)
                self.assertNotIn('GLOB2_TEST_ARTIFACTS', env)
                units = '--test-suite=UnitCustomization' in command
                family = 'units' if units else 'resources'
                if failure and target == family:
                    raise failure
                directory = Path(env['GLOB2_TEST_ARTIFACTS_ROOT'])
                directory.mkdir()
                fixture = ROOT / ('test/fixtures/unit-catalog/ablation-checksums.txt' if units else
                                  'test/fixtures/resources/seeded-compositions-units.trace')
                trace = fixture.read_bytes()
                name = 'unit-ablations.trace' if units else 'seeded-compositions.trace'
                (directory / name).write_bytes(trace[:-1] if truncate and target == family else trace)
                (directory / 'build-provenance.json').write_text(json.dumps(dict(
                    revision='revision', sourceTreeSha256='source', dirty=False)))
            return subprocess.CompletedProcess(command, 0)

        args = ['run-browser-determinism.py', str(ROOT / 'fake-cli'), str(output),
                '--engine-binary', str(ROOT / 'fake-engine')]
        try:
            with redirect_stdout(io.StringIO()), patch('sys.argv', args), patch('subprocess.run', side_effect=run), patch.object(Path, 'read_bytes', read_bytes), patch.dict(
                    os.environ, GLOB2_TEST_UPDATE_FIXTURES='1', GLOB2_TEST_ARTIFACTS='/stale'):
                runpy.run_path(str(ROOT / 'test/run-browser-determinism.py'), run_name='__main__')
        finally:
            for profile in profiles:
                self.assertFalse(profile.exists())

    def test_complete_trace_and_provenance_are_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            self.collect(output)
            manifest = json.loads((output / 'resources/native/manifest.json').read_text())
            self.assertEqual(manifest['producer']['revision'], 'revision')
            self.assertIn('--test-suite=RuntimeResources', manifest['command'])
            manifest = json.loads((output / 'units/native/manifest.json').read_text())
            self.assertEqual(manifest['producer']['revision'], 'revision')
            self.assertIn('--test-suite=UnitCustomization', manifest['command'])

    def test_failed_or_interrupted_run_has_no_success_manifest(self):
        for error in (subprocess.CalledProcessError(1, 'engine'), subprocess.TimeoutExpired('engine', 300)):
            with self.subTest(error=type(error)), tempfile.TemporaryDirectory() as directory:
                output = Path(directory)
                with self.assertRaises(type(error)):
                    self.collect(output, failure=error)
                self.assertFalse((output / 'resources/native/manifest.json').exists())

    def test_truncated_trace_fails_and_stale_output_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with self.assertRaises(RuntimeError):
                self.collect(output, truncate=True)
            self.assertFalse((output / 'resources/native/manifest.json').exists())
            with self.assertRaises(FileExistsError):
                self.collect(output)

    def test_failed_or_truncated_unit_collection_cannot_publish_success(self):
        for error in (subprocess.CalledProcessError(1, 'engine'), subprocess.TimeoutExpired('engine', 300)):
            with self.subTest(error=type(error)), tempfile.TemporaryDirectory() as directory:
                output = Path(directory)
                with self.assertRaises(type(error)):
                    self.collect(output, failure=error, target='units')
                self.assertTrue((output / 'resources/native/manifest.json').exists())
                self.assertFalse((output / 'units/native/manifest.json').exists())
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with self.assertRaises(RuntimeError):
                self.collect(output, truncate=True, target='units')
            self.assertFalse((output / 'units/native/manifest.json').exists())


if __name__ == '__main__':
    unittest.main()
