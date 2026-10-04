import hashlib
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mobile'))
import play_release


class Request:
    def __init__(self, response):
        self.response = response

    def execute(self, **_):
        return self.response


class FakeEdits:
    def __init__(self, code, digest, active=1, bad_digest=False):
        self.code, self.digest, self.active = code, digest, active
        self.bad_digest = bad_digest
        self.calls = []

    def insert(self, **kwargs):
        self.calls.append(('insert', kwargs))
        return Request({'id': 'edit-1'})

    def tracks(self):
        return self

    def bundles(self):
        return self

    def get(self, **kwargs):
        self.calls.append(('get', kwargs))
        return Request({'track': 'internal', 'releases': [{'versionCodes': [str(self.active)]}]})

    def upload(self, **kwargs):
        self.calls.append(('upload', kwargs))
        return Request({'versionCode': self.code,
                        'sha256': '0' * 64 if self.bad_digest else self.digest})

    def update(self, **kwargs):
        self.calls.append(('update', kwargs))
        return Request(kwargs['body'])

    def validate(self, **kwargs):
        self.calls.append(('validate', kwargs))
        return Request({})

    def commit(self, **kwargs):
        self.calls.append(('commit', kwargs))
        return Request({})


class PlayReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        unsigned = root / 'app-release.aab'
        unsigned.write_bytes(b'unsigned bundle')
        self.bundle = root / 'app-release-play.aab'
        self.bundle.write_bytes(b'signed bundle')
        self.code = 42
        unsigned.with_suffix('.json').write_text(json.dumps({
            'version_code': self.code, 'architecture': 'arm64-v8a',
            'sha256': hashlib.sha256(unsigned.read_bytes()).hexdigest()}))
        self.digest = hashlib.sha256(self.bundle.read_bytes()).hexdigest()
        media = types.ModuleType('googleapiclient.http')
        media.MediaFileUpload = lambda *args, **kwargs: (args, kwargs)
        package = types.ModuleType('googleapiclient')
        self.patch_modules = patch.dict(sys.modules, {
            'googleapiclient': package, 'googleapiclient.http': media})
        self.patch_modules.start()
        self.addCleanup(self.patch_modules.stop)

    def service(self, **kwargs):
        edits = FakeEdits(self.code, self.digest, **kwargs)
        return types.SimpleNamespace(edits=lambda: edits), edits

    def test_version_codes_are_monotonic_seconds_within_play_limit(self):
        self.assertEqual(play_release.version_code(play_release.VERSION_EPOCH + 42), 42)
        with self.assertRaises(ValueError):
            play_release.version_code(play_release.VERSION_EPOCH + play_release.MAX_VERSION_CODE + 1)

    def test_publishes_only_internal_track_after_validation(self):
        service, edits = self.service()
        result = play_release.publish(service, self.bundle, self.code, 'Internal 42', 'Test build')
        self.assertEqual(result['bundle_sha256'], self.digest)
        self.assertEqual([name for name, _ in edits.calls],
                         ['insert', 'get', 'upload', 'update', 'validate', 'commit'])
        update = edits.calls[3][1]
        self.assertEqual(update['track'], 'internal')
        self.assertEqual(update['body']['releases'][0]['status'], 'completed')
        self.assertEqual(update['body']['releases'][0]['versionCodes'], ['42'])
        self.assertEqual(update['body']['releases'][0]['releaseNotes'][0]['text'], 'Test build')

    def test_never_commits_wrong_or_reused_bundle(self):
        for kwargs in ({'active': self.code}, {'bad_digest': True}):
            with self.subTest(kwargs=kwargs):
                service, edits = self.service(**kwargs)
                with self.assertRaises(ValueError):
                    play_release.publish(service, self.bundle, self.code, 'Internal 42', 'Test build')
                self.assertNotIn('commit', [name for name, _ in edits.calls])

    def test_rejects_changed_build_before_creating_edit(self):
        (self.bundle.parent / 'app-release.aab').write_bytes(b'changed')
        service, edits = self.service()
        with self.assertRaises(ValueError):
            play_release.publish(service, self.bundle, self.code, 'Internal 42', 'Test build')
        self.assertEqual(edits.calls, [])


if __name__ == '__main__':
    unittest.main()
