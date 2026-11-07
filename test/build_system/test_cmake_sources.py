"""The experimental CMake build reads SCons' manifests, never its own copy."""
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
import harness_programs  # noqa: E402
import sources  # noqa: E402

registry = export_cmake_sources.registry


class CMakeSourcesTests(unittest.TestCase):
    def test_committed_files_are_current(self):
        for path, text in export_cmake_sources.outputs().items():
            with self.subTest(path=path.name):
                self.assertEqual(path.read_text(), text,
                                 f'cmake/{path.name} is stale; run python3 scons/export_cmake_sources.py')

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
                      'UNIT_PRODUCTION_SOURCES'):
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
        for table, binary in (('ENGINE_TESTS', 'GLOB2_ENGINE_TESTS'), ('UNIT_TESTS', 'GLOB2_UNIT_TESTS')):
            for entry in getattr(registry, table):
                source, options = (entry, {}) if isinstance(entry, str) else entry
                path = registry.source_path(source)
                group = next(name for name, paths in groups.items()
                             if name.startswith(binary + '_') and '    ' + path + '\n' in paths)
                with self.subTest(source=source):
                    self.assertEqual(flags[group].split(), list(options.get('cxxflags', ())))

    def test_harness_programs_come_from_the_shared_table(self):
        script = (ROOT / 'src/SConscript').read_text()
        self.assertIn('from harness_programs import HARNESS_PROGRAMS', script)
        tail = script.split('# Harness and tool programs', 1)[1]
        self.assertNotIn("development_program('", tail, 'add harness programs to scons/harness_programs.py')
        for program in harness_programs.HARNESS_PROGRAMS:
            self.assertTrue((ROOT / program.source.lstrip('#')).is_file(), program.source)
            if not isinstance(program.link, str):
                for name in program.link:
                    self.assertIn(name, sources.CLIENT_SOURCES)

    def test_native_only_sources_match_src_sconscript(self):
        script = (ROOT / 'src/SConscript').read_text()
        added = re.search(r"source_files \+= \[('mcp/[^\]]*)\]", script)
        self.assertIsNotNone(added, 'src/SConscript no longer adds the MCP sources this way')
        self.assertEqual(tuple(re.findall(r"'([^']+)'", added.group(1))),
                         export_cmake_sources.NATIVE_CLIENT_SOURCES)

    def test_cmake_reads_the_generated_lists(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('include("${GLOB2_ROOT}/cmake/sources.cmake")', cmake)
        self.assertIn('include("${GLOB2_ROOT}/cmake/tests.cmake")', cmake)
        for name in ('GLOB2_CLIENT_SOURCES', 'GLOB2_GAG_SOURCES', 'GLOB2_USL_SOURCES',
                     'GLOB2_JAVASCRIPT_SOURCES', 'GLOB2_INCLUDE_DIRECTORIES', 'GLOB2_STRICT_NUMERIC_SOURCES',
                     'GLOB2_UNIT_TESTS_REUSED_CLIENT_SOURCES', 'GLOB2_TEST_PROGRAMS', 'GLOB2_HARNESS_PROGRAMS',
                     'GLOB2_TEST_LEGACY_ALIASES'):
            self.assertRegex(cmake, r'\b' + name + r'\b')
        self.assertIn('glob2_test_objects(engine_objects ENGINE_TESTS', cmake)
        self.assertIn('glob2_test_objects(unit_objects UNIT_TESTS', cmake)
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

    def test_bridge_identity_is_the_scons_dev_identity(self):
        bridge = importlib.util.spec_from_file_location('cmake_generate', ROOT / 'scons/cmake_generate.py')
        module = importlib.util.module_from_spec(bridge)
        bridge.loader.exec_module(module)
        import json
        with tempfile.TemporaryDirectory() as directory:
            module.main(['identity', '--builddir', directory])
            identity = json.loads((Path(directory) / 'identity.json').read_text())
        self.assertEqual(identity.pop('generator'), 'cmake')
        self.assertEqual(identity, module.build_identity(module.DEV_IDENTITY))
        self.assertEqual(identity['mode'], 'debug')

    def test_export_runs_without_scons(self):
        # The generator must not import SCons (scons/javascript.py imports it lazily).
        result = subprocess.run([sys.executable, '-c', 'import runpy, sys\n'
                                 'sys.modules["SCons"] = None\n'
                                 'sys.argv = ["export_cmake_sources.py", "--check"]\n'
                                 'runpy.run_path("scons/export_cmake_sources.py", run_name="__main__")'],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_bridge_modules_import_without_scons(self):
        # The CMake build calls the provenance and asset export code without SCons installed.
        for module in ('test_provenance', 'runtime_assets', 'shared_assets', 'harness_programs'):
            result = subprocess.run([sys.executable, '-c', 'import sys\n'
                                     'sys.modules["SCons"] = None\n'
                                     'sys.path.insert(0, "scons")\n'
                                     f'import {module}'], cwd=ROOT, capture_output=True, text=True)
            with self.subTest(module=module):
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
