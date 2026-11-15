# SPDX-License-Identifier: GPL-3.0-or-later
"""Metal tool lock format, check logic and the workflows that rely on it."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('metal_tool_lock', ROOT / 'tools/shaders/metal_tool_lock.py')
lock_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lock_tool)
WORKFLOWS = ROOT / '.github/workflows'
PIN = json.loads((ROOT / 'platforms/mobile/toolchain.json').read_text())['ios']['xcode']


class FakeXcode:
    """Answers the xcodebuild/xcrun queries compile_metal and the lock tool make."""

    def __init__(self, directory, xcode='Xcode ' + PIN + '\nBuild version 27A100', sdk_version='27.0'):
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

    def lock(self, sdk='iphoneos'):
        return {'xcodeVersion': self.xcode, 'sdk': sdk, 'sdkVersion': self.sdk_version,
                'metalSha256': hashlib.sha256(self.tools['metal'].read_bytes()).hexdigest(),
                'metallibSha256': hashlib.sha256(self.tools['metallib'].read_bytes()).hexdigest()}


def run_tool(fake, *argv):
    out, err = io.StringIO(), io.StringIO()
    with patch.object(lock_tool.generator.subprocess, 'check_output', side_effect=fake), \
            patch.dict('os.environ', {'GITHUB_ACTIONS': ''}), \
            contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = lock_tool.main(list(argv))
    return code, out.getvalue(), err.getvalue()


class LockToolTest(unittest.TestCase):
    def test_writes_exactly_the_compile_metal_format(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            output = Path(directory) / 'lock.json'
            code, stdout, _ = run_tool(fake, '--output', str(output))
            self.assertEqual(code, 0)
            written = json.loads(output.read_text())
            self.assertEqual(list(written), list(lock_tool.generator.METAL_TOOL_LOCK_KEYS))
            self.assertEqual(written, fake.lock())
            self.assertEqual(output.read_text(), stdout)
            self.assertTrue(output.read_text().endswith('}\n'))
            self.assertIn(['xcrun', '--sdk', 'iphoneos', '--find', 'metal'], fake.calls)

    def test_written_lock_is_accepted_by_compile_metal(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            output = Path(directory) / 'lock.json'
            self.assertEqual(run_tool(fake, '--output', str(output))[0], 0)
            bundle = Path(directory) / 'bundle'
            bundle.mkdir()
            source = bundle / 'primitive.metal'
            source.write_text('vertex float4 main0() { return float4(0); }')
            receipt = {'path': source.name, 'entrypoint': 'main0', 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
            (bundle / 'metadata.json').write_text(json.dumps({
                'variants': ['primitive'], 'license': 'GPL-3.0-or-later',
                'nativeTranslationReceipt': {'metalSources': {'primitive': {'vertex': receipt, 'fragment': receipt}}}}))
            def run(command, **kwargs):
                Path(command[-1]).write_bytes(b'MTLBmain0\0' if 'metallib' in command else b'air')
            generator = lock_tool.generator
            with patch.object(generator.subprocess, 'check_output', side_effect=fake), patch.object(generator.subprocess, 'run', side_effect=run):
                meta = generator.compile_metal(bundle, 'iphoneos', output)
            self.assertTrue(meta['nativeTranslationReceipt']['metalLibrariesProduced'])
            self.assertEqual(meta['nativeTranslationReceipt']['metalToolLock'], fake.lock())

    def test_refuses_an_xcode_other_than_the_pin(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory, xcode='Xcode 1.0\nBuild version 1A1')
            output = Path(directory) / 'lock.json'
            code, _, stderr = run_tool(fake, '--output', str(output))
            self.assertEqual(code, 1)
            self.assertFalse(output.exists())
            self.assertIn('repository-pinned Xcode ' + PIN, stderr)

    def test_never_writes_the_committed_lock_in_place(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                run_tool(FakeXcode(directory), '--output', str(lock_tool.DEFAULT_LOCK))

    def test_check_accepts_a_matching_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            lock = Path(directory) / 'lock.json'
            lock.write_text(lock_tool.render(fake.lock()))
            code, stdout, stderr = run_tool(fake, '--check', '--lock', str(lock))
            self.assertEqual((code, stderr), (0, ''))
            self.assertIn('matches', stdout)

    def test_check_reports_each_changed_field_with_the_remedy(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            lock = Path(directory) / 'lock.json'
            lock.write_text(lock_tool.render(fake.lock()))
            fake.sdk_version = '27.1'
            fake.tools['metal'].write_bytes(b'updated compiler')
            code, _, stderr = run_tool(fake, '--check', '--lock', str(lock))
            self.assertEqual(code, 1)
            self.assertIn('  sdkVersion:\n    locked:   27.0\n    observed: 27.1', stderr)
            self.assertIn('  metalSha256:', stderr)
            self.assertNotIn('  metallibSha256:', stderr)
            self.assertIn('-  "sdkVersion": "27.0"', stderr)
            self.assertIn('+  "sdkVersion": "27.1"', stderr)
            self.assertIn('.github/workflows/ios-metal-tool-lock.yml', stderr)
            self.assertIn('pull request', stderr)

    def test_check_rejects_unknown_fields_wrong_sdk_and_unreadable_locks(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            lock = Path(directory) / 'lock.json'
            for content, field in ((dict(fake.lock(), skip=True), 'skip'), (fake.lock('macosx'), 'sdk'),
                                   ('not json', '<unreadable>'), ([], '<lock>')):
                with self.subTest(field=field):
                    lock.write_text(content if isinstance(content, str) else json.dumps(content))
                    code, _, stderr = run_tool(fake, '--check', '--lock', str(lock))
                    self.assertEqual(code, 1)
                    self.assertIn('  ' + field + ':', stderr)

    def test_check_names_the_workflow_when_no_lock_is_committed(self):
        with tempfile.TemporaryDirectory() as directory:
            code, _, stderr = run_tool(FakeXcode(directory), '--check', '--lock', str(Path(directory) / 'absent.json'))
            self.assertEqual(code, 2)
            self.assertIn('.github/workflows/ios-metal-tool-lock.yml', stderr)

    def test_github_annotation_on_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            lock = Path(directory) / 'lock.json'
            lock.write_text(lock_tool.render(dict(fake.lock(), sdkVersion='old')))
            out = io.StringIO()
            with patch.object(lock_tool.generator.subprocess, 'check_output', side_effect=fake), \
                    patch.dict('os.environ', {'GITHUB_ACTIONS': 'true'}), \
                    contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(lock_tool.main(['--check', '--lock', str(lock)]), 1)
            self.assertTrue(out.getvalue().startswith('::error title=Metal tool lock::'))

    def test_compile_metal_rejects_extra_fields_with_the_remedy(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = FakeXcode(directory)
            lock = Path(directory) / 'lock.json'
            lock.write_text(json.dumps(dict(fake.lock(), allowUnverified=True)))
            generator = lock_tool.generator
            with patch.object(generator.subprocess, 'check_output', side_effect=fake), patch.object(generator.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'unexpected fields.*ios-metal-tool-lock.yml'):
                    generator.compile_metal(Path(directory), 'iphoneos', lock)
                run.assert_not_called()

    def test_committed_lock_if_present_has_the_exact_format(self):
        if not lock_tool.DEFAULT_LOCK.exists():
            self.skipTest('no committed Metal tool lock yet')
        text = lock_tool.DEFAULT_LOCK.read_text()
        lock = json.loads(text)
        self.assertEqual(text, lock_tool.render(lock))
        self.assertEqual(lock['sdk'], 'iphoneos')
        self.assertTrue(lock['xcodeVersion'].startswith('Xcode ' + PIN + '\n'))
        for key in ('metalSha256', 'metallibSha256'):
            self.assertRegex(lock[key], r'^[0-9a-f]{64}$')


def job(text, name):
    return re.split(r'\n  (?=[a-z][\w-]*:\n)', text.split('\n  ' + name + ':\n', 1)[1], maxsplit=1)[0]


class WorkflowPolicyTest(unittest.TestCase):
    def assert_pinned_actions(self, text):
        for action in re.findall(r'uses: (\S+)', text):
            if not action.startswith('./'):
                self.assertRegex(action, r'@[0-9a-f]{40}$')

    def test_lock_workflow_only_uploads_for_review(self):
        text = (WORKFLOWS / 'ios-metal-tool-lock.yml').read_text()
        on = text.split('\non:\n', 1)[1].split('\npermissions:', 1)[0]
        self.assertEqual(re.findall(r'^  ([a-z_]+):', on, re.M), ['workflow_dispatch'])
        self.assertIn('\npermissions:\n  contents: read\n\n', text)
        self.assertIn('    permissions:\n      contents: read\n', job(text, 'lock'))
        self.assertIn('!github.event.repository.fork', text)
        self.assertIn('runs-on: xcode-27', text)
        for forbidden in ('secrets.', 'pull_request_target', 'git push', 'git commit', 'environment:', 'write'):
            self.assertNotIn(forbidden, text)
        self.assertIn('persist-credentials: false', text)
        self.assertIn('metal_tool_lock.py --sdk iphoneos --output artifacts/', text)
        self.assertIn('actions/upload-artifact@', text)
        self.assert_pinned_actions(text)

    def test_testflight_compiles_metal_from_a_verified_bundle_and_the_committed_lock(self):
        text = (WORKFLOWS / 'ios-testflight.yml').read_text()
        self.assertNotIn('pull_request_target', text)
        self.assert_pinned_actions(text)
        shaders, build, upload = job(text, 'shaders'), job(text, 'build'), job(text, 'upload')
        for block in (shaders, build):
            self.assertNotIn('secrets.', block)
            self.assertNotIn('environment:', block)
            self.assertIn('    permissions:\n      contents: read\n', block)
            self.assertIn('ref: ${{ needs.credentials.outputs.commit }}', block)
        self.assertIn('runs-on: ubuntu-24.04', shaders)
        self.assertIn('./.github/actions/renderer-shader-tools', shaders)
        self.assertIn('--glslang "$GLOB2_GLSLANG" --spirv-cross', shaders)
        self.assertIn('--prebuilt "$out/renderer-shaders"', shaders)
        self.assertIn('needs: [credentials, shaders]', build)
        verify = build.split('Verify the shader bundle and the committed Metal tool lock', 1)[1].split('      - name:', 1)[0]
        self.assertIn('BUNDLE_SHA256: ${{ needs.shaders.outputs.bundle_sha256 }}', verify)
        self.assertIn('[[ "$BUNDLE_SHA256" =~ ^[0-9a-f]{64}$ ]]', verify)
        self.assertLess(verify.index('"$actual" != "$BUNDLE_SHA256"'), verify.index('tar -xzf'))
        self.assertLess(verify.index('--prebuilt "$input/renderer-shaders"'), verify.index('GLOB2_RENDER_SHADER_INPUT='))
        self.assertIn('metal_tool_lock.py --check --sdk iphoneos --lock platforms/mobile/metal-tool-lock.json', verify)
        self.assertIn('GLOB2_METAL_SDK=iphoneos', verify)
        self.assertIn('GLOB2_METAL_TOOL_LOCK=$GITHUB_WORKSPACE/platforms/mobile/metal-tool-lock.json', verify)
        self.assertIn('ios-metal-tool-lock.yml', verify)
        # The opt-out only applies to sources without a committed lock.
        compile_step = build.split('Compile game and generate Xcode project', 1)[1].split('      - name:', 1)[0]
        guarded = compile_step.split('if [ "$METAL_REQUIRED" != true ]; then', 1)[1].split('\n          fi\n          python3', 1)[0]
        self.assertEqual(compile_step.count('--allow-missing-sdl-gpu'), guarded.count('--allow-missing-sdl-gpu'))
        self.assertIn('ios-metal-tool-lock.yml', guarded)
        # Signing never touches shader input, and nothing overrides the lock.
        self.assertNotIn('ios-renderer-shaders', upload)
        self.assertNotIn('GLOB2_METAL', upload)
        for forbidden in ('metal-tool-lock.json >', 'metal_tool_lock.py --output', 'GLOB2_ALLOW', 'SKIP_METAL'):
            self.assertNotIn(forbidden, text)


if __name__ == '__main__':
    unittest.main()
