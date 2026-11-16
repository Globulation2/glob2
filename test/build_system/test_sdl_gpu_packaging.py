# SPDX-License-Identifier: GPL-3.0-or-later
"""Mobile packages must carry complete SDL GPU shaders, and the pinned glslang provisioning."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
sys.path.insert(0, str(ROOT / 'platforms/mobile'))
import asset_bundle  # noqa: E402
import glslang_tool  # noqa: E402
import renderer_shaders  # noqa: E402

SPIRV = bytes.fromhex('03022307') + b'\0' * 12
METAL = b'MTLB' + b'main0\0'


def shader_set(directory, family='vulkan', fmt='spirv', suffix='.spv', payload=SPIRV, variants=('primitive', 'world')):
    directory = Path(directory)
    meta = {'variants': list(variants), 'shaderArtifacts': {family: {}}}
    for variant in variants:
        for stage, short in (('vertex', 'vert'), ('fragment', 'frag')):
            relative = family + '/' + variant + '.' + short + suffix
            (directory / relative).parent.mkdir(parents=True, exist_ok=True)
            (directory / relative).write_bytes(payload)
            meta['shaderArtifacts'][family].setdefault(variant, {})[stage] = {
                'path': relative, 'format': fmt, 'stage': stage, 'entrypoint': 'main',
                'sha256': hashlib.sha256(payload).hexdigest()}
    (directory / 'metadata.json').write_text(json.dumps(meta))
    return meta


class PackagingRequirementTests(unittest.TestCase):
    def test_complete_vulkan_set_passes_and_gaps_fail_loudly(self):
        with tempfile.TemporaryDirectory() as directory:
            shader_set(directory)
            self.assertEqual(renderer_shaders.missing_sdl_gpu_shaders(directory, 'android'), [])
            renderer_shaders.require_sdl_gpu_shaders(directory, 'android')
            (Path(directory) / 'vulkan/world.frag.spv').unlink()
            with self.assertRaisesRegex(ValueError, 'SDL GPU shaders are missing for android.*world.frag.spv.*glslang'):
                renderer_shaders.require_sdl_gpu_shaders(directory, 'android')

    def test_gl_only_build_and_tampered_binary_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'metadata.json').write_text(json.dumps({'variants': ['primitive'], 'shaderArtifacts': {'webgl2': {}}}))
            self.assertIn('no vulkan', renderer_shaders.missing_sdl_gpu_shaders(directory, 'android')[0])
        with tempfile.TemporaryDirectory() as directory:
            shader_set(directory)
            (Path(directory) / 'vulkan/primitive.vert.spv').write_bytes(SPIRV + b'x')
            self.assertIn('hash mismatch', renderer_shaders.missing_sdl_gpu_shaders(directory, 'android')[0])

    def test_ios_requires_metal_libraries_not_spirv(self):
        with tempfile.TemporaryDirectory() as directory:
            shader_set(directory)
            with self.assertRaisesRegex(ValueError, 'ios packaging.*compile Metal automatically'):
                renderer_shaders.require_sdl_gpu_shaders(directory, 'ios')
        with tempfile.TemporaryDirectory() as directory:
            shader_set(directory, 'metal', 'metallib', '.metallib', METAL)
            self.assertEqual(renderer_shaders.missing_sdl_gpu_shaders(directory, 'ios'), [])

    def test_apk_check_reads_the_packaged_shader_set(self):
        with tempfile.TemporaryDirectory() as directory:
            shaders = Path(directory) / 'shaders'
            shaders.mkdir()
            shader_set(shaders)
            apk = Path(directory) / 'app.apk'
            with zipfile.ZipFile(apk, 'w') as package:
                for path in shaders.rglob('*'):
                    if path.is_file():
                        package.write(path, 'assets/glob2-bundle/shaders/' + path.relative_to(shaders).as_posix())
            self.assertEqual(asset_bundle.verify_apk_sdl_gpu(apk), 4)
            stripped = Path(directory) / 'stripped.apk'
            with zipfile.ZipFile(apk) as source, zipfile.ZipFile(stripped, 'w') as target:
                for item in source.infolist():
                    if not item.filename.endswith('world.vert.spv'):
                        target.writestr(item, source.read(item))
            with self.assertRaisesRegex(ValueError, 'missing SDL GPU shader vulkan/world.vert.spv'):
                asset_bundle.verify_apk_sdl_gpu(stripped)


class AndroidSdlVulkanTests(unittest.TestCase):
    def test_android_sdl_builds_the_vulkan_gpu_driver(self):
        # Without SDL_VULKAN the SDL GPU device reports "vulkan unsupported" on Android.
        manifest = json.loads((ROOT / 'platforms/mobile/vcpkg.json').read_text())
        sdl = next(entry for entry in manifest['dependencies'] if isinstance(entry, dict) and entry['name'] == 'sdl3')
        self.assertIn({'name': 'vulkan', 'platform': 'android'}, sdl.get('features', []))


class GlslangProvisioningTests(unittest.TestCase):
    def test_pin_matches_the_shader_manifest(self):
        pin = json.loads((ROOT / 'buildsys/glslang.json').read_text())
        manifest = json.loads((ROOT / 'libs/gag/shaders/renderer-manifest.json').read_text())
        self.assertEqual(pin['version'], manifest['nativeCompiler']['version'])
        self.assertRegex(pin['source']['sha256'], '^[0-9a-f]{64}$')
        self.assertIn('ENABLE_OPT=OFF', ' '.join(pin['cmake_options']))

    def test_environment_override_and_explicit_disable(self):
        self.assertEqual(glslang_tool.resolve_for_build({'GLOB2_GLSLANG': '/pinned/glslang'}), '/pinned/glslang')
        self.assertIsNone(glslang_tool.resolve_for_build({'GLOB2_GLSLANG': ''}))
        with patch.object(glslang_tool, 'locate', return_value=None):
            self.assertIsNone(glslang_tool.resolve_for_build({}))

    def test_installation_with_changed_binary_is_not_trusted(self):
        with tempfile.TemporaryDirectory() as directory:
            install = Path(directory)
            binary = install / 'bin' / glslang_tool.EXECUTABLE
            binary.parent.mkdir()
            binary.write_bytes(b'compiler')
            pin = json.loads((ROOT / 'buildsys/glslang.json').read_text())
            (install / 'receipt.json').write_text(json.dumps({'source_sha256': pin['source']['sha256'],
                                                              'binary_sha256': hashlib.sha256(b'compiler').hexdigest()}))
            with patch.object(glslang_tool, 'install_directory', return_value=install):
                self.assertEqual(glslang_tool.locate(), binary)
                binary.write_bytes(b'replaced')
                self.assertIsNone(glslang_tool.locate())

    def test_version_check_rejects_other_compilers(self):
        with patch.object(glslang_tool.subprocess, 'check_output', return_value='Glslang Version: 11:11.1.0\n'):
            with self.assertRaisesRegex(ValueError, 'not pinned glslang 15.1.0'):
                glslang_tool.check_version('glslang')
        with patch.object(glslang_tool.subprocess, 'check_output', return_value='Glslang Version: 11:15.1.0\n'):
            glslang_tool.check_version('glslang')


if __name__ == '__main__':
    unittest.main()
