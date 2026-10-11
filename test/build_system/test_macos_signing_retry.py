"""Credential isolation and immutable artifact retry boundaries."""
import copy
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/release'))
import macos_keychain as keychain
import signing_retry as retry

COMMIT = 'a' * 40
IDENTITY = 'Developer ID Application: Example (TEAM)'

class SigningRetryTests(unittest.TestCase):
    def fixture(self):
        run = {'repository': {'id': retry.REPOSITORY_ID, 'full_name': retry.REPOSITORY},
               'actor': {'id': retry.OWNER_ID}, 'triggering_actor': {'id': retry.OWNER_ID},
               'event': 'workflow_dispatch', 'head_branch': 'master',
               'path': '.github/workflows/github-release.yml', 'conclusion': 'failure'}
        jobs = [{'name': name, 'status': 'completed', 'conclusion': 'success'} for name in
                ('context', 'packages / macos-package (macos-15, arm64)',
                 'packages / macos-package (macos-15-intel, x86_64)')]
        artifacts = [{'id': index + 1, 'name': 'unsigned-macos-' + arch,
                      'expired': False, 'digest': 'sha256:' + 'b' * 64}
                     for index, arch in enumerate(('arm64', 'x86_64'))]
        return run, jobs, artifacts, 'timestamp TAG: v0.11.0.5\ntimestamp ' + COMMIT + '\n'

    def test_failed_signing_run_can_reuse_verified_builds(self):
        self.assertEqual(set(retry.validate(*self.fixture(), 'v0.11.0.5', COMMIT)), {'arm64', 'x86_64'})

    def test_foreign_dispatch_and_mixed_source_fail_closed(self):
        for field, value in [('actor', {'id': 1}), ('triggering_actor', {'id': 1}),
                             ('repository', {'id': 1, 'full_name': retry.REPOSITORY}),
                             ('event', 'push'), ('head_branch', 'other')]:
            data = list(self.fixture()); data[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                retry.validate(*data, 'v0.11.0.5', COMMIT)
        for log in ('timestamp TAG: v0.11.0.6\ntimestamp ' + COMMIT + '\n',
                    'timestamp TAG: v0.11.0.5\ntimestamp ' + 'c' * 40 + '\n',
                    self.fixture()[3] + 'timestamp ' + COMMIT + '\n'):
            data = list(self.fixture()); data[3] = log
            with self.assertRaises(ValueError): retry.validate(*data, 'v0.11.0.5', COMMIT)

    def test_missing_failed_expired_duplicate_or_unhashed_artifacts_rejected(self):
        for mutation in ('missing', 'failed', 'expired', 'duplicate', 'digest'):
            data = list(self.fixture())
            if mutation == 'missing': data[1].pop()
            if mutation == 'failed': data[1][-1]['conclusion'] = 'failure'
            if mutation == 'expired': data[2][0]['expired'] = True
            if mutation == 'duplicate': data[2].append(copy.deepcopy(data[2][0]))
            if mutation == 'digest': data[2][0]['digest'] = None
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                retry.validate(*data, 'v0.11.0.5', COMMIT)

    def test_archive_checksum_and_contents_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); archive = root / 'artifact.zip'
            with zipfile.ZipFile(archive, 'w') as package:
                package.writestr('Glob2-0.11.0.5.dmg', b'final bytes')
            digest = 'sha256:' + hashlib.sha256(archive.read_bytes()).hexdigest()
            retry.extract(archive, digest, root / 'good', '0.11.0.5')
            self.assertEqual((root / 'good/Glob2-0.11.0.5.dmg').read_bytes(), b'final bytes')
            with self.assertRaises(ValueError): retry.extract(archive, '0' * 64, root / 'bad', '0.11.0.5')
            with zipfile.ZipFile(archive, 'w') as package: package.writestr('../escape.dmg', b'x')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            with self.assertRaises(ValueError): retry.extract(archive, digest, root / 'unsafe', '0.11.0.5')
            self.assertFalse((root / 'unsafe').exists())

class KeychainTests(unittest.TestCase):
    def test_intermediate_mismatch_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                keychain.prepare(directory, b'p12', 'password', b'p8', IDENTITY, b'wrong CA')
            self.assertFalse(list(Path(directory).iterdir()))

    def test_search_list_chain_identity_and_secret_file_permissions(self):
        commands = []
        def checked(args, label):
            commands.append(args)
            if args[1:] == ['list-keychains', '-d', 'user']: return '"/existing/login.keychain-db"\n'
            if args[1] == 'find-identity': return '1) ABC "' + IDENTITY + '"\n'
            return ''
        old_umask = os.umask(0o077)
        try:
            with tempfile.TemporaryDirectory() as directory, patch.object(keychain, 'checked', checked), \
                    patch.object(keychain, 'INTERMEDIATE_SHA256', hashlib.sha256(b'CA').hexdigest()):
                kc, key = keychain.prepare(directory, b'p12', 'secret', b'p8', IDENTITY, b'CA')
                self.assertIn(['security', 'list-keychains', '-d', 'user', '-s', str(kc),
                               '/existing/login.keychain-db'], commands)
                self.assertEqual(key.stat().st_mode & 0o777, 0o600)
                self.assertFalse((Path(directory) / 'glob2-developer-id.p12').exists())
                self.assertFalse((Path(directory) / 'glob2-developer-id-g2.cer').exists())
                imports = [args for args in commands if args[1] == 'import']
                self.assertTrue(imports[0][2].endswith('.cer'))
                self.assertTrue(imports[1][2].endswith('.p12'))
        finally: os.umask(old_umask)

    def test_imported_but_unusable_identity_fails_before_signing(self):
        old_umask = os.umask(0o077)
        try:
            with tempfile.TemporaryDirectory() as directory, patch.object(keychain, 'checked', return_value=''), \
                    patch.object(keychain, 'INTERMEDIATE_SHA256', hashlib.sha256(b'CA').hexdigest()):
                with self.assertRaisesRegex(ValueError, 'not valid'):
                    keychain.prepare(directory, b'p12', 'secret', b'p8', IDENTITY, b'CA')
        finally: os.umask(old_umask)

    def test_failure_does_not_disclose_password_or_command(self):
        with patch.object(keychain.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', 'secret')):
            with self.assertRaises(ValueError) as error:
                keychain.checked(['security', 'import', '-P', 'supersecret'], 'Import')
            self.assertNotIn('supersecret', str(error.exception))

    def test_retry_verifies_artifact_before_accessing_signing_secrets(self):
        workflow = (ROOT / '.github/workflows/macos-signing-retry.yml').read_text()
        self.assertLess(workflow.index('signing_retry.py download'), workflow.index('      - name: Import temporary Developer ID'))
        self.assertIn('ref: ${{ needs.preflight.outputs.source_commit }}', workflow)
        self.assertIn('environment: macos-developer-id', workflow)
        self.assertIn('source/tools/release/sign_macos.py', workflow)
        self.assertNotIn('contents: write', workflow)

if __name__ == '__main__': unittest.main()
