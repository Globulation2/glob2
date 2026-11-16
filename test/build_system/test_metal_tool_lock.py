# SPDX-License-Identifier: GPL-3.0-or-later
"""Metal tool lock format, automatic lock choice, SPIRV-Cross provisioning and the workflows that rely on them."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('metal_tool_lock', ROOT / 'tools/shaders/metal_tool_lock.py')
lock_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lock_tool)
generator = lock_tool.generator
sys.path.insert(0, str(ROOT / 'buildsys'))
import renderer_shaders  # noqa: E402
import spirv_cross_tool  # noqa: E402
WORKFLOWS = ROOT / '.github/workflows'
PIN = generator.xcode_pin(ROOT)


class FakeXcode:
    """Answers the xcodebuild/xcrun queries compile_metal and the lock tool make."""

    def __init__(self, directory, xcode='Xcode ' + PIN + '\nBuild version 27B52', sdk_version='27.2'):
        self.directory, self.xcode, self.sdk_version = Path(directory), xcode, sdk_version
        self.tools = {name: self.directory / name for name in ('metal', 'metallib')}
        for name, path in self.tools.items():
            path.write_bytes(name.encode() + b' binary')
        self.calls = []

    def __call__(self, command, **kwargs):
        self.calls.append(command)
        if 'xcodebuild' in command:
            return self.xcode
        if '--show-sdk-version' in command:
            return self.sdk_version
        if '--find' in command:
            return str(self.tools[command[-1]])
        raise AssertionError(command)

    def sdk(self):
        return {'sdkVersion': self.sdk_version,
                'metalSha256': hashlib.sha256(self.tools['metal'].read_bytes()).hexdigest(),
                'metallibSha256': hashlib.sha256(self.tools['metallib'].read_bytes()).hexdigest()}

    def toolchain(self, sdks=('iphoneos', 'macosx')):
        return {'xcodeVersion': self.xcode, 'sdks': {sdk: self.sdk() for sdk in sdks}}


def run_tool(fake, *argv):
    out, err = io.StringIO(), io.StringIO()
    with patch.object(generator.subprocess, 'check_output', side_effect=fake), \
            patch.dict('os.environ', {'GITHUB_ACTIONS': ''}), \
            contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = lock_tool.main(list(argv))
    return code, out.getvalue(), err.getvalue()


def translated_bundle(directory):
    bundle = Path(directory) / 'shaders'
    bundle.mkdir()
    source = bundle / 'primitive.metal'
    source.write_text('vertex float4 main0() { return float4(0); }')
    receipt = {'path': source.name, 'entrypoint': 'main0', 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
    (bundle / 'metadata.json').write_text(json.dumps({
        'variants': ['primitive'], 'license': 'GPL-3.0-or-later',
        'nativeTranslationReceipt': {'metalSources': {'primitive': {'vertex': receipt, 'fragment': receipt}}}}))
    return bundle


def fake_metal(command, **kwargs):
    Path(command[-1]).write_bytes(b'MTLBmain0\0' if 'metallib' in command else b'air')


class LockToolTest(unittest.TestCase):
    def test_writes_one_toolchain_entry_for_every_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            output = Path(directory) / 'lock.json'
            code, stdout, _ = run_tool(fake, '--output', str(output))
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(output.read_text()), {'toolchains': [fake.toolchain()]})
            self.assertEqual(output.read_text(), stdout)
            self.assertIn(['xcrun', '--sdk', 'macosx', '--find', 'metal'], fake.calls)
            self.assertIn(['xcrun', '--sdk', 'iphoneos', '--find', 'metal'], fake.calls)

    def test_ios_needs_the_pinned_xcode_but_macos_runners_do_not(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory, xcode='Xcode 16.4\nBuild version 16F6')
            output = Path(directory) / 'lock.json'
            code, _, stderr = run_tool(fake, '--output', str(output))
            self.assertEqual(code, 1)
            self.assertIn('repository-pinned Xcode ' + PIN, stderr)
            self.assertEqual(run_tool(fake, '--sdk', 'macosx', '--output', str(output))[0], 0)
            self.assertEqual(json.loads(output.read_text()), {'toolchains': [fake.toolchain(('macosx',))]})

    def test_never_writes_the_committed_lock_in_place(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                run_tool(FakeXcode(directory), '--output', str(lock_tool.DEFAULT_LOCK))

    def test_check_accepts_any_listed_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            other = FakeXcode(Path(directory), xcode='Xcode 16.4\nBuild version 16F6', sdk_version='15.5')
            lock = Path(directory) / 'lock.json'
            lock.write_text(lock_tool.render({'toolchains': [other.toolchain(('macosx',)), fake.toolchain()]}))
            code, stdout, stderr = run_tool(fake, '--check', '--lock', str(lock))
            self.assertEqual((code, stderr), (0, ''))
            self.assertIn('lists', stdout)

    def test_check_reports_the_nearest_toolchain_and_the_entry_that_would_match(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            lock = Path(directory) / 'lock.json'
            lock.write_text(lock_tool.render({'toolchains': [fake.toolchain()]}))
            fake.sdk_version = '27.3'
            fake.tools['metal'].write_bytes(b'other metal')
            code, _, stderr = run_tool(fake, '--check', '--lock', str(lock))
            self.assertEqual(code, 1)
            for field in ('iphoneos.sdkVersion', 'macosx.metalSha256'):
                self.assertIn(field, stderr)
            self.assertIn('This toolchain entry would match', stderr)
            self.assertIn('apple-metal-tool-lock.yml', stderr)

    def test_check_rejects_unknown_fields_and_unreadable_locks(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            lock = Path(directory) / 'lock.json'
            for text in (json.dumps({'toolchains': [dict(fake.toolchain(), extra=1)]}), '{', json.dumps({'sdks': {}})):
                lock.write_text(text)
                self.assertEqual(run_tool(fake, '--check', '--lock', str(lock))[0], 1)
            self.assertEqual(run_tool(fake, '--check', '--lock', str(Path(directory) / 'missing.json'))[0], 2)

    def test_combine_merges_runner_entries_once_in_a_stable_order(self):
        with tempfile.TemporaryDirectory() as directory:
            first = FakeXcode(Path(directory))
            second = FakeXcode(Path(directory), xcode='Xcode 16.4\nBuild version 16F6', sdk_version='15.5')
            paths = []
            for index, lock in enumerate(({'toolchains': [first.toolchain()]},
                                          {'toolchains': [second.toolchain(('macosx',)), first.toolchain()]})):
                paths.append(Path(directory) / ('lock%d.json' % index))
                paths[-1].write_text(json.dumps(lock))
            code, stdout, _ = run_tool(first, '--combine', *map(str, paths))
            self.assertEqual(code, 0)
            combined = json.loads(stdout)['toolchains']
            self.assertEqual(len(combined), 2)
            self.assertEqual(stdout, lock_tool.render({'toolchains': list(reversed(combined))}))

    def test_committed_lock_lists_the_pinned_xcode_for_ios_and_macos(self):
        lock = json.loads((ROOT / generator.COMMITTED_METAL_TOOL_LOCK).read_text())
        self.assertEqual((ROOT / generator.COMMITTED_METAL_TOOL_LOCK).read_text(), lock_tool.render(lock))
        pinned = [toolchain for toolchain in lock['toolchains']
                  if toolchain['xcodeVersion'].splitlines()[0] == 'Xcode ' + PIN]
        self.assertTrue(pinned, 'no toolchain for the pinned Xcode ' + PIN)
        self.assertTrue(any({'iphoneos', 'macosx'} <= set(toolchain['sdks']) for toolchain in pinned))
        for toolchain in lock['toolchains']:
            for sdk, entry in toolchain['sdks'].items():
                self.assertIn(sdk, lock_tool.SDKS)
                self.assertEqual(list(entry), list(generator.METAL_SDK_KEYS))
                self.assertRegex(entry['metalSha256'], r'^[0-9a-f]{64}$')
                self.assertRegex(entry['metallibSha256'], r'^[0-9a-f]{64}$')


class CompileAndChoiceTest(unittest.TestCase):
    def test_compile_metal_takes_the_matching_toolchain_of_a_committed_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            other = FakeXcode(Path(directory) / '..', xcode='Xcode 16.4\nBuild version 16F6', sdk_version='15.5')
            bundle = translated_bundle(directory)
            lock = {'toolchains': [other.toolchain(('macosx',)), fake.toolchain()]}
            with patch.object(generator.subprocess, 'check_output', side_effect=fake), \
                    patch.object(generator.subprocess, 'run', side_effect=fake_metal):
                meta = generator.compile_metal(bundle, 'macosx', lock)
            receipt = meta['nativeTranslationReceipt']
            self.assertTrue(receipt['metalLibrariesProduced'])
            self.assertEqual(receipt['metalToolLockKind'], 'committed')
            self.assertEqual(receipt['metalToolLock'], {'xcodeVersion': fake.xcode, 'sdk': 'macosx', **fake.sdk()})

    def test_compile_metal_refuses_an_unlisted_compiler(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            bundle = translated_bundle(directory)
            lock = {'toolchains': [fake.toolchain()]}
            fake.tools['metallib'].write_bytes(b'tampered')
            with patch.object(generator.subprocess, 'check_output', side_effect=fake), \
                    patch.object(generator.subprocess, 'run', side_effect=fake_metal), \
                    self.assertRaisesRegex(ValueError, 'does not match verified lock: metallib'):
                generator.compile_metal(bundle, 'iphoneos', lock)

    def _choose(self, fake, lock, strict, directory):
        root = Path(directory) / 'root'
        (root / 'platforms/mobile').mkdir(parents=True, exist_ok=True)
        (root / 'buildsys').mkdir(parents=True, exist_ok=True)
        (root / 'platforms/mobile/toolchain.json').write_text(json.dumps({'ios': {'xcode': PIN}}))
        if lock is not None:
            (root / generator.COMMITTED_METAL_TOOL_LOCK).write_text(json.dumps(lock))
        err = io.StringIO()
        with patch.object(generator.subprocess, 'check_output', side_effect=fake), contextlib.redirect_stderr(err):
            result = renderer_shaders.choose_metal_lock(root, 'macosx', Path(directory) / 'build', strict,
                                                        generator=generator)
        return result, err.getvalue()

    def test_a_listed_toolchain_is_used_as_committed(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            (lock, kind), _ = self._choose(fake, {'toolchains': [fake.toolchain()]}, True, directory)
            self.assertEqual(kind, 'committed')
            self.assertEqual(lock['sdk'], 'macosx')

    def test_release_builds_refuse_an_unlisted_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            listed = FakeXcode(Path(directory) / '..', sdk_version='27.0')
            with self.assertRaisesRegex(ValueError, 'release builds compile Metal only with reviewed tools'):
                self._choose(fake, {'toolchains': [listed.toolchain()]}, True, directory)
            with self.assertRaisesRegex(ValueError, 'reviewed tools'):
                self._choose(fake, None, True, directory)

    def test_development_builds_record_their_own_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory, xcode='Xcode 99.0\nBuild version 99A1')
            (lock, kind), stderr = self._choose(fake, {'toolchains': []}, False, directory)
            self.assertEqual(kind, 'development')
            self.assertEqual(lock['xcodeVersion'], fake.xcode)
            record = Path(directory) / 'build/metal-development-lock-macosx.json'
            self.assertEqual(json.loads(record.read_text()), lock)
            self.assertIn('Release builds refuse', stderr)

    def test_strictness_comes_from_the_release_environment(self):
        self.assertFalse(renderer_shaders.require_committed_metal_lock({}))
        self.assertFalse(renderer_shaders.require_committed_metal_lock({'GLOB2_METAL_REQUIRE_COMMITTED_LOCK': '0'}))
        self.assertTrue(renderer_shaders.require_committed_metal_lock({'GLOB2_METAL_REQUIRE_COMMITTED_LOCK': '1'}))


class SpirvCrossProvisioningTest(unittest.TestCase):
    def test_pin_is_a_hash_verified_source_archive(self):
        pin = json.loads((ROOT / 'buildsys/spirv-cross.json').read_text())
        self.assertRegex(pin['source']['sha256'], r'^[0-9a-f]{64}$')
        self.assertTrue(pin['source']['url'].startswith('https://github.com/KhronosGroup/SPIRV-Cross/'))
        self.assertIn(pin['version'], pin['source']['url'])
        manifest = json.loads((ROOT / 'libs/gag/shaders/renderer-manifest.json').read_text())
        # The Ubuntu package version encodes the same SDK release.
        self.assertIn(pin['version'].removeprefix('sdk-'), manifest['nativeTranslationTools']['spirvCross']['packageVersion'])

    def test_only_a_receipted_build_of_the_pinned_source_is_trusted(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'bin/spirv-cross'
            binary.parent.mkdir()
            binary.write_bytes(b'spirv-cross')
            self.assertFalse(spirv_cross_tool.verified_build(binary))
            pin = json.loads((ROOT / 'buildsys/spirv-cross.json').read_text())
            receipt = {'source_sha256': pin['source']['sha256'], 'binary_sha256': hashlib.sha256(b'spirv-cross').hexdigest()}
            (Path(directory) / 'receipt.json').write_text(json.dumps(receipt))
            self.assertTrue(spirv_cross_tool.verified_build(binary))
            binary.write_bytes(b'tampered')
            self.assertFalse(spirv_cross_tool.verified_build(binary))


class WorkflowPolicyTest(unittest.TestCase):
    def job(self, workflow, name):
        text = (WORKFLOWS / workflow).read_text()
        match = re.search(r'^  ' + re.escape(name) + r':\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)', text, re.M | re.S)
        self.assertIsNotNone(match, workflow + ' has no job ' + name)
        return match.group(1)

    def test_macos_release_builds_require_the_committed_lock(self):
        for workflow, job in (('release.yml', 'macos-package'), ('mac-app-store.yml', 'build'),
                              ('gog-staging.yml', 'macos')):
            self.assertIn("GLOB2_METAL_REQUIRE_COMMITTED_LOCK: '1'", self.job(workflow, job), workflow)

    def test_lock_workflow_covers_every_release_runner(self):
        lock_workflow = (WORKFLOWS / 'apple-metal-tool-lock.yml').read_text()
        covered = set(re.findall(r'^          - runner: (\S+)$', lock_workflow, re.M))
        for workflow, job in (('release.yml', 'macos-package'), ('mac-app-store.yml', 'build'),
                              ('gog-staging.yml', 'macos'), ('ios-testflight.yml', 'build')):
            text = self.job(workflow, job)
            runners = set(re.findall(r'^\s+(?:- )?runner: (\S+)$', text, re.M))
            direct = re.search(r'^    runs-on: (\S+)$', text, re.M)
            if direct and not direct.group(1).startswith('$'):
                runners.add(direct.group(1))
            self.assertTrue(runners, workflow)
            self.assertLessEqual(runners, covered, workflow + ' builds on runners the lock workflow does not check')

    def test_lock_workflow_only_checks_and_uploads(self):
        text = (WORKFLOWS / 'apple-metal-tool-lock.yml').read_text()
        self.assertIn('contents: read', text)
        self.assertNotIn('secrets.', text)
        self.assertNotIn('git push', text)
        self.assertIn('metal_tool_lock.py $SDKS --check', text)
        self.assertIn('- buildsys/metal-tool-lock.json', text)

    def test_testflight_compiles_metal_on_the_mac_with_the_committed_lock(self):
        text = (WORKFLOWS / 'ios-testflight.yml').read_text()
        build = self.job('ios-testflight.yml', 'build')
        self.assertNotIn('GLOB2_RENDER_SHADER_INPUT', text)
        self.assertNotIn('spirv-cross=', text)
        self.assertIn('metal_tool_lock.py --check --sdk iphoneos', build)
        self.assertIn('GLOB2_METAL_REQUIRE_COMMITTED_LOCK=1', build)
        self.assertIn('--allow-missing-sdl-gpu', build)


if __name__ == '__main__':
    unittest.main()
