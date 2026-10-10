"""Dependency producers and native consumers must agree on mobile identities."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'mobile'))
sys.path.insert(0, str(ROOT / 'scons'))
import dependencies
import android
from build_layout import build_identity
from dev_build import dependency_identity


class MobileDependencyManifestTests(unittest.TestCase):
    def test_android_packaging_accepts_producer_manifest_and_rejects_changed_bytes(self):
        class PackagingReached(Exception):
            pass

        cases = [(arch, 'configure', True) for arch in ('arm64-v8a', 'armeabi-v7a', 'x86_64')]
        cases += [('arm64-v8a', 'bundle', False)]
        for arch, command, fdroid in cases:
            for corruption in (None, 'identity', 'toolchain', 'archive'):
                with self.subTest(arch=arch, command=command, corruption=corruption), tempfile.TemporaryDirectory() as task:
                    root = Path(task).resolve()
                    from build_layout import default_directory
                    identity = build_identity({'target': 'android', 'arch': arch, 'release': 1, 'china': 0, 'amazon': 0})
                    output = root / default_directory(identity)
                    (output / 'lib').mkdir(parents=True)
                    (output / 'lib/libmain.so').write_bytes(b'native client')
                    (root / 'mobile').mkdir()
                    (root / 'mobile/android').mkdir()
                    prefix = root / 'deps'
                    (prefix / 'share/glob2/sdl-java').mkdir(parents=True)
                    library = prefix / 'library.a'
                    library.write_bytes(b'verified dependency')
                    dep_identity = dependency_identity(build_identity({'target': 'android', 'arch': arch, 'release': 1, 'china': 0}))
                    manifest = {'identity': dep_identity, 'toolchain': {'compiler': 'pinned'},
                        'archives': {'library.a': hashlib.sha256(library.read_bytes()).hexdigest()}}
                    if corruption == 'identity': manifest['identity'] = dict(dep_identity, arch='wrong')
                    if corruption == 'toolchain': manifest['toolchain'] = {'compiler': 'wrong'}
                    (prefix / 'manifest.json').write_text(json.dumps(manifest))
                    if corruption == 'archive': library.write_bytes(b'changed bytes')
                    sdk = root / 'sdk'
                    ndk = json.loads(android.LOCK.read_text())['android']['ndk']
                    prebuilt = sdk / 'ndk' / ndk / 'toolchains/llvm/prebuilt/linux-x86_64'
                    triple = {'arm64-v8a': 'aarch64-linux-android', 'armeabi-v7a': 'arm-linux-androideabi', 'x86_64': 'x86_64-linux-android'}[arch]
                    runtime = prebuilt / 'sysroot/usr/lib' / triple / 'libc++_shared.so'
                    runtime.parent.mkdir(parents=True)
                    runtime.write_bytes(b'native runtime')
                    argv = ['android.py', command, '--arch', arch, '--release'] + (['--fdroid'] if fdroid else [])
                    with patch.object(sys, 'argv', argv), patch.object(android, 'ROOT', root), \
                         patch.object(android, 'android_sdk', return_value=sdk), \
                         patch.object(android, 'dependency_prefix', return_value=prefix), \
                         patch.object(android, 'discover', return_value={'fingerprint': {'compiler': 'pinned'}}), \
                         patch.object(android.subprocess, 'run'), patch.object(android, 'verify_android_shared_library'), \
                         patch('tools.package_assets.export_assets', side_effect=PackagingReached) as export:
                        with self.assertRaises(ValueError if corruption else PackagingReached):
                            android.main()
                        self.assertEqual(export.called, corruption is None)

    def test_existing_bundle_matches_native_consumer_for_all_mobile_targets(self):
        cases = [('android', arch, 'device') for arch in ('arm64-v8a', 'armeabi-v7a', 'x86_64')]
        cases += [('ios', 'arm64', environment) for environment in ('device', 'simulator')]
        for target, arch, environment in cases:
            with self.subTest(target=target, arch=arch, environment=environment), tempfile.TemporaryDirectory() as task:
                root = Path(task)
                prefix = root / 'deps'
                prefix.mkdir()
                library = prefix / 'library.a'
                library.write_bytes(b'verified native dependency')
                identity = dependency_identity(build_identity({'target': target, 'arch': arch,
                    'environment': environment, 'release': 1}))
                (prefix / 'manifest.json').write_text(json.dumps({'identity': identity,
                    'toolchain': {'compiler': 'pinned'}, 'archives': {
                        'library.a': hashlib.sha256(library.read_bytes()).hexdigest()}}))
                argv = ['dependencies.py', '--target', target, '--arch', arch,
                    '--environment', environment, '--release']
                with patch.object(sys, 'argv', argv), patch.object(dependencies, 'ROOT', root), \
                     patch.object(dependencies, 'isolated', return_value=True), \
                     patch.object(dependencies, 'dependency_prefix', return_value=prefix), \
                     patch.object(dependencies, 'discover', return_value={'fingerprint': {'compiler': 'pinned'}}) as discover:
                    self.assertEqual(dependencies.main(), 0)
                    self.assertEqual(discover.call_args.args[0], identity)


if __name__ == '__main__':
    unittest.main()
