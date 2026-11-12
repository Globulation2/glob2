"""tools/build_paths.py finds the newest native client build."""
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import build_paths  # noqa: E402


class BuildPathsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        toolchain = 'mingw' if os.name == 'nt' else build_paths.platform.system().lower()
        client = self.root / 'build' / toolchain / 'client'
        self.dev = client / 'debug/dev-dev_fast-true-linker-auto'
        self.core = client / 'debug/dev-dev_fast-true-linker-auto-compile_profile-core'
        self.release = client / 'release'
        self.perf = client / 'perf/o3'
        patches = [mock.patch.object(build_paths, 'ROOT', self.root),
                   mock.patch.dict(os.environ, {'GLOB2_BUILD_DIR': ''})]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        self.addCleanup(self.temp.cleanup)

    def build(self, directory, when):
        binary = directory / 'src' / ('glob2' + build_paths.EXECUTABLE)
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.touch()
        os.utime(binary, (when, when))

    def test_newest_dev_build_wins(self):
        self.build(self.release, 1000)
        self.build(self.dev, 2000)
        self.build(self.core, 1500)
        self.assertEqual(build_paths.native_build_directory(), self.dev)

    def test_newer_release_build_wins(self):
        self.build(self.dev, 1000)
        self.build(self.release, 2000)
        self.assertEqual(build_paths.native_build_directory(), self.release)

    def test_dev_build_is_not_optimized(self):
        self.build(self.perf, 1000)
        self.build(self.dev, 2000)
        binary, optimized = build_paths.study_binary()
        self.assertEqual(binary.parent.parent, self.dev)
        self.assertFalse(optimized)
        self.build(self.perf, 3000)
        self.assertTrue(build_paths.study_binary()[1])


if __name__ == '__main__':
    unittest.main()
