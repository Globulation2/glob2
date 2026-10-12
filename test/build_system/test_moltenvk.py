# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned runtime packaging rejects corrupt and partially extracted caches."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('moltenvk', ROOT / 'scons/moltenvk.py')
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)


def archive_bytes(omit=None, corrupt_plist=False):
    result = io.BytesIO()
    entries = []
    for name, platform, variant in (('mac', 'macos', None), ('device', 'ios', None), ('sim', 'ios', 'simulator')):
        entry = {'LibraryIdentifier': name, 'LibraryPath': 'MoltenVK.framework', 'SupportedPlatform': platform}
        if variant:
            entry['SupportedPlatformVariant'] = variant
        entries.append(entry)
    files = {
        'MoltenVK/LICENSE': b'license',
        'MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/Info.plist': plistlib.dumps({'AvailableLibraries': entries}),
    }
    for name in ('mac', 'device', 'sim'):
        files[f'MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/{name}/MoltenVK.framework/MoltenVK'] = b'pinned binary'
    if omit:
        del files[omit]
    if corrupt_plist:
        files['MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/Info.plist'] = b'invalid plist'
    with tarfile.open(fileobj=result, mode='w') as output:
        for name, data in files.items():
            member = tarfile.TarInfo(name)
            member.size = len(data)
            output.addfile(member, io.BytesIO(data))
    return result.getvalue()


class MoltenVKTest(unittest.TestCase):
    def setup_lock(self, root, data=None):
        data = archive_bytes() if data is None else data
        lock = root / 'lock.json'
        receipt = {'url': 'https://example.test/runtime.tar', 'sha256': hashlib.sha256(data).hexdigest()}
        lock.write_text(json.dumps({'version': 'test', 'platforms': {key: receipt for key in ('macos', 'ios', 'ios-simulator')}}))
        return data, lock

    def test_selects_exact_platform_slice_and_reuses_verified_cache(self):
        for platform, environment, expected in (('macos', 'device', 'mac'), ('ios', 'device', 'device'), ('ios', 'simulator', 'sim')):
            with self.subTest(platform=platform, environment=environment), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                data, lock = self.setup_lock(root)
                with patch.object(runtime, 'LOCK', lock), patch.object(runtime.urllib.request, 'urlretrieve', side_effect=lambda url, path: Path(path).write_bytes(data)) as download:
                    framework, license_path = runtime.prepare(platform, root, environment)
                    self.assertEqual(framework.parent.name, expected)
                    self.assertEqual(license_path.read_bytes(), b'license')
                    self.assertEqual(runtime.prepare(platform, root, environment), (framework, license_path))
                    download.assert_called_once()

    def test_bad_download_never_becomes_cached(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _, lock = self.setup_lock(root)
            with patch.object(runtime, 'LOCK', lock), patch.object(runtime.urllib.request, 'urlretrieve', side_effect=lambda url, path: Path(path).write_bytes(b'partial')):
                with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                    runtime.prepare('macos', root)
            self.assertEqual(list((root / 'moltenvk-test-macos-device').iterdir()), [])

    def test_partial_extraction_is_not_published_and_can_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data, lock = self.setup_lock(root)
            def interrupt(source, path, **kwargs):
                Path(path).mkdir()
                (Path(path) / 'partial').write_bytes(b'partial')
                raise OSError('interrupted')
            with patch.object(runtime, 'LOCK', lock), patch.object(runtime.urllib.request, 'urlretrieve', side_effect=lambda url, path: Path(path).write_bytes(data)) as download:
                with patch.object(tarfile.TarFile, 'extractall', interrupt):
                    with self.assertRaisesRegex(OSError, 'interrupted'):
                        runtime.prepare('macos', root)
                cache = root / 'moltenvk-test-macos-device'
                self.assertEqual([path.name for path in cache.iterdir()], ['runtime.tar'])
                runtime.prepare('macos', root)
                download.assert_called_once()

    def test_corrupted_cache_is_rejected_without_network(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _, lock = self.setup_lock(root)
            cache = root / 'moltenvk-test-macos-device'
            cache.mkdir()
            (cache / 'runtime.tar').write_bytes(b'bad cached archive')
            with patch.object(runtime, 'LOCK', lock), patch.object(runtime.urllib.request, 'urlretrieve') as download:
                with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                    runtime.prepare('macos', root)
                download.assert_not_called()

    def test_invalid_platform_does_not_fetch_runtime(self):
        with patch.object(runtime.urllib.request, 'urlretrieve') as download:
            for platform, environment in (('android', 'device'), ('ios', 'unknown'), ('macos', 'simulator')):
                with self.subTest(platform=platform, environment=environment), self.assertRaises(ValueError):
                    runtime.prepare(platform, 'unused', environment)
            download.assert_not_called()

    def test_incomplete_or_malformed_package_is_not_published(self):
        binary = 'MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/mac/MoltenVK.framework/MoltenVK'
        for data in (archive_bytes(omit=binary), archive_bytes(omit='MoltenVK/LICENSE'), archive_bytes(corrupt_plist=True)):
            with self.subTest(), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                _, lock = self.setup_lock(root, data)
                with patch.object(runtime, 'LOCK', lock), patch.object(runtime.urllib.request, 'urlretrieve', side_effect=lambda url, path: Path(path).write_bytes(data)):
                    with self.assertRaises((ValueError, KeyError)):
                        runtime.prepare('macos', root)
                self.assertEqual([path.name for path in (root / 'moltenvk-test-macos-device').iterdir()], ['runtime.tar'])

    def test_extracted_binary_corruption_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data, lock = self.setup_lock(root)
            with patch.object(runtime, 'LOCK', lock), patch.object(runtime.urllib.request, 'urlretrieve', side_effect=lambda url, path: Path(path).write_bytes(data)) as download:
                framework, _ = runtime.prepare('macos', root)
                (framework / 'MoltenVK').write_bytes(b'damaged')
                with self.assertRaisesRegex(ValueError, 'differs from its verified archive'):
                    runtime.prepare('macos', root)
                download.assert_called_once()

    def test_unrequested_platform_variant_is_not_a_device_slice(self):
        with tempfile.TemporaryDirectory() as directory:
            extracted = Path(directory)
            with tarfile.open(fileobj=io.BytesIO(archive_bytes())) as source:
                source.extractall(extracted, filter='data')
            plist = extracted / 'MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/Info.plist'
            metadata = plistlib.loads(plist.read_bytes())
            for entry in metadata['AvailableLibraries']:
                if entry['LibraryIdentifier'] == 'device':
                    entry['SupportedPlatformVariant'] = 'maccatalyst'
            plist.write_bytes(plistlib.dumps(metadata))
            with self.assertRaisesRegex(ValueError, 'no ios/device slice'):
                runtime.select_slice(extracted, 'ios', 'device')


if __name__ == '__main__':
    unittest.main()
