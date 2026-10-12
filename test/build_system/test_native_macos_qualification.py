"""Native host, source provenance and actual save/resume trace boundaries."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/release'))
import qualify_macos as qa
import signing_retry as retry
import test_macos_signing_retry as existing_retry_tests
COMMIT = existing_retry_tests.COMMIT


class NativeQualificationTests(unittest.TestCase):
    def cases(self, root):
        ranges = {'direct-600': (1, 600), 'repeat-600': (1, 600), 'save-300': (1, 300),
                  'resume-600': (300, 600), 'full-match': (1, 602)}
        for name, (first, last) in ranges.items():
            output = root / 'cases' / name; output.mkdir(parents=True)
            (output / 'world.checksums').write_text(''.join(f'{tick} {tick+100}\n' for tick in range(first, last)))
            (output / 'result.json').write_text(json.dumps({'ticks': last, 'status': 'completed', 'initialChecksum': first+100, 'finalChecksum': last+100, 'unresolved': name != 'full-match'}))
        (root / 'cases/full-match/game.replay').write_bytes(b'fixture replay')

    def report(self, arch, cases):
        return {'tag': 'v0.11.0.6', 'sourceCommit': COMMIT, 'fixtureSha256': qa.FIXTURE_SHA256,
                'version': {'version': '0.11.0.6'}, 'sourceRunId': 123, 'verifierCommit': 'c'*40,
                'qualificationRunId': '456', 'architecture': arch, 'hostArchitecture': arch,
                'nativeHost': True, 'publisherVerified': True, 'finalDmgSha256': 'd'*64, 'cases': cases}

    def test_actual_save_resume_and_repeat_are_consistent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); self.cases(root)
            self.assertEqual(qa.verify_cases(root)['full-match']['ticks'], 602)
            path = root / 'cases/resume-600/world.checksums'
            path.write_text(path.read_text().replace('350 450', '350 451'))
            with self.assertRaisesRegex(ValueError, 'Resumed save diverged'): qa.verify_cases(root)

    def test_missing_duplicate_and_reordered_ticks_rejected(self):
        for text in ('0 1\n2 3\n', '0 1\n0 1\n', '1 2\n0 1\n', 'not a checksum\n'):
            with self.subTest(text=text), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'trace'; path.write_text(text)
                with self.assertRaises(ValueError): qa.trace(path)

    def test_unfinished_match_or_missing_replay_rejected(self):
        for fault in ('unresolved', 'replay'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                root = Path(directory); self.cases(root)
                if fault == 'replay': (root / 'cases/full-match/game.replay').unlink()
                else:
                    path = root / 'cases/full-match/result.json'; data = json.loads(path.read_text()); data['unresolved'] = True; path.write_text(json.dumps(data))
                with self.assertRaises(ValueError): qa.verify_cases(root)

    def test_native_comparison_rejects_mixed_revisions_and_translated_hosts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for arch in ('arm64', 'x86_64'):
                output = root / ('qualification-macos-' + arch); self.cases(output)
                (output / 'report.json').write_text(json.dumps(self.report(arch, qa.verify_cases(output))))
            qa.compare(root, root / 'comparison.json')
            path = root / 'qualification-macos-x86_64/report.json'; original = json.loads(path.read_text())
            for field, value in (('sourceCommit', 'e'*40), ('sourceRunId', 999), ('verifierCommit', 'f'*40),
                                 ('nativeHost', False), ('hostArchitecture', 'arm64'), ('publisherVerified', False)):
                data = copy.deepcopy(original); data[field] = value; path.write_text(json.dumps(data))
                with self.subTest(field=field), self.assertRaises(ValueError): qa.compare(root, root / 'bad.json')
            path.write_text(json.dumps(original))
            trace = root / 'qualification-macos-x86_64/cases/direct-600/world.checksums'
            trace.write_text(trace.read_text().replace('350 450', '350 451'))
            with self.assertRaises(ValueError): qa.compare(root, root / 'bad.json')

    def test_reports_cannot_disagree_with_identical_trace_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for arch in ('arm64', 'x86_64'):
                output = root / ('qualification-macos-' + arch); self.cases(output)
                report = self.report(arch, qa.verify_cases(output))
                report['cases']['direct-600']['traceSha256'] = 'a'*64
                (output / 'report.json').write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'final trace bytes'):
                qa.compare(root, root / 'bad.json')

    def test_architecture_build_times_differ_but_compatibility_versions_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for arch, compiled in (('arm64', 'Oct 11 2026 03:28:41'),
                                   ('x86_64', 'Oct 11 2026 03:53:06')):
                output = root / ('qualification-macos-' + arch); self.cases(output)
                report = self.report(arch, qa.verify_cases(output))
                report['version'].update(compiled=compiled, cli_version=2, minimum_save_version=58,
                                         save_version=152, network_protocol=68, sdl='3.4.16')
                (output / 'report.json').write_text(json.dumps(report))
            qa.compare(root, root / 'comparison.json')
            path = root / 'qualification-macos-x86_64/report.json'
            original = json.loads(path.read_text())
            for field in ('version', 'cli_version', 'minimum_save_version', 'save_version',
                          'network_protocol', 'sdl'):
                report = copy.deepcopy(original); report['version'][field] = 'different'
                path.write_text(json.dumps(report))
                with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'compatibility versions'):
                    qa.compare(root, root / 'bad.json')

    def test_rosetta_and_wrong_platform_fail_before_package_access(self):
        args = SimpleNamespace(architecture='x86_64')
        with patch.object(qa.platform, 'system', return_value='Darwin'), patch.object(qa.platform, 'machine', return_value='arm64'):
            with self.assertRaisesRegex(ValueError, 'native Mac host'): qa.qualify(args)
        with patch.object(qa.platform, 'system', return_value='Darwin'), patch.object(qa.platform, 'machine', return_value='x86_64'), \
                patch.object(qa.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout='1\n')):
            with self.assertRaisesRegex(ValueError, 'Translated execution'): qa.qualify(args)

    def test_final_artifacts_require_both_successful_signing_jobs(self):
        data = list(existing_retry_tests.SigningRetryTests().fixture())
        for artifact in data[2]: artifact['name'] = artifact['name'].replace('unsigned-', 'signed-')
        with self.assertRaises(ValueError): retry.validate(*data, 'v0.11.0.5', COMMIT, 'signed-macos')
        for name in ('macos-sign (macos-15, arm64)', 'macos-sign (macos-15-intel, x86_64)'):
            data[1].append({'name': name, 'status': 'completed', 'conclusion': 'success'})
        self.assertEqual(set(retry.validate(*data, 'v0.11.0.5', COMMIT, 'signed-macos')), {'arm64', 'x86_64'})
        data[1][-1]['conclusion'] = 'failure'
        with self.assertRaises(ValueError): retry.validate(*data, 'v0.11.0.5', COMMIT, 'signed-macos')

    def test_final_archive_cannot_mix_architecture_or_unsigned_filename(self):
        import zipfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); archive = root / 'artifact.zip'
            with zipfile.ZipFile(archive, 'w') as z: z.writestr('Glob2-0.11.0.6-macos-arm64.dmg', b'signed bytes')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            retry.extract(archive, digest, root / 'good', '0.11.0.6', 'arm64')
            for arch in ('x86_64', None):
                with self.subTest(arch=arch), self.assertRaises(ValueError): retry.extract(archive, digest, root / 'bad', '0.11.0.6', arch)

    def test_fixture_is_frozen_and_qualification_has_no_publication_credentials(self):
        fixture = ROOT / 'test/fixtures/releases/qualification-river.map.gz'
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(), qa.FIXTURE_SHA256)
        workflow = (ROOT / '.github/workflows/macos-downloads-qualification.yml').read_text()
        self.assertIn('--kind signed-macos', workflow)
        self.assertIn('ref: ${{ needs.preflight.outputs.source_commit }}', workflow)
        self.assertIn('macos-15-intel', workflow)
        self.assertNotIn('environment:', workflow)
        self.assertNotIn('contents: write', workflow)
        import re
        self.assertEqual(set(re.findall(r'secrets\.([A-Z_]+)', workflow)), {'GITHUB_TOKEN'})
        self.assertNotIn('*.p8', workflow)

if __name__ == '__main__': unittest.main()
