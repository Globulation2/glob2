"""Release verification must resolve the same shared dependencies as the producer."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
sys.path.insert(0, str(ROOT / 'platforms/mobile'))
import android_release
from build_layout import build_identity
from dev_build import dependency_identity


class MobileReleaseDependencyIdentityTests(unittest.TestCase):
    def verify_fixture(self, arch, manifest):
        with tempfile.TemporaryDirectory() as task:
            root = Path(task)
            (root / 'platforms/mobile').mkdir(parents=True)
            (root / 'platforms/mobile/toolchain.json').write_text(json.dumps({'android': {'min_api': 24}}))
            apk = root / 'client.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('lib/' + arch + '/libmain.so', b'not inspected before library-set check')
            prefix = root / 'managed-dependencies'
            if manifest:
                prefix.mkdir()
                (prefix / 'manifest.json').write_text(json.dumps({'archives': {'lib/libSDL3.so': 'pinned'}}))
            sdk = root / 'sdk'
            canonical = dependency_identity(build_identity({'target': 'android', 'arch': arch, 'release': 1}))
            with patch.object(android_release, 'release_identity', return_value={'versionName': 'test'}), \
                 patch.object(android_release, 'version_code', return_value=1), \
                 patch.object(android_release, 'isolated', return_value=False), \
                 patch.object(android_release, 'dependency_prefix', return_value=prefix) as resolve, \
                 patch('mobile_toolchain.discover', return_value={'fingerprint': 'pinned-toolchain'}) as discover:
                expected = 'APK native libraries differ' if manifest else 'Missing Android dependency manifest'
                # Keep the normal strict manifest requirement enabled.
                with self.assertRaisesRegex(ValueError, expected):
                    android_release.verify_apk(apk, arch, sdk, root=root)
                resolve.assert_called_once_with(root, canonical, 'pinned-toolchain')
                discover.assert_called_once_with(canonical, {'android_sdk': str(sdk)})

    def test_shared_verifier_uses_producer_identity_and_rejects_missing_manifest(self):
        for arch in ('arm64-v8a', 'armeabi-v7a', 'x86_64'):
            with self.subTest(arch=arch):
                self.verify_fixture(arch, manifest=False)

    def test_canonical_manifest_still_enforces_exact_packaged_library_set(self):
        for arch in ('arm64-v8a', 'armeabi-v7a', 'x86_64'):
            with self.subTest(arch=arch):
                self.verify_fixture(arch, manifest=True)


if __name__ == '__main__':
    unittest.main()
