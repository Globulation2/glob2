"""Development policy and atomic dependency reuse contracts."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import types
import tempfile
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
import dev_build
import dev_store
import shared_dependencies
from build_layout import build_identity, default_directory


def fake_builder(prefix, work, **kwargs):
    prefix.mkdir(parents=True)
    (prefix / 'lib').mkdir()
    (prefix / 'lib/library.a').write_bytes(b'library')
    (prefix / 'library-manifest.json').write_text('{}')
    (prefix / 'library.pc').write_text('prefix=' + str(prefix))


class DevelopmentBuildTests(unittest.TestCase):
    def tearDown(self):
        dev_store.finish()

    def test_profiles_are_isolated_and_mobile_dependencies_reused(self):
        for target in ('native', 'web', 'android', 'ios'):
            normal = build_identity({'target': target})
            variants = [build_identity({'target': target, **args}) for args in
                        ({'dev_fast': 1}, {'linker': 'lld'}, {'dev_fast': 1, 'linker': 'lld'})]
            self.assertEqual(len({default_directory(i) for i in [normal, *variants]}), 4)
            for identity in variants:
                self.assertEqual(dev_build.dependency_identity(identity), dev_build.dependency_identity(normal))

    def test_invalid_profiles(self):
        for args in ({'pch': 1}, {'unity': 1}, {'release': 1, 'dev_fast': 1}, {'profile': 1, 'dev_fast': 1}, {'dev_fast': 'maybe'},
                     {'web_variant': 'serial'}, {'target': 'web', 'release': 1, 'web_variant': 'serial'},
                     {'dependency_jobs': 0}, {'linker': 'mold'}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                build_identity(args)

    def test_perf_tier_is_a_native_optimized_identity(self):
        screen, confirm = build_identity({'perf': 1}, host='linux'), build_identity({'perf': 1, 'opt': 3}, host='linux')
        self.assertEqual((screen['mode'], screen['opt'], confirm['opt']), ('perf', 2, 3))
        self.assertEqual(str(default_directory(screen)), 'build/linux/client/perf')
        self.assertEqual(str(default_directory(confirm)), 'build/linux/client/perf/o3')
        self.assertEqual(screen['client_profile'], build_identity({}, host='linux')['client_profile'])
        for args in ({'perf': 1, 'release': 1}, {'perf': 1, 'dev_fast': 1}, {'perf': 1, 'profile': 1},
                     {'perf': 1, 'target': 'web'}, {'opt': 3}, {'perf': 1, 'opt': 1}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                build_identity(args, host='linux')

    def test_dependency_configuration_skips_query_only_targets(self):
        script = types.ModuleType('SCons.Script')
        script.GetOption = lambda name: False
        script.COMMAND_LINE_TARGETS = []
        package = types.ModuleType('SCons')
        package.Script = script
        for targets, flags, expected in [([], (), True), (['compile_commands.json'], (), False),
                                       (['custom/compile_commands.json'], (), False),
                                       (['compile_commands.json', 'glob2'], (), True),
                                       ([], ('clean',), False), ([], ('no_exec',), False),
                                       ([], ('help',), False)]:
            with self.subTest(targets=targets, flags=flags), patch.dict(sys.modules, {'SCons': package, 'SCons.Script': script}), patch.object(script, 'COMMAND_LINE_TARGETS', targets), patch.object(script, 'GetOption', side_effect=lambda name: name in flags):
                self.assertEqual(dev_build.can_build_dependencies(), expected)

    def test_development_command_preserves_overrides(self):
        spec = importlib.util.spec_from_file_location('dev_command', ROOT / 'tools/dev_build.py')
        command = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(command)
        result = command.command(['target=web', 'linker=lld', '--build=custom', 'web-tests'])
        self.assertIn('web_variant=threaded', result)
        result = command.command(['dev_fast=0', '-j3', 'linker=default', 'CC=clang', 'engine-tests'])
        self.assertNotIn('dev_fast=1', result)
        self.assertNotIn('linker=auto', result)
        self.assertEqual(result.count('-j3'), 1)
        result = command.command(['perf=1', 'opt=3'])
        self.assertNotIn('dev_fast=1', result)
        self.assertNotIn('linker=auto', result)

    def test_cmake_command_targets_and_options(self):
        spec = importlib.util.spec_from_file_location('dev_command', ROOT / 'tools/dev_build.py')
        command = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(command)
        directory = Path(tempfile.mkdtemp())
        configure, build = command.cmake_commands(['--build=' + str(directory), '.', 'glob2', '-j12', '-k'])
        self.assertEqual(configure[:4], ['cmake', '-S', str(ROOT), '-B'])
        self.assertIn('-DGLOB2_BUILD_ARGS=dev_fast=1 linker=auto', configure)
        self.assertEqual(build, ['cmake', '--build', str(directory), '--target', 'glob2', 'tests',
                                 '--', '-j12', '-k', '0'])
        self.assertEqual(command.cmake_commands(['--build=' + str(directory)])[-1][-2:], ['--', '-j8'])
        # Each tier configures its own default directory, with no development defaults.
        perf = command.cmake_commands(['perf=1', 'opt=3', 'engine-tests'])
        self.assertIn('-DGLOB2_BUILD_ARGS=perf=1 opt=3', perf[0])
        self.assertEqual(perf[-1][2], str(ROOT / 'build/linux/client/perf/o3'))
        release = command.cmake_commands(['release=1', 'CXXFLAGS=-g -O1', 'build/linux/client/release/src/glob2'])
        self.assertIn('-DGLOB2_BUILD_ARGS=release=1', release[0])
        self.assertIn('-DGLOB2_CXXFLAGS=-g -O1', release[0])
        self.assertEqual(release[-1][3:5], ['--target', 'glob2'])
        relay = command.cmake_commands(['role=relay', 'release=1', 'relay'])
        self.assertEqual(relay[-1][2], str(ROOT / 'build/linux/relay/release'))
        self.assertEqual(command.compiler_environment(['CC=clang', 'CXX=clang++'], {})['CXX'], 'clang++')
        # `install` builds glob2 as a shipping build, then installs it where SCons would.
        staged = command.cmake_commands(['release=1', 'lean_images=1', 'BINDIR=/stage/usr/bin',
                                         'INSTALLDIR=/stage/usr/share', 'DATADIR=/usr/share', 'install'])
        for setting in ('-DGLOB2_SHIPPING=ON', '-DGLOB2_BINDIR=/stage/usr/bin',
                        '-DGLOB2_INSTALLDIR=/stage/usr/share', '-DGLOB2_DATADIR=/usr/share'):
            self.assertIn(setting, staged[0])
        self.assertEqual(staged[-2][2:5], [str(ROOT / 'build/linux/client/release/lean-images'), '--target', 'glob2'])
        self.assertEqual(staged[-1], ['cmake', '--install', str(ROOT / 'build/linux/client/release/lean-images')])
        self.assertIn('-n', command.cmake_commands(['--build=' + str(directory), '-n'])[-1])
        for unsupported in ('--weird', 'build/elsewhere/glob2'):
            with self.assertRaises(ValueError):
                command.cmake_commands([unsupported])

    def test_default_generator_is_cmake_only_for_requests_it_builds(self):
        spec = importlib.util.spec_from_file_location('dev_command', ROOT / 'tools/dev_build.py')
        command = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(command)
        def host(system='Linux', which='/usr/bin/tool', version='cmake version 3.28.3\n'):
            return (patch.object(command.platform, 'system', return_value=system),
                    patch.object(command.shutil, 'which', return_value=which),
                    patch.object(command.subprocess, 'run',
                                 return_value=subprocess.CompletedProcess([], 0, stdout=version)))
        system, which, run = host()
        with system, which, run:
            for arguments in ([], ['.'], ['engine-tests', 'unit-tests', '-j12', '-k'], ['perf=1'],
                              ['perf=1', 'opt=3', 'engine-tests'], ['release=1', 'CXX=g++-13'],
                              ['compile_profile=core', 'engine-tests'], ['--build=build/x'], ['-n'],
                              ['role=relay', 'release=1', 'relay'],
                              ['build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2']):
                self.assertIsNone(command.scons_reason(arguments), arguments)
            # Other targets and the options the CMake build does not cover stay on SCons.
            for arguments in (['target=web'], ['mingw=1', 'release=1'],
                              ['recording_codec=vp9'], ['--debug=explain']):
                self.assertIsNotNone(command.scons_reason(arguments), arguments)
        for overrides, expected in (({'version': 'cmake version 3.22.1\n'}, '3.25'),
                                    ({'which': None}, 'not installed'), ({'system': 'Darwin'}, 'Linux')):
            system, which, run = host(**overrides)
            with system, which, run:
                self.assertIn(expected, command.scons_reason([]))

    def test_shared_install_reuses_content_and_relocates_metadata(self):
        with tempfile.TemporaryDirectory() as task, patch.dict(os.environ, {'GLOB2_DEV_HOME': task, 'GLOB2_DEV_MODE': 'shared'}):
            with patch.object(shared_dependencies, 'fingerprint', return_value={'version': 1}):
                first = shared_dependencies.ensure(fake_builder, Path(task) / 'one', Path(task) / 'work')
                second = shared_dependencies.ensure(fake_builder, Path(task) / 'two', Path(task) / 'work')
                self.assertEqual(first, second)
                self.assertEqual((first / 'library.pc').read_text(), 'prefix=' + str(first))
                self.assertTrue(shared_dependencies.verify(first, {'version': 1}))
                report = dev_store.prune(budget=0)
                self.assertTrue(report['busy'])
                dev_store.finish()
                (first / 'lib/library.a').write_bytes(b'corrupted')
                repaired = shared_dependencies.ensure(fake_builder, Path(task) / 'two', Path(task) / 'work')
                self.assertEqual((repaired / 'lib/library.a').read_bytes(), b'library')
                dev_store.finish()

    def test_compatible_consumer_reuses_an_active_reader_installation(self):
        with tempfile.TemporaryDirectory() as task, patch.dict(os.environ, {'GLOB2_DEV_HOME': task, 'GLOB2_DEV_MODE': 'shared'}), patch.object(shared_dependencies, 'fingerprint', return_value={'version': 1}):
            prefix = shared_dependencies.ensure(fake_builder, Path(task) / 'one', Path(task) / 'work')
            code = '''import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import shared_dependencies as shared
shared.fingerprint = lambda builder, args: {'version': 1}
def forbidden(*args, **kwargs):
    raise RuntimeError('Compatible reader must not rebuild')
print(shared.ensure(forbidden, Path(sys.argv[2])/'two', Path(sys.argv[2])/'work'))
'''
            result = subprocess.run([sys.executable, '-c', code, str(ROOT / 'buildsys'), task], capture_output=True, text=True, timeout=5, check=True)
            self.assertEqual(result.stdout.strip(), str(prefix))

    def test_concurrent_consumers_publish_once(self):
        with tempfile.TemporaryDirectory() as task:
            environment = dict(os.environ, GLOB2_DEV_HOME=task, GLOB2_DEV_MODE='shared')
            code = """import sys,time
from pathlib import Path
sys.path.insert(0,sys.argv[1])
import shared_dependencies as shared
shared.fingerprint=lambda builder,args:{'version':1}
def build(prefix,work):
    with open(Path(sys.argv[2])/'count','a') as output: output.write('build\\n')
    time.sleep(.2)
    prefix.mkdir(parents=True)
    (prefix/'library-manifest.json').write_text('{}')
    (prefix/'library.a').write_bytes(b'archive')
shared.ensure(build,Path(sys.argv[2])/'local',Path(sys.argv[2])/'work')
"""
            processes = [subprocess.Popen([sys.executable, '-c', code, str(ROOT / 'buildsys'), task], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(2)]
            for process in processes:
                stdout, stderr = process.communicate(timeout=15)
                self.assertEqual(process.returncode, 0, stderr.decode())
            self.assertEqual((Path(task) / 'count').read_text().splitlines(), ['build'])

    def test_identity_serializes_path_and_tuple_inputs(self):
        with tempfile.TemporaryDirectory() as task, patch.dict(os.environ, {'GLOB2_DEV_HOME': task, 'GLOB2_DEV_MODE': 'shared'}), patch.object(shared_dependencies, 'fingerprint', return_value={'sdk': Path(task), 'flags': ('-pthread',)}):
            first = shared_dependencies.ensure(fake_builder, Path(task) / 'one', Path(task) / 'work')
            self.assertTrue(first.exists())
            dev_store.finish()

    def test_failed_publish_preserves_prior_install(self):
        with tempfile.TemporaryDirectory() as task, patch.dict(os.environ, {'GLOB2_DEV_HOME': task, 'GLOB2_DEV_MODE': 'shared'}), patch.object(shared_dependencies, 'fingerprint', return_value={'version': 1}):
            first = shared_dependencies.ensure(fake_builder, Path(task) / 'one', Path(task) / 'work')
            dev_store.finish()
            (first / 'lib/library.a').write_bytes(b'old')
            def failed(prefix, work):
                prefix.mkdir(parents=True)
                raise RuntimeError('interrupted')
            with self.assertRaises(RuntimeError):
                shared_dependencies.ensure(failed, Path(task) / 'one', Path(task) / 'work')
            self.assertEqual((first / 'lib/library.a').read_bytes(), b'old')

    def test_query_and_isolated_mode(self):
        with tempfile.TemporaryDirectory() as task, patch.dict(os.environ, {'GLOB2_DEV_HOME': task, 'GLOB2_DEV_MODE': 'shared'}), patch.object(shared_dependencies, 'fingerprint', return_value={'version': 1}):
            prefix = shared_dependencies.ensure(fake_builder, Path(task) / 'one', Path(task) / 'work', execute=False)
            self.assertFalse(prefix.exists())
            with patch.dict(os.environ, {'GLOB2_DEV_MODE': 'isolated'}):
                local = shared_dependencies.ensure(fake_builder, Path(task) / 'one', Path(task) / 'work')
                self.assertEqual(local, Path(task) / 'one')

    def test_configurable_budget_and_no_automatic_pruning(self):
        with patch.dict(os.environ, {'GLOB2_DEV_BUDGET_GIB': '48'}):
            self.assertEqual(dev_store.budget(), 48 * 1024**3)
        with patch.object(dev_store, '_prune_entries') as prune:
            dev_store.finish()
            prune.assert_not_called()


if __name__ == '__main__':
    unittest.main()
