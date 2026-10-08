"""Regression checks for evidence provenance and Android transfer failures."""
import gzip
import struct
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from build_provenance import build_issues, source_identity
from check_javascript_evidence import provenance_issues, inventory
from check_javascript import save_header

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('android_device_tests', ROOT / 'mobile/android_device_tests.py')
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class ProvenanceTests(unittest.TestCase):
    def test_isolated_tool_lease_does_not_change_source_identity(self):
        spec = importlib.util.spec_from_file_location('dev_store', ROOT / 'scons/dev_store.py')
        store = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(store)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            def git(*args):
                subprocess.run(['git', '-C', str(root), *args], check=True,
                               stdout=subprocess.DEVNULL)
            git('config', 'user.email', 'test@example.invalid')
            git('config', 'user.name', 'Test')
            (root / '.gitignore').write_bytes((ROOT / '.gitignore').read_bytes())
            git('add', '.gitignore')
            git('commit', '-qm', 'fixture')
            before = source_identity(root)
            with patch.dict(os.environ, {'GLOB2_DEV_MODE': 'isolated'}):
                lease = store.Lease(root / 'tools/browser-emsdk', track_use=False)
                try:
                    self.assertEqual(before, source_identity(root))
                    (root / 'tools/new-source.js').write_text('const value = 1;')
                    self.assertTrue(source_identity(root)['dirty'])
                finally:
                    lease.close()

    def test_clean_runner_cannot_relabel_stale_binary(self):
        source = {'revision': 'new', 'dirty': False, 'sourceTreeSha256': 'new-bytes'}
        stale = dict(source, revision='old', sourceTreeSha256='old-bytes')
        manifest = dict(source, tests=[{'exitCode': 0, 'build': stale}])
        issues = provenance_issues(manifest, manifest)
        self.assertTrue(any('revision' in issue for issue in issues))
        self.assertTrue(any('sourceTreeSha256' in issue for issue in issues))
        self.assertTrue(build_issues({}, source))
        self.assertFalse(build_issues(source, source))

    def test_source_bytes_are_checked_in_addition_to_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            def git(*args):
                subprocess.run(['git', '-C', str(root), *args], check=True,
                               stdout=subprocess.DEVNULL)
            git('config', 'user.email', 'test@example.invalid')
            git('config', 'user.name', 'Test')
            path = root / 'runtime.cpp'
            path.write_text('before')
            git('add', '.')
            git('commit', '-qm', 'fixture')
            before = source_identity(root)
            path.write_text('after')
            after = source_identity(root)
            self.assertEqual(before['revision'], after['revision'])
            self.assertNotEqual(before['sourceTreeSha256'], after['sourceTreeSha256'])
            self.assertFalse(before['dirty'])
            self.assertTrue(after['dirty'])
            self.assertEqual(before['sourceStatus'], [])
            self.assertEqual(after['sourceStatus'], [' M runtime.cpp'])
            (root / 'extra.js').write_text('new source input')
            self.assertEqual(source_identity(root)['sourceStatus'],
                             [' M runtime.cpp', '?? extra.js'])

    def test_older_subset_cannot_omit_retained_nan_or_conversion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            numbers = root / 'JavaScriptNumbers/case'
            simulation = root / 'JavaScriptSimulation/case'
            numbers.mkdir(parents=True)
            simulation.mkdir(parents=True)
            (numbers / 'numeric-profile1.value').write_bytes(b'numbers')
            (simulation / 'game.checksums').write_bytes(b'trace')
            with self.assertRaisesRegex(ValueError, 'global-numbers'):
                inventory(root)
            (numbers / 'global-numbers-profile1.value').write_bytes(b'snapshots')
            with self.assertRaisesRegex(ValueError, 'conversion continuation'):
                inventory(root)

    def test_touching_unchanged_tracked_input_preserves_clean_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            def git(*args):
                subprocess.run(['git', '-C', str(root), *args], check=True,
                               stdout=subprocess.DEVNULL)
            git('config', 'user.email', 'test@example.invalid')
            git('config', 'user.name', 'Test')
            path = root / 'runtime.cpp'
            path.write_text('unchanged source')
            git('add', '.')
            git('commit', '-qm', 'fixture')
            before = source_identity(root)
            path.write_text('unchanged source')
            self.assertEqual(before, source_identity(root))
            self.assertEqual(before, source_identity(root))

    def test_checkout_symlinks_and_crlf_have_identical_clean_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'original'
            clone = Path(directory) / 'windows-style'
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            def git(*args):
                subprocess.run(['git', '-C', str(root), *args], check=True,
                               stdout=subprocess.DEVNULL)
            git('config', 'user.email', 'test@example.invalid')
            git('config', 'user.name', 'Test')
            (root / 'runtime.cpp').write_bytes(b'before\nnext line\n')
            (root / 'alias').symlink_to('runtime.cpp')
            git('add', '.')
            git('commit', '-qm', 'fixture')
            subprocess.run(['git', 'clone', '-q', '-c', 'core.symlinks=false', '-c', 'core.autocrlf=true',
                            str(root), str(clone)], check=True)
            self.assertFalse((clone / 'alias').is_symlink())
            self.assertIn(b'\r\n', (clone / 'runtime.cpp').read_bytes())
            self.assertEqual(source_identity(root), source_identity(clone))
            changed = dict(source_identity(clone), sourceTreeSha256='different inputs',
                           tests=[{'exitCode': 0, 'build': source_identity(clone)}])
            original = dict(source_identity(root), tests=[{'exitCode': 0, 'build': source_identity(root)}])
            self.assertTrue(any('Normalized source trees' in issue
                                for issue in provenance_issues(original, changed)))

    def test_missing_producer_and_retrieval_failure_are_ineligible(self):
        manifest = {'revision': 'head', 'dirty': False, 'tests': [{'exitCode': 0}],
                    'retrievalErrors': [{'error': 'disconnected'}]}
        issues = provenance_issues(manifest, manifest)
        self.assertTrue(any('Missing executed-binary' in issue for issue in issues))
        self.assertTrue(any('retrieval' in issue for issue in issues))


class AndroidRetrievalTests(unittest.TestCase):
    def test_failed_pull_cannot_accept_preexisting_report(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'tests.xml'
            destination.write_text('<testsuites/>')
            errors = []
            with patch.object(android.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 1, stdout='device disconnected')):
                self.assertFalse(android.retrieve(['adb'], '/remote/tests.xml', destination, errors, xml=True))
            self.assertIn('disconnected', errors[0]['error'])

    def test_successful_pull_requires_valid_xml_and_nonempty_corpus(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'tests.xml'
            destination.write_text('truncated')
            errors = []
            with patch.object(android.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, stdout='')):
                self.assertFalse(android.retrieve(['adb'], '/remote/tests.xml', destination, errors, xml=True))
                self.assertFalse(android.retrieve(['adb'], '/remote/artifacts', Path(directory) / 'corpus', errors))
            self.assertEqual(len(errors), 2)


class SaveHeaderTests(unittest.TestCase):
    @staticmethod
    def fixture(version, resource_declarations=None, required=(), ai_delay=0, building_gradient_delay=4):
        def text(value):
            encoded = value.encode('utf8')
            return struct.pack('>I', len(encoded)) + encoded
        result = text('Resource header fixture')
        result += struct.pack('>IIII', 0, version, 2, 0) + b'\1' + bytes(20)
        if version >= 134:
            result += struct.pack('>I', 1) + text('ice-terrain')
        if version >= 140:
            declarations = resource_declarations or []
            result += struct.pack('>I', len(declarations))
            for definition in declarations:
                result += b''.join(text(value) for value in definition)
            result += struct.pack('>I', len(required))
            result += b''.join(text(key) for key in required)
        result += bytes(40)  # Two BaseTeam headers.
        result += bytes(5)
        if version >= 143:
            result += bytes([ai_delay])
        if version >= 148:
            result += bytes([building_gradient_delay])
        result += struct.pack('>I', 3)  # Three players in GameHeader.
        return gzip.compress(result)

    def parse(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.game.gz'
            path.write_bytes(value)
            return save_header(path)

    def test_legacy_and_resource_headers_have_correct_player_offsets(self):
        for version in (125, 137, 138, 139, 140, 142, 143, 146, 147, 148):
            self.assertEqual(self.parse(self.fixture(version)), (0, version, 2, 3))
        declarations = [('custom-crops', 'Custom crops', 'Enable experimental multi-material crops.')]
        self.assertEqual(self.parse(self.fixture(140, declarations, ['custom-crops'])), (0, 140, 2, 3))

    def test_ai_delay_does_not_shift_player_count(self):
        for delay in (0, 1, 8):
            self.assertEqual(self.parse(self.fixture(143, ai_delay=delay)), (0, 143, 2, 3))

    def test_building_gradient_delay_does_not_shift_player_count(self):
        for delay in (1, 4, 8):
            self.assertEqual(self.parse(self.fixture(148, building_gradient_delay=delay)), (0, 148, 2, 3))

    def test_resource_header_metadata_bounds_are_checked(self):
        for declarations, required in [([('x' * 129, 'Label', 'Help')], []),
                                      ([('key', 'L' * 513, 'Help')], []),
                                      ([('key', 'Label', 'H' * 4097)], []),
                                      ([], ['x' * 129]),
                                      ([('key', 'Label', 'Help')] * 65, [])]:
            with self.assertRaises(AssertionError):
                self.parse(self.fixture(140, declarations, required))


if __name__ == '__main__':
    unittest.main()
