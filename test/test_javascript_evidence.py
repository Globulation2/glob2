"""Regression checks for evidence provenance and Android transfer failures."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from build_provenance import build_issues, source_identity
from check_javascript_evidence import provenance_issues, inventory

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('android_device_tests', ROOT / 'mobile/android_device_tests.py')
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class ProvenanceTests(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
