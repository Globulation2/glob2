"""The experimental CMake build reads SCons' source manifests, never its own copy."""
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
import build_config  # noqa: E402
import export_cmake_sources  # noqa: E402
import sources  # noqa: E402


class CMakeSourcesTests(unittest.TestCase):
    def test_committed_file_is_current(self):
        committed = (ROOT / 'cmake/sources.cmake').read_text()
        self.assertEqual(committed, export_cmake_sources.content(),
                         'cmake/sources.cmake is stale; run python3 scons/export_cmake_sources.py')

    def test_check_mode_reports_staleness(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'sources.cmake'
            output.write_text('stale\n')
            self.assertEqual(export_cmake_sources.main(['--check', '--output', str(output)]), 1)
            self.assertEqual(export_cmake_sources.main(['--output', str(output)]), 0)
            self.assertEqual(export_cmake_sources.main(['--check', '--output', str(output)]), 0)

    def test_every_manifest_source_is_exported(self):
        text = export_cmake_sources.content()
        for name in sources.CLIENT_SOURCES:
            self.assertIn('    src/' + name + '\n', text)
        for name in sources.GAG_SOURCES:
            self.assertIn('    libs/gag/src/' + name + '\n', text)
        for name in sources.USL_SOURCES:
            self.assertIn('    libs/usl/src/' + name + '\n', text)
        for path in export_cmake_sources.javascript.openlibm_sources(ROOT):
            self.assertIn('    ' + path.relative_to(ROOT).as_posix() + '\n', text)

    def test_native_only_sources_match_src_sconscript(self):
        script = (ROOT / 'src/SConscript').read_text()
        added = re.search(r"source_files \+= \[('mcp/[^\]]*)\]", script)
        self.assertIsNotNone(added, 'src/SConscript no longer adds the MCP sources this way')
        self.assertEqual(tuple(re.findall(r"'([^']+)'", added.group(1))),
                         export_cmake_sources.NATIVE_CLIENT_SOURCES)

    def test_cmake_reads_the_generated_lists(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('include("${GLOB2_ROOT}/cmake/sources.cmake")', cmake)
        for name in ('GLOB2_CLIENT_SOURCES', 'GLOB2_GAG_SOURCES', 'GLOB2_USL_SOURCES',
                     'GLOB2_JAVASCRIPT_SOURCES', 'GLOB2_INCLUDE_DIRECTORIES', 'GLOB2_STRICT_NUMERIC_SOURCES'):
            self.assertRegex(cmake, r'\b' + name + r'\b')
        self.assertNotRegex(cmake, r'[\w/]+\.(cpp|c)\b', 'list sources in scons/sources.py, not CMakeLists.txt')

    def test_build_config_matches_sconstruct_rendering(self):
        """The CMake bridge renders BuildConfig.h through the same helpers as SConstruct."""
        bridge = importlib.util.spec_from_file_location('cmake_generate', ROOT / 'scons/cmake_generate.py')
        module = importlib.util.module_from_spec(bridge)
        bridge.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            module.main(['build-config', '--builddir', directory, '--opengl', '--fribidi'])
            text = (Path(directory) / 'include/glob2/BuildConfig.h').read_text()
        expected = build_config.ConfigurationText()
        build_config.add_package(expected, datadir='/usr/local/share', version=module.PACKAGE_VERSION,
                                 font='sans.ttf', darwin=False, windows=False, china=False,
                                 identity=module.build_identity({}))
        build_config.add_native_wss(expected)
        build_config.add_opengl(expected)
        build_config.add_fribidi(expected)
        self.assertEqual(text, expected.text())
        self.assertIn('#define HAVE_OPENGL  \n', text)
        sconstruct = (ROOT / 'SConstruct').read_text()
        for helper in ('add_package(configfile', 'add_native_wss(configfile)', 'add_opengl(configfile)',
                       'add_fribidi(configfile)'):
            self.assertIn(helper, sconstruct)

    def test_export_runs_without_scons(self):
        # The generator must not import SCons (scons/javascript.py imports it lazily).
        result = subprocess.run([sys.executable, '-c', 'import runpy, sys\n'
                                 'sys.modules["SCons"] = None\n'
                                 'sys.argv = ["export_cmake_sources.py", "--check"]\n'
                                 'runpy.run_path("scons/export_cmake_sources.py", run_name="__main__")'],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
