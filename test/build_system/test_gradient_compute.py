# SPDX-License-Identifier: GPL-3.0-or-later
"""The shipped compute bundle must remain tied to every pinned input."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('gradient_compute', ROOT / 'tools/shaders/gradient_compute.py')
shader = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shader)


class GradientComputeTest(unittest.TestCase):
    def test_translation_normalization_is_idempotent_and_preserves_tokens(self):
        source = '@group(0) @binding(4) \nvar<storage, read> params: Params;\t\n\n'
        expected = '@group(0) @binding(4)\nvar<storage, read> params: Params;\n\n'
        self.assertEqual(shader.normalized_translation(source), expected)
        self.assertEqual(shader.normalized_translation(expected), expected)

    def copy_bundle(self, root):
        for relative in (*shader.BUNDLE_FILES, 'src/field/shaders/manifest.json',
                         *(f'third_party/vulkan/{name}' for name in shader.VULKAN_FILES),
                         'third_party/vulkan/manifest.json'):
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)

    def mutate_manifest(self, root, relative, mutate):
        path = root / relative
        manifest = json.loads(path.read_text())
        mutate(manifest)
        path.write_text(json.dumps(manifest))

    def test_current_bundle_is_complete(self):
        self.assertEqual(set(shader.verify()['files']), shader.BUNDLE_FILES)

    def test_windows_checkout_preserves_receipted_bytes(self):
        # Exercise Git's real checkout conversion with the Windows default.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.copy_bundle(root)
            shutil.copyfile(ROOT / '.gitattributes', root / '.gitattributes')
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            subprocess.run(['git', '-C', str(root), '-c', 'core.autocrlf=true', 'add', '.'],
                           check=True, capture_output=True)
            for relative in (*shader.BUNDLE_FILES,
                             *(f'third_party/vulkan/{name}' for name in shader.VULKAN_FILES)):
                (root / relative).unlink()
            subprocess.run(['git', '-C', str(root), '-c', 'core.autocrlf=true',
                            'checkout-index', '-a', '-f'], check=True)
            shader.verify(root)

    def test_every_shipped_input_is_checked(self):
        for relative in (*shader.BUNDLE_FILES, *(f'third_party/vulkan/{name}' for name in shader.VULKAN_FILES)):
            with self.subTest(input=relative), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.copy_bundle(root)
                with (root / relative).open('ab') as target:
                    target.write(b'changed')
                with self.assertRaises(ValueError):
                    shader.verify(root)

    def test_omitted_receipts_cannot_bypass_verification(self):
        for manifest, filename in (
            ('src/field/shaders/manifest.json', 'src/field/shaders/frozen8.comp'),
            ('third_party/vulkan/manifest.json', 'vulkan_core.h'),
        ):
            with self.subTest(manifest=manifest), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.copy_bundle(root)
                self.mutate_manifest(root, manifest, lambda data: data['files'].pop(filename))
                with self.assertRaises(ValueError):
                    shader.verify(root)

    def test_unknown_receipts_and_contract_versions_fail(self):
        for key, value in (('version', 2), ('target', 'vulkan1.3'), ('glslang', 'latest'), ('naga', '28.0.0')):
            with self.subTest(key=key), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.copy_bundle(root)
                self.mutate_manifest(root, 'src/field/shaders/manifest.json', lambda data: data.update({key: value}))
                with self.assertRaises(ValueError):
                    shader.verify(root)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.copy_bundle(root)
            self.mutate_manifest(root, 'src/field/shaders/manifest.json', lambda data: data['files'].update({'../../outside': '0' * 64}))
            with self.assertRaisesRegex(ValueError, 'receipts'):
                shader.verify(root)

    def test_missing_compiler_argument_is_an_error(self):
        for option in ('--glslang', '--naga'):
            result = subprocess.run([sys.executable, str(ROOT / 'tools/shaders/gradient_compute.py'), option, 'unused'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('must be supplied together', result.stderr)


if __name__ == '__main__':
    unittest.main()
