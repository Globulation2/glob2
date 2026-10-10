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
from build_layout import build_identity
from dev_build import dependency_identity


class MobileDependencyManifestTests(unittest.TestCase):
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
