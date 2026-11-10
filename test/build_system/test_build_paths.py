"""tools/build_paths.py finds the newest native client build, CMake's included."""
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
        self.scons = self.root / 'build' / toolchain / 'client/debug/dev-x'
        self.perf = self.root / 'build' / toolchain / 'client/perf/o3'
        self.cmake = self.root / 'build/cmake/dev'
        patches = [mock.patch.object(build_paths, 'ROOT', self.root),
                   mock.patch.object(build_paths, 'CMAKE_DEV', self.cmake),
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

    def test_newer_cmake_build_wins(self):
        self.build(self.scons, 1000)
        self.build(self.cmake, 2000)
        self.assertEqual(build_paths.native_build_directory(), self.cmake)

    def test_newer_scons_build_wins(self):
        self.build(self.cmake, 1000)
        self.build(self.scons, 2000)
        self.assertEqual(build_paths.native_build_directory(), self.scons)

    def test_cmake_dev_build_is_not_optimized(self):
        self.build(self.perf, 1000)
        self.build(self.cmake, 2000)
        binary, optimized = build_paths.study_binary()
        self.assertEqual(binary.parent.parent, self.cmake)
        self.assertFalse(optimized)


if __name__ == '__main__':
    unittest.main()
