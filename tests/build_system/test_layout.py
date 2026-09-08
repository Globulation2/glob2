import itertools
import json
from pathlib import Path
import sys
import tempfile
import unittest
import subprocess
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scons'))
from build_layout import build_identity, default_directory, prepare_directory, write_if_changed, BuildLock
from sources import CLIENT_SOURCES, SERVER_SOURCES, GAG_SOURCES, USL_SOURCES


class BuildLayoutTests(unittest.TestCase):
    def test_all_supported_configurations_have_distinct_directories(self):
        configurations = []
        for release, host in itertools.product(('0','1'), ('darwin','linux','windows')):
            for role in ('client','server','router','gateway'):
                configurations.append(build_identity({'release':release,'role':role}, host))
        configurations += [build_identity({'target':'web','release':release}) for release in ('0','1')]
        directories = [default_directory(c) for c in configurations]
        self.assertEqual(len(directories), len(set(directories)))

    def test_legacy_server_and_mingw_options_select_separate_identities(self):
        self.assertEqual(build_identity({'server':'1'})['role'], 'server')
        self.assertEqual(build_identity({'mingwcross':'1'})['toolchain'], 'mingwcross')
        self.assertEqual(build_identity({})['role'], 'client')

    def test_incompatible_explicit_directory_is_rejected(self):
        with tempfile.TemporaryDirectory() as path:
            native = build_identity({})
            prepare_directory(path,native)
            prepare_directory(path,native)
            with self.assertRaises(ValueError):
                prepare_directory(path,build_identity({'target':'web'}))
            self.assertEqual(json.loads((Path(path)/'identity.json').read_text()),native)

    def test_invalid_combinations_fail_before_toolchain_initialization(self):
        for args in ({'target':'unknown'}, {'target':'web','server':'1'}, {'target':'web','mingwcross':'1'}, {'role':'unknown'}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                build_identity(args)

    def test_identical_generated_header_does_not_invalidate_objects(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'include/glob2/BuildConfig.h'
            write_if_changed(path,'#pragma once\n')
            stamp=path.stat().st_mtime_ns
            write_if_changed(path,'#pragma once\n')
            self.assertEqual(stamp,path.stat().st_mtime_ns)
            write_if_changed(path,'#pragma once\n#define CHANGED 1\n')
            self.assertIn('CHANGED',path.read_text())

    def test_same_identity_cannot_have_two_concurrent_writers(self):
        with tempfile.TemporaryDirectory() as directory:
            with BuildLock(directory):
                code = "import sys; sys.path.insert(0,sys.argv[1]); from build_layout import BuildLock; BuildLock(sys.argv[2])"
                process = subprocess.run([sys.executable, '-c', code, str(Path(__file__).resolve().parents[2]/'scons'), directory], capture_output=True, text=True)
                self.assertNotEqual(process.returncode,0)
                self.assertIn('Another build is using',process.stderr)
            with BuildLock(directory):
                pass

    def test_shared_manifests_are_valid_and_unique(self):
        root=Path(__file__).resolve().parents[2]
        for prefix,files in [('src',CLIENT_SOURCES),('src',SERVER_SOURCES),('libgag/src',GAG_SOURCES),('libusl/src',USL_SOURCES)]:
            self.assertEqual(len(files),len(set(files)))
            for filename in files:
                self.assertTrue((root/prefix/filename).is_file(),filename)

if __name__ == '__main__':
    unittest.main()
