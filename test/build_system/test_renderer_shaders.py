# SPDX-License-Identifier: GPL-3.0-or-later
"""Portable shader generation and backend binding layout contract checks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('renderer_shaders', ROOT / 'tools/shaders/generate.py')
shader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shader)


class RendererShaderTest(unittest.TestCase):
    def test_linux_program_archive_retains_complete_shader_bundle(self):
        workflow = (ROOT / '.github/workflows/ci-linux-build.yml').read_text()
        # Execute the actual archive file-selection block against a small build.
        start = workflow.index('          {\n', workflow.index('id: package_linux_test_programs'))
        end = workflow.index('\n      - uses:', start)
        command = '\n'.join(line[10:] for line in workflow[start:end].splitlines())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'build/linux/client/release'
            shader.generate(build / 'shaders')
            (build / 'runtime-assets/data').mkdir(parents=True)
            (build / 'runtime-assets/data/fixture').write_bytes(b'asset')
            (build / 'runtime-assets.json').write_text('{}')
            (root / 'build/sdl3-ci/prefix/lib').mkdir(parents=True)
            (root / 'artifacts/ci-native').mkdir(parents=True)
            subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', command], cwd=root, check=True)
            with tarfile.open(root / 'artifacts/ci-native/linux-test-programs.tar.gz') as archive:
                names = set(archive.getnames())
            for path in (build / 'shaders').rglob('*'):
                if path.is_file():
                    self.assertIn(path.relative_to(root).as_posix(), names)

    def test_std140_rig_layout(self):
        block = shader.metadata()['uniformBlocks']['skin-rig']
        self.assertEqual(block['size'], 2176)
        self.assertEqual([m['offset'] for m in block['members']], [0, 2048, 2112, 2160, 2164, 2168])
        self.assertEqual(block['members'][0]['arrayStride'], 64)
        self.assertEqual(block['members'][2]['matrixStride'], 16)
        self.assertEqual(shader.metadata()['uniformBlocks']['skin-fragment']['size'], 16)

    def test_source_identity_and_authoritative_materials(self):
        meta = shader.metadata()
        for dialect in ('webgl2', 'desktop-gl', 'vulkan'):
            for variant in ('skin', 'skin-rig'):
                result = shader.sources(ROOT, meta, dialect, variant)
                self.assertIn((ROOT / meta['materialSource']).read_text(), result['frag'])
                self.assertIn('skinShadeAtlas(paint,material', result['frag'])
                if variant == 'skin-rig':
                    body = (ROOT / meta['deformationSource']).read_text().split('void main() {', 1)[1].split(')GLSL"', 1)[0]
                    if dialect != 'vulkan':
                        self.assertIn('void main() {' + body, result['vert'])
                    else:
                        self.assertIn('gl_Position.z=(gl_Position.z+gl_Position.w)*0.5', result['vert'])
                self.assertNotIn('gl_Vertex', result['vert'])
                if dialect == 'webgl2':
                    self.assertNotRegex(result['vert'], r'layout\([^\n]*\) out')

    def test_generated_outputs_are_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            first = shader.generate(output)
            contents = {p.relative_to(output): p.read_bytes() for p in output.rglob('*') if p.is_file()}
            self.assertEqual(first, shader.generate(output))
            self.assertEqual(contents, {p.relative_to(output): p.read_bytes() for p in output.rglob('*') if p.is_file()})
            self.assertNotIn('compilerReceipt', first)

    def test_artifact_manifest_advertises_only_produced_formats(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            meta = shader.generate(output)
            self.assertEqual(set(meta['shaderArtifacts']), {'webgl2', 'desktop-gl'})
            for family, variants in meta['shaderArtifacts'].items():
                self.assertEqual(set(variants), set(meta['variants']))
                for stages in variants.values():
                    self.assertEqual(set(stages), {'vertex', 'fragment'})
                    for stage, receipt in stages.items():
                        self.assertEqual(receipt['stage'], stage)
                        self.assertEqual(receipt['entrypoint'], 'main')
                        self.assertEqual(receipt['license'], 'GPL-3.0-or-later')
                        self.assertIn('// SPDX-License-Identifier: GPL-3.0-or-later', (output / receipt['path']).read_text())
                        self.assertTrue((output / receipt['path']).is_file())
                        self.assertEqual(len(receipt['sha256']), 64)
            wgsl = output / 'translated.wgsl'
            wgsl.write_text('@fragment fn translated_main() -> @location(0) vec4<f32> {}')
            self.assertEqual(shader.artifact_entrypoint(wgsl, 'wgsl', 'fragment'), 'translated_main')
            with self.assertRaisesRegex(ValueError, 'expected one'):
                shader.artifact_entrypoint(wgsl, 'wgsl', 'vertex')

    def test_source_regeneration_invalidates_old_binaries(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            shader.generate(output)
            binary = output / 'vulkan/primitive.vert.spv'
            binary.write_bytes(b'stale')
            shader.generate(output)
            self.assertFalse(binary.exists())

    def test_primitive_matches_sdl_vertex(self):
        meta = shader.metadata()
        self.assertEqual(meta['vertexLayouts']['primitive']['stride'], 32)
        header = shader.layout_header(meta)
        self.assertIn('primitive_stride = 32', header)
        self.assertIn('primitive_fragment_alpha_offset = 4', header)
        self.assertIn('primitive_fragment_uvBounds_offset = 16', header)
        self.assertIn('primitive_fragment_size = 32', header)
        self.assertIn('skin_rig_size = 2176', header)
        self.assertEqual([a['offset'] for a in meta['vertexLayouts']['primitive']['attributes']], [0, 8, 24])
        for dialect in ('webgl2', 'desktop-gl', 'vulkan'):
            source = shader.sources(ROOT, meta, dialect, 'primitive')
            self.assertIn('position.y*2.0/viewport.y', source['vert'])
            self.assertIn('if(textured>0.5)', source['frag'])
            self.assertIn('color.a*=alpha', source['frag'])
            self.assertIn('textureGrad(image,clamp(uv,uvBounds.xy,uvBounds.zw),dFdx(uv),dFdy(uv))', source['frag'])

    def test_webgpu_derives_material_and_separate_samplers(self):
        meta = shader.metadata()
        self.assertEqual(meta['webgpuCompiler']['version'], '27.0.0')
        for variant in meta['variants']:
            sources = shader.webgpu_sources(ROOT, meta, variant)
            self.assertNotIn('uniform sampler2D', sources['frag'])
            if variant in ('skin', 'skin-rig'):
                self.assertIn((ROOT / meta['materialSource']).read_text(), sources['frag'])
                self.assertNotIn('#define SKIN_TEXTURE texture', sources['frag'])
                self.assertIn('#define dFdy(value) (-dFdy(value))', sources['frag'])
                self.assertIn('texture(sampler2D(paintTexture,paintSampler)', sources['frag'])
                self.assertIn('vec4 shaded=skinShade(', sources['frag'])
                self.assertIn('binding=3) uniform sampler materialSampler', sources['frag'])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            shader.generate(output)
            stale = output / 'webgpu/skin.vert.wgsl'
            stale.parent.mkdir(exist_ok=True)
            stale.write_text('stale')
            shader.generate(output)
            self.assertFalse(stale.exists())
            with self.assertRaisesRegex(ValueError, 'requires pinned glslang'):
                shader.generate(output, naga='nonexistent')

    def test_world_and_point_contracts(self):
        meta = shader.metadata()
        self.assertEqual(meta['vertexLayouts']['world']['stride'], 48)
        self.assertEqual([a['offset'] for a in meta['vertexLayouts']['world']['attributes']], [0, 16, 28, 36])
        self.assertEqual(meta['uniformBlocks']['world']['size'], 80)
        self.assertEqual(meta['uniformBlocks']['world-fragment']['size'], 16)
        for dialect in ('webgl2', 'desktop-gl', 'vulkan'):
            world = shader.sources(ROOT, meta, dialect, 'world')
            self.assertIn('pow(max(dot(normalize(normal),sunSpecular.xyz)', world['frag'].replace(' ', ''))
            self.assertIn('uv=texcoord*uvTransform.xy+uvTransform.zw', world['vert'].replace(' ', ''))
            self.assertEqual('gl_Position.z=2.0*gl_Position.z-gl_Position.w' in world['vert'], dialect != 'vulkan')
            cloud = shader.sources(ROOT, meta, dialect, 'cloud')
            self.assertNotIn('sunSpecular', cloud['frag'])
            point = shader.sources(ROOT, meta, dialect, 'point')
            self.assertIn('fwidth(radius)', point['frag'])
            self.assertNotIn('sampler2D', point['frag'])

    def test_integer_terrain_contracts(self):
        meta = shader.metadata()
        compose = meta['uniformBlocks']['terrain-compose-fragment']
        self.assertEqual(compose['size'], 352)
        self.assertEqual([m['offset'] for m in compose['members']], [0, 16, 80, 144, 160, 224, 288])
        self.assertEqual(meta['uniformBlocks']['terrain-reduce-fragment']['size'], 16)
        self.assertEqual([t['binding'] for t in meta['terrainComposeTextures']], list(range(6)))
        self.assertEqual([t['webgpuImageBinding'] for t in meta['terrainComposeTextures']], list(range(0, 12, 2)))
        for variant in ('terrain-compose', 'terrain-reduce'):
            self.assertEqual(meta['vertexLayouts'][variant], meta['vertexLayouts']['primitive'])
            canonical = (ROOT / meta['passSources'][variant + 'Fragment']).read_text()
            for dialect in ('webgl2', 'desktop-gl', 'vulkan'):
                self.assertIn(canonical, shader.sources(ROOT, meta, dialect, variant)['frag'])
            translated = shader.webgpu_sources(ROOT, meta, variant)['frag']
            self.assertNotIn('uniform usampler2D', translated)
            self.assertIn('texelFetch(sampler2D(', translated)
        translated = shader.webgpu_sources(ROOT, meta, 'terrain-compose')['frag']
        self.assertIn('uniform utexture2D coverageWeightsTexture', translated)
        self.assertIn('texelFetch(usampler2D(coverageWeightsTexture,coverageWeightsSampler)', translated)
        self.assertIn('terrain_compose_fragment_size = 352', shader.layout_header(meta))

    def test_rejects_overlapping_vertex_attributes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for p in (shader.MANIFEST, Path('libgag/shaders/skin-material.glsl'), Path('libgag/shaders/skin-materials.json')):
                (root / p).parent.mkdir(parents=True, exist_ok=True)
                (root / p).write_bytes((ROOT / p).read_bytes())
            manifest = json.loads((root / shader.MANIFEST).read_text())
            manifest['vertexLayouts']['skin']['attributes'][1]['offset'] = 0
            (root / shader.MANIFEST).write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'overlapping'):
                shader.metadata(root)

    def test_native_translation_is_optional_and_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            stale = output / 'direct3d12/skin.frag.dxil'
            stale.parent.mkdir()
            stale.write_bytes(b'stale')
            meta = shader.generate(output)
            self.assertFalse(stale.exists())
            self.assertNotIn('direct3d12', meta['shaderArtifacts'])
            self.assertNotIn('metal', meta['shaderArtifacts'])
            with self.assertRaisesRegex(ValueError, 'requires pinned glslang'):
                shader.generate(output, spirv_cross='unavailable')
            wrong = output / 'wrong-cross'
            wrong.write_bytes(b'wrong tool')
            with patch.object(shader.subprocess, 'check_output', return_value='Glslang Version: 15:15.1.0\n'), patch.object(shader.subprocess, 'run'), patch.object(shader, 'register_artifact'):
                with self.assertRaisesRegex(ValueError, 'SPIRV-Cross binary'):
                    shader.generate(output, compiler='compiler', spirv_cross=str(wrong))

    def test_metal_offline_compilation_receipts_and_tool_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            metal, metallib = output / 'metal-tool', output / 'metallib-tool'
            metal.write_bytes(b'metal compiler');metallib.write_bytes(b'metallib linker')
            source = output / 'primitive.metal'
            source.write_text('vertex float4 main0() { return float4(0); }')
            source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
            receipt = {'path': source.name, 'entrypoint': 'main0', 'sha256': source_hash}
            meta = {'variants': ['primitive'], 'license': 'GPL-3.0-or-later', 'nativeTranslationReceipt': {'metalSources': {'primitive': {'vertex': receipt, 'fragment': receipt}}}}
            metadata = output / 'metadata.json';metadata.write_text(json.dumps(meta))
            xcode = 'Xcode ' + json.loads((ROOT / 'mobile/toolchain.json').read_text())['ios']['xcode'] + '\nBuild version example'
            lock = {'xcodeVersion': xcode, 'sdk': 'macosx', 'sdkVersion': 'example', 'metalSha256': hashlib.sha256(metal.read_bytes()).hexdigest(), 'metallibSha256': hashlib.sha256(metallib.read_bytes()).hexdigest()}
            lockfile = output / 'tool-lock.json';lockfile.write_text(json.dumps(lock))
            commands = []
            def check(command, **kwargs):
                if 'xcodebuild' in command:return xcode
                if '--show-sdk-version' in command:return 'example'
                if '--find' in command:return str(metal if command[-1]=='metal' else metallib)
                raise AssertionError(command)
            def run(command, **kwargs):
                commands.append(command)
                Path(command[-1]).write_bytes(b'MTLBmain0\0' if 'metallib' in command else b'air')
            with patch.object(shader.subprocess, 'check_output', side_effect=check), patch.object(shader.subprocess, 'run', side_effect=run):
                result = shader.compile_metal(output, 'macosx', lockfile, developer_dir='/selected/Xcode')
            self.assertEqual(len(commands), 4)
            for stages in result['shaderArtifacts']['metal'].values():
                for receipt in stages.values():
                    self.assertEqual(receipt['format'], 'metallib')
                    self.assertEqual(receipt['entrypoint'], 'main0')
                    self.assertEqual(receipt['sourceSha256'], source_hash)
            self.assertTrue(result['nativeTranslationReceipt']['metalLibrariesProduced'])
            self.assertEqual(commands[0][:2], ['/usr/bin/env', 'DEVELOPER_DIR=/selected/Xcode'])
            self.assertIn('-std=macos-metal2.1', commands[0])
            for sdk in ('iphoneos', 'iphonesimulator'):
                with self.subTest(sdk=sdk):
                    lock['sdk'] = sdk
                    lockfile.write_text(json.dumps(lock))
                    commands.clear()
                    with patch.object(shader.subprocess, 'check_output', side_effect=check), patch.object(shader.subprocess, 'run', side_effect=run):
                        shader.compile_metal(output, sdk, lockfile)
                    self.assertEqual(commands[0][:3], ['xcrun', '--sdk', sdk])
                    self.assertIn('-std=ios-metal2.1', commands[0])
            lock['sdk'] = 'macosx'
            lockfile.write_text(json.dumps(lock))
            metal.write_bytes(b'unknown compiler')
            with patch.object(shader.subprocess, 'check_output', side_effect=check), patch.object(shader.subprocess, 'run') as calls:
                with self.assertRaisesRegex(ValueError, 'binary does not match'):
                    shader.compile_metal(output, 'macosx', lockfile)
                calls.assert_not_called()

    def test_verified_prebuilt_bundle_import_and_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            source, destination = Path(directory)/'source', Path(directory)/'installed'
            meta = shader.generate(source)
            imported = shader.import_bundle(source, destination)
            self.assertEqual(imported, meta)
            self.assertEqual(shader.verify_bundle(destination)[0], meta)
            # Artifact corruption must fail before replacing a working bundle.
            fragment = source / 'webgl2/primitive.frag'
            fragment.write_text(fragment.read_text() + '// modified')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                shader.import_bundle(source, destination)
            self.assertEqual(shader.verify_bundle(destination)[0], meta)
            shader.generate(source)
            tampered = json.loads((source/'metadata.json').read_text())
            tampered['sourceRevision'] = 'old revision'
            (source/'metadata.json').write_text(json.dumps(tampered))
            with self.assertRaisesRegex(ValueError, 'source/layout mismatch'):
                shader.verify_bundle(source)
            shader.generate(source)
            tampered = json.loads((source/'metadata.json').read_text())
            tampered['shaderArtifacts']['webgl2']['primitive']['fragment']['path'] = '../outside'
            (source/'metadata.json').write_text(json.dumps(tampered))
            with self.assertRaisesRegex(ValueError, 'escapes bundle'):
                shader.verify_bundle(source)

    def test_prebuilt_bundle_rejects_rewritten_authoritative_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            meta = shader.generate(source)
            fragment = source/'webgl2/primitive.frag'
            fragment.write_text(fragment.read_text().replace('color.a*=alpha', 'color.a=0.0'))
            meta['shaderArtifacts']['webgl2']['primitive']['fragment']['sha256'] = hashlib.sha256(fragment.read_bytes()).hexdigest()
            (source/'metadata.json').write_text(json.dumps(meta))
            with self.assertRaisesRegex(ValueError, 'differs from authoritative'):
                shader.verify_bundle(source)

    def test_build_prebuilt_route_preserves_artifacts_without_local_tools(self):
        build_spec = importlib.util.spec_from_file_location('shader_build', ROOT/'scons/renderer_shaders.py')
        build = importlib.util.module_from_spec(build_spec);build_spec.loader.exec_module(build)
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/'prebuilt', Path(directory)/'build'
            shader.generate(source)
            environment = {'GLOB2_RENDER_SHADER_INPUT': str(source), 'GLOB2_GLSLANG': '/missing/glslang', 'GLOB2_NAGA': '/missing/naga', 'GLOB2_DXC': '/missing/dxc', 'GLOB2_METAL_SDK': ''}
            with patch.dict(build.os.environ, environment):
                installed = build.prepare(ROOT, output)
                header_time = (installed/'RendererShaderLayouts.h').stat().st_mtime_ns
                self.assertEqual(build.prepare(ROOT, output), installed)
                self.assertEqual((installed/'RendererShaderLayouts.h').stat().st_mtime_ns, header_time)
                (installed/'webgl2/primitive.frag').write_text('corrupted')
                build.prepare(ROOT, output)
                shader.verify_bundle(installed)

    def test_native_compiler_version_is_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            compiler = Path(directory) / 'wrong-compiler'
            compiler.write_text('#!/bin/sh\nprintf "Glslang Version: 11:0.0.0\\n"\n')
            compiler.chmod(0o755)
            with self.assertRaisesRegex(ValueError, 'pinned version'):
                shader.generate(Path(directory) / 'output', str(compiler))


if __name__ == '__main__':
    unittest.main()
