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
sys.path.insert(0, str(ROOT / 'scons'))
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
                        ({'dev_fast': 1}, {'pch': 1}, {'unity': 1}, {'pch': 1, 'unity': 1})]
            self.assertEqual(len({default_directory(i) for i in [normal, *variants]}), 5)
            for identity in variants:
                self.assertEqual(dev_build.dependency_identity(identity), normal)

    def test_invalid_profiles(self):
        for args in ({'release': 1, 'pch': 1}, {'profile': 1, 'dev_fast': 1}, {'unity': 'maybe'},
                     {'web_variant': 'serial'}, {'target': 'web', 'release': 1, 'web_variant': 'serial'},
                     {'dependency_jobs': 0}, {'linker': 'mold'}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                build_identity(args)

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
        result = command.command(['target=web', 'pch=1', '--build=custom', 'web-tests'])
        self.assertIn('web_variant=threaded', result)
        result = command.command(['dev_fast=0', '-j3', 'linker=default', 'CC=clang', 'engine-tests'])
        self.assertNotIn('dev_fast=1', result)
        self.assertNotIn('linker=auto', result)
        self.assertEqual(result.count('-j3'), 1)

    def test_benchmark_snapshot_retains_git_provenance_and_candidate_edits(self):
        spec = importlib.util.spec_from_file_location('benchmark_command', ROOT / 'tools/benchmark_build.py')
        command = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(command)
        with tempfile.TemporaryDirectory() as task:
            source, destination = Path(task) / 'source', Path(task) / 'snapshot'
            source.mkdir()
            destination.mkdir()
            def git(*args):
                return subprocess.check_output(['git', '-C', str(source), *args], stderr=subprocess.DEVNULL, text=True)
            git('init')
            (source / 'SConstruct').write_text('baseline')
            git('add', 'SConstruct')
            git('-c', 'user.name=Build fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture')
            (source / 'SConstruct').write_text('candidate')
            with patch.object(command, 'ROOT', source):
                command.snapshot(destination)
            self.assertEqual((destination / 'SConstruct').read_text(), 'candidate')
            self.assertEqual(subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'HEAD'], text=True), git('rev-parse', 'HEAD'))
            self.assertIn('M SConstruct', subprocess.check_output(['git', '-C', str(destination), 'status', '--porcelain'], text=True))

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
            result = subprocess.run([sys.executable, '-c', code, str(ROOT / 'scons'), task], capture_output=True, text=True, timeout=5, check=True)
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
            processes = [subprocess.Popen([sys.executable, '-c', code, str(ROOT / 'scons'), task], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(2)]
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
