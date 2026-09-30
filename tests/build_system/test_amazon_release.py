"""Amazon release identity, artifact trust boundary, and submission safety."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mobile'))
import amazon_release as release


class VersionTests(unittest.TestCase):
    def test_version_code_is_stable_and_ordered(self):
        self.assertEqual(release.version_code('0.9.5.0'), 9050000)
        self.assertLess(release.version_code('0.9.5.9'), release.version_code('0.9.6.0'))
        self.assertLess(release.version_code('0.9.99.9999'), release.version_code('0.10.0.0'))
        for version in ('0.9.5', '0.9.100.0', '0.9.5.10000', '21.0.0.0'):
            with self.subTest(version=version), self.assertRaises(ValueError):
                release.version_code(version)


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.apk = root / 'glob2-fire-unsigned.apk'
        self.manifest = root / 'provenance.json'
        with zipfile.ZipFile(self.apk, 'w') as archive:
            for abi in release.ABIS:
                archive.writestr(f'lib/{abi}/libmain.so', b'native')
                archive.writestr(f'lib/{abi}/libc++_shared.so', b'c++')
        self.data = {'sha256': release.sha256(self.apk), 'version_code': release.version_code(),
            'version_name': release.PACKAGE_VERSION, 'package': release.PACKAGE,
            'architectures': list(release.ABIS), 'tag': 'v' + release.PACKAGE_VERSION,
            'source_commit': 'a' * 40}
        self.manifest.write_text(json.dumps(self.data))

    def test_rejects_changed_handoff_before_signing(self):
        with patch.object(release, 'verify_apk'):
            release.verify_artifact(self.apk, self.manifest, self.apk.parent)
            self.apk.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'provenance mismatch'):
                release.verify_artifact(self.apk, self.manifest, self.apk.parent)

    def test_rejects_single_abi(self):
        with zipfile.ZipFile(self.apk, 'w') as archive:
            archive.writestr('lib/arm64-v8a/libmain.so', b'native')
        with patch('developer_apk.verify_alignment'), patch.object(release, 'verify_apk_assets'):
            with self.assertRaisesRegex(ValueError, 'unequal ARM native libraries'):
                release.verify_apk(self.apk, self.apk.parent)


class FakeAPI:
    def __init__(self, active=None, old_code=1):
        self.active, self.old_code = active, old_code
        self.calls = []

    def request(self, method, path, data=None, headers=None):
        self.calls.append((method, path, data, headers))
        if path == '/edits' and method == 'GET': return self.active, None
        if path == '/edits' and method == 'POST': return {'id': 'edit-42'}, None
        if path.endswith('/apks') and method == 'GET': return [{'id': 'apk-1', 'versionCode': self.old_code}], None
        if path.endswith('/apks/apk-1') and method == 'GET': return {'id': 'apk-1'}, 'apk-etag'
        if path.endswith('/replace'): return {'versionCode': release.version_code()}, None
        if path.endswith('/listings/en_US') and method == 'GET': return {'language': 'en_US', 'title': 'Globulation 2'}, 'listing-etag'
        if path.endswith('/listings/en_US') and method == 'PUT': return {'recentChanges': 'New maps'}, None
        if path.endswith('/validate'): return {'id': 'edit-42'}, None
        if path == '/edits/edit-42' and method == 'GET': return {'id': 'edit-42'}, 'edit-etag'
        if path.endswith('/commit'): return {'status': 'SUBMITTED'}, None
        raise AssertionError((method, path))


class PublisherTests(ArtifactTests):
    def setUp(self):
        super().setUp()
        self.signed = self.apk.with_name('glob2-fire-amazon.apk')
        self.signed.write_bytes(self.apk.read_bytes())
        self.signed_manifest = self.signed.with_suffix('.json')
        self.signed_manifest.write_text(json.dumps(dict(self.data,
            unsigned_sha256=self.data['sha256'], sha256=release.sha256(self.signed),
            signing_certificate_sha256='f' * 64)))

    def run_publish(self, api):
        with patch.object(release, 'verify_apk'), patch('developer_apk.build_tools', return_value=self.apk.parent), \
             patch('amazon_release.subprocess.check_output', return_value='Signer #1 certificate SHA-256 digest: ' + 'f' * 64):
            return release.publish(api, self.signed, self.signed_manifest, self.apk.parent, 'New maps')

    def test_refuses_existing_draft_without_mutation(self):
        api = FakeAPI(active={'id': 'someone-elses-edit'})
        with self.assertRaisesRegex(ValueError, 'open edit'):
            self.run_publish(api)
        self.assertEqual([call[:2] for call in api.calls], [('GET', '/edits')])

    def test_refuses_nonincreasing_version_before_upload(self):
        api = FakeAPI(old_code=release.version_code())
        with self.assertRaisesRegex(ValueError, 'must exceed'):
            self.run_publish(api)
        self.assertNotIn('PUT', [call[0] for call in api.calls])

    def test_replaces_apk_updates_notes_validates_and_commits(self):
        api = FakeAPI()
        result = self.run_publish(api)
        self.assertEqual(result['edit_id'], 'edit-42')
        self.assertEqual([call[1].split('/')[-1] for call in api.calls[-4:]],
                         ['en_US', 'validate', 'edit-42', 'commit'])
        self.assertEqual(api.calls[-1][3], {'If-Match': 'edit-etag'})
        self.assertEqual(json.loads(api.calls[-4][2])['recentChanges'], 'New maps')


if __name__ == '__main__':
    unittest.main()
