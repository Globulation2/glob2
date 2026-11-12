"""The CMake build reads the shared manifests (buildsys/sources.py, test/tests.py,
buildsys/harness_programs.py) through generated lists, never its own copy."""
import contextlib
import importlib
import importlib.util
import io
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
import build_config  # noqa: E402
import export_cmake_sources  # noqa: E402
import harness_programs  # noqa: E402
import sources  # noqa: E402

registry = export_cmake_sources.registry


class CMakeSourcesTests(unittest.TestCase):
    def test_committed_files_are_current(self):
        for path, text in export_cmake_sources.outputs().items():
            with self.subTest(path=path.name):
                self.assertEqual(path.read_text(), text,
                                 f'cmake/{path.name} is stale; run python3 buildsys/export_cmake_sources.py')

    def test_check_mode_reports_staleness(self):
        with tempfile.TemporaryDirectory() as directory:
            arguments = ['--directory', directory]
            self.assertEqual(export_cmake_sources.main(['--check', *arguments]), 1)
            self.assertEqual(export_cmake_sources.main(arguments), 0)
            self.assertEqual(export_cmake_sources.main(['--check', *arguments]), 0)
            (Path(directory) / 'tests.cmake').write_text('stale\n')
            self.assertEqual(export_cmake_sources.main(['--check', *arguments]), 1)

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

    def test_every_registry_entry_is_exported(self):
        text = export_cmake_sources.tests_content()
        for table in ('SUPPORT', 'ENGINE_SUPPORT', 'ENGINE_TESTS', 'UNIT_STUBS', 'UNIT_TESTS',
                      'UNIT_PRODUCTION_SOURCES', 'MAP_TESTS', 'MAP_STUBS', 'MAP_PRODUCTION_SOURCES', 'SIM_TESTS',
                      'SIM_SUPPORT', 'SIM_STUBS', 'SIM_PRODUCTION_SOURCES'):
            for entry in getattr(registry, table):
                source = entry if isinstance(entry, str) else entry[0]
                with self.subTest(table=table, source=source):
                    self.assertIn('    ' + registry.source_path(source) + '\n', text)
        for target, source, alias, group in registry.PROGRAMS:
            self.assertIn(f'set(GLOB2_TEST_PROGRAM_{target}_SOURCE {registry.source_path(source)})', text)
        for program in harness_programs.HARNESS_PROGRAMS:
            self.assertIn(f'set(GLOB2_HARNESS_{program.target}_SOURCE {program.source.lstrip("#")})', text)

    def test_entry_options_reach_their_group(self):
        """Each test entry lands in a group carrying exactly its own cxxflags."""
        text = export_cmake_sources.tests_content()
        block = r'set\((GLOB2_\w+_TESTS_\d+)_{}\n((?:    .*\n)*)\)'
        groups = dict(re.findall(block.format('SOURCES'), text))
        flags = dict(re.findall(block.format('CXXFLAGS'), text))
        for table in ('ENGINE_TESTS', 'UNIT_TESTS', 'MAP_TESTS', 'SIM_TESTS'):
            binary = 'GLOB2_' + table
            for entry in getattr(registry, table):
                source, options = (entry, {}) if isinstance(entry, str) else entry
                path = registry.source_path(source)
                group = next(name for name, paths in groups.items()
                             if name.startswith(binary + '_') and '    ' + path + '\n' in paths)
                with self.subTest(source=source):
                    self.assertEqual(flags[group].split(), list(options.get('cxxflags', ())))

    def test_harness_programs_name_real_sources(self):
        for program in harness_programs.HARNESS_PROGRAMS:
            self.assertTrue((ROOT / program.source.lstrip('#')).is_file(), program.source)
            if not isinstance(program.link, str):
                for name in program.link:
                    self.assertIn(name, sources.CLIENT_SOURCES)

    def test_cmake_reads_the_generated_lists(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('include("${GLOB2_ROOT}/cmake/sources.cmake")', cmake)
        self.assertIn('include("${GLOB2_ROOT}/cmake/tests.cmake")', cmake)
        for name in ('GLOB2_CLIENT_SOURCES', 'GLOB2_GAG_SOURCES', 'GLOB2_USL_SOURCES',
                     'GLOB2_JAVASCRIPT_SOURCES', 'GLOB2_INCLUDE_DIRECTORIES', 'GLOB2_STRICT_NUMERIC_SOURCES',
                     'GLOB2_TEST_BINARIES', 'GLOB2_TEST_PROGRAMS', 'GLOB2_HARNESS_PROGRAMS',
                     'GLOB2_TEST_LEGACY_ALIASES'):
            self.assertRegex(cmake, r'\b' + name + r'\b')
        self.assertIn('foreach(binary IN LISTS GLOB2_TEST_BINARIES)', cmake)
        self.assertIn('glob2_test_objects(unused TEST_PRODUCTION', cmake)
        self.assertIn('glob2_objects_of(module_objects glob2_client ${GLOB2_${upper}_SOURCES})', cmake)
        self.assertNotRegex(cmake, r'[\w/]+\.(cpp|c)\b', 'list sources in buildsys/sources.py, not CMakeLists.txt')

    def test_build_config_renders_through_the_shared_helpers(self):
        """The bridge renders BuildConfig.h through buildsys/build_config.py."""
        bridge = importlib.util.spec_from_file_location('cmake_generate', ROOT / 'buildsys/cmake_generate.py')
        module = importlib.util.module_from_spec(bridge)
        bridge.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            module.main(['build-config', '--builddir', directory, '--opengl', '--fribidi'])
            text = (Path(directory) / 'include/glob2/BuildConfig.h').read_text()
        expected = build_config.ConfigurationText()
        identity = module.build_identity({})
        build_config.add_package(expected, datadir='/usr/local/share', version=module.PACKAGE_VERSION,
                                 font='sans.ttf', darwin=identity['toolchain'] == 'darwin',
                                 windows=identity['toolchain'] == 'mingw', china=False, identity=identity)
        build_config.add_native_wss(expected)
        build_config.add_opengl(expected)
        build_config.add_fribidi(expected)
        self.assertEqual(text, expected.text())
        self.assertIn('#define HAVE_OPENGL  \n', text)

    def test_bridge_identity_is_the_build_layout_identity(self):
        bridge = importlib.util.spec_from_file_location('cmake_generate', ROOT / 'buildsys/cmake_generate.py')
        module = importlib.util.module_from_spec(bridge)
        bridge.loader.exec_module(module)
        import json
        for arguments in (['dev_fast=1', 'linker=auto'], ['perf=1', 'opt=3'], ['release=1'], ['role=relay', 'release=1']):
            with self.subTest(arguments=arguments), tempfile.TemporaryDirectory() as directory:
                with contextlib.redirect_stdout(io.StringIO()) as printed:
                    module.main(['configure', '--builddir', directory, *('--arg=' + word for word in arguments)])
                identity = json.loads((Path(directory) / 'identity.json').read_text())
                self.assertEqual(identity.pop('generator'), 'cmake')
                expected = module.build_identity(dict(word.split('=', 1) for word in arguments))
                self.assertEqual(identity, expected)
                self.assertEqual(json.loads(printed.getvalue())['directory'],
                                 (ROOT / module.default_directory(expected)).as_posix())

    def test_nothing_imports_scons(self):
        for path in sorted((ROOT / 'buildsys').glob('*.py')) + sorted((ROOT / 'tools').glob('*.py')):
            with self.subTest(path=path.name):
                self.assertNotRegex(path.read_text(), r'(?m)^\s*(from|import) SCons\b')


if __name__ == '__main__':
    unittest.main()
