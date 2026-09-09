"""Mobile configuration isolation, independent of installed SDKs."""
import itertools
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scons'))
from build_layout import build_identity, default_directory, prepare_directory
from mobile_toolchain import discover


class EmulatorSmokeTests(unittest.TestCase):
    def test_smoke_rejects_physical_devices_and_shared_servers(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mobile'))
        import smoke
        for serial, port, avd in [('USB-device', 15037, 'glob2-api35-arm64'),
                                   ('emulator-5580', 5037, 'glob2-api35-arm64'),
                                   ('emulator-5580', 15037, 'personal-avd')]:
            with self.subTest(serial=serial, port=port, avd=avd), self.assertRaises(ValueError):
                smoke.validate_target(serial, port, avd)
        smoke.validate_target('emulator-5580', 15037, 'glob2-api35-x64')

    def test_extracted_emulator_is_registered_without_changing_licenses(self):
        import json
        import xml.etree.ElementTree as ET
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mobile'))
        import setup_tools
        root = Path(__file__).resolve().parents[2]
        lock = json.loads((root / 'mobile/emulator.json').read_text())
        scratch = root / 'build/test-profiles'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            destination = Path(temporary)
            properties = destination / 'source.properties'
            properties.write_text('Pkg.Revision=' + lock['emulator_revision'] + '\n')
            artifact = lock['archives']['Linux-x86_64']
            setup_tools.ensure_sdk_metadata(destination, artifact)
            package = ET.parse(destination / 'package.xml').getroot().find('localPackage')
            self.assertEqual(package.get('path'), 'emulator')
            self.assertFalse((destination / 'licenses').exists())
            setup_tools.ensure_sdk_metadata(destination, artifact)
            properties.write_text('Pkg.Revision=0.0.0\n')
            with self.assertRaisesRegex(ValueError, 'revision differs'):
                setup_tools.ensure_sdk_metadata(destination, artifact)

    def test_emulator_archives_match_configured_package_versions(self):
        import json
        root = Path(__file__).resolve().parents[2]
        lock = json.loads((root / 'mobile/emulator.json').read_text())
        for arch in ('arm64-v8a', 'x86_64'):
            artifact = lock['archives'][arch]
            self.assertEqual(artifact['directory'], f"android-sdk/system-images/android-{lock['api']}/{lock['tag']}/{arch}")
            self.assertIn(f"-{lock['api']}_r{int(lock['image_revision']):02}.zip", artifact['url'])
            self.assertEqual(len(artifact['sha1']), 40)


class DeveloperSigningTests(unittest.TestCase):
    def test_install_rejects_stale_or_modified_signed_apk(self):
        import hashlib
        import json
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'mobile'))
        import developer_apk
        scratch=Path(__file__).resolve().parents[2]/'build/test-profiles'
        scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            root=Path(temporary)
            directory=root/'app/build/outputs/apk/release';directory.mkdir(parents=True)
            unsigned=directory/'app-release-unsigned.apk'
            signed=directory/'app-release-development.apk'
            unsigned.write_bytes(b'original');signed.write_bytes(b'signed')
            signed.with_suffix('.json').write_text(json.dumps({
                'unsigned_sha256':hashlib.sha256(b'original').hexdigest(),
                'signed_sha256':hashlib.sha256(b'signed').hexdigest()}))
            with patch('developer_apk.subprocess.run') as run:
                for path in (unsigned,signed):
                    original=path.read_bytes();path.write_bytes(b'changed')
                    with self.assertRaisesRegex(ValueError,'stale or changed'):
                        developer_apk.verified(root,root,root)
                    path.write_bytes(original)
                run.assert_not_called()


class IOSCommandTests(unittest.TestCase):
    def test_incomplete_device_requests_fail_before_sdk_or_build(self):
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'mobile'))
        import ios
        cases = [
            (['build','--environment','device'], 'Device packaging needs'),
            (['configure','--environment','device'], 'Device packaging needs'),
            (['install'], '--device is required'),
            (['launch','--environment','device'], '--device is required'),
            (['install','--environment','device','--unsigned','--device','example'], 'cannot be installed'),
        ]
        with patch('ios.discover') as discover_sdk, patch('ios.subprocess.run') as run:
            for arguments,message in cases:
                with self.subTest(arguments=arguments), patch.object(sys,'argv',['ios.py']+arguments):
                    with self.assertRaisesRegex(ValueError,message):
                        ios.main()
            discover_sdk.assert_not_called()
            run.assert_not_called()

    def test_simulator_install_uses_task_device_set(self):
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'mobile'))
        import ios
        arguments=['ios.py','install','--device','test-udid','--simulator-set','build/test-simulators']
        with patch.object(sys,'argv',arguments), patch('ios.discover'), patch('ios.subprocess.run') as run:
            ios.main()
        command=run.call_args.args[0]
        self.assertEqual(command[:6],['xcrun','simctl','--set',str(Path('build/test-simulators').resolve()),'install','test-udid'])


class MobileBuildTests(unittest.TestCase):
    def test_each_mobile_configuration_has_its_own_directory(self):
        options = [dict(target='android', arch=arch, api=api, release=release)
            for arch, api, release in itertools.product(('arm64-v8a', 'armeabi-v7a', 'x86_64'), ('26','35'), ('0','1'))]
        options += [dict(target='ios', environment=env, deployment=api, release=release)
            for env, api, release in itertools.product(('device','simulator'), ('15.0','16.0'), ('0','1'))]
        paths = [default_directory(build_identity(args)) for args in options]
        self.assertEqual(len(paths), len(set(paths)))

    def test_wrong_architecture_cannot_reuse_output(self):
        with tempfile.TemporaryDirectory() as directory:
            prepare_directory(directory, build_identity({'target':'android'}))
            with self.assertRaises(ValueError):
                prepare_directory(directory, build_identity({'target':'android', 'arch':'armeabi-v7a'}))

    def test_invalid_targets_fail_without_probing_host(self):
        invalid = [dict(target='android', role='server'), dict(target='ios', mingw='1'),
            dict(target='android', arch='arm64'), dict(target='ios', arch='x86_64'),
            dict(target='android', api='25'), dict(target='android', api='../26'),
            dict(target='ios', deployment='14.0'), dict(target='ios', deployment='../15.0'),
            dict(target='android', environment='simulator'), dict(target='ios', profile='1')]
        for options in invalid:
            with self.subTest(options=options), self.assertRaises(ValueError):
                build_identity(options)

    def test_missing_android_sdk_does_not_fall_back_to_host(self):
        with tempfile.TemporaryDirectory() as directory, patch('mobile_toolchain.run') as run:
            with self.assertRaisesRegex(ValueError, 'Android NDK .* missing'):
                discover(build_identity({'target':'android'}), {'android_sdk':directory})
            run.assert_not_called()

    def test_ios_on_non_apple_host_has_actionable_error(self):
        with patch('mobile_toolchain.platform.system', return_value='Linux'):
            with self.assertRaisesRegex(ValueError, 'full Xcode'):
                discover(build_identity({'target':'ios'}), {})

class MobileArtifactTests(unittest.TestCase):
    def test_packaged_shared_libraries_require_aligned_load_segments(self):
        import struct
        from mobile_artifacts import verify_android_shared_library
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'libmain.so'
            for architecture,machine,bits in (('arm64-v8a',183,2),('x86_64',62,2),('armeabi-v7a',40,1)):
                data=bytearray(self.elf(machine,bits))
                struct.pack_into('<H',data,16,3)
                if bits==2:
                    struct.pack_into('<Q',data,32,64)
                    struct.pack_into('<HH',data,54,56,1)
                    data.extend(struct.pack('<II6Q',1,5,0,0,0,120,120,16384))
                    alignment_offset=112
                else:
                    struct.pack_into('<I',data,28,64)
                    struct.pack_into('<HH',data,42,32,1)
                    data.extend(struct.pack('<8I',1,0,0,0,96,96,5,4096))
                    alignment_offset=92
                path.write_bytes(data)
                verify_android_shared_library(path,architecture)
                path.write_bytes(data[:-1])
                with self.assertRaisesRegex(ValueError,'program header'):
                    verify_android_shared_library(path,architecture)
                struct.pack_into('<Q' if bits==2 else '<I',data,alignment_offset,4096 if bits==2 else 1024)
                path.write_bytes(data)
                with self.assertRaisesRegex(ValueError,'page aligned'):
                    verify_android_shared_library(path,architecture)

    @staticmethod
    def elf(machine, bits):
        import struct
        data=bytearray(64);data[:4]=b'\x7fELF';data[4]=bits;data[5]=1
        struct.pack_into('<H',data,18,machine)
        return bytes(data)

    def test_reject_mislabeled_dependency_before_linking(self):
        from mobile_artifacts import verify_android_library
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'libSDL2.so';path.write_bytes(self.elf(40,1))
            verify_android_library(path,'armeabi-v7a')
            with self.assertRaisesRegex(ValueError,'wrong architecture'):
                verify_android_library(path,'arm64-v8a')

    def test_checks_every_archive_member(self):
        from mobile_artifacts import verify_android_library
        def member(name,data):
            header=f'{name:<16}{0:<12}{0:<6}{0:<6}{644:<8}{len(data):<10}`\n'.encode()
            return header+data+(b'\n' if len(data)%2 else b'')
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'libmixed.a'
            path.write_bytes(b'!<arch>\n'+member('good.o/',self.elf(183,2))+member('bad.o/',self.elf(40,1)))
            with self.assertRaisesRegex(ValueError,'bad.o'):
                verify_android_library(path,'arm64-v8a')

    def test_rejects_host_archive_and_empty_archive(self):
        from mobile_artifacts import verify_android_library
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'libhost.a'
            for data in (b'!<arch>\n',b'not an archive'):
                path.write_bytes(data)
                with self.assertRaises(ValueError): verify_android_library(path,'arm64-v8a')
