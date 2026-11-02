#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate portable skin shaders and layout metadata; optionally compile SPIR-V.

No runtime compiler or alternative material implementation is introduced. Outputs
are build artifacts. WGSL uses pinned Naga after SPIR-V validation. DXIL and Metal libraries remain
gated on validated tooling and platform render evidence.
"""
import argparse
import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path
import re
import subprocess
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
import skin_materials

MANIFEST = Path('libs/gag/shaders/renderer-manifest.json')
TYPES = {'float': (4, 4), 'vec2': (8, 8), 'vec4': (16, 16), 'ivec4': (16, 16), 'uvec4': (16, 16), 'mat3': (16, 48), 'mat4': (16, 64)}


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def metadata(root=ROOT):
    manifest = json.loads((root / MANIFEST).read_text())
    if manifest['version'] != 1 or manifest['variants'] != ['skin', 'skin-rig', 'primitive', 'world', 'cloud', 'point', 'terrain-compose', 'terrain-reduce']:
        raise ValueError('unsupported renderer manifest version or variants')
    registry = skin_materials.load_registry(root)
    skin_materials.check_shader(root, registry)
    for layout in manifest['vertexLayouts'].values():
        occupied, locations, names = set(), set(), set()
        for a in layout['attributes']:
            span = set(range(a['offset'], a['offset'] + 4 * a['components']))
            if (a['components'] not in (2, 3, 4) or a['offset'] < 0 or a['offset'] % 4 or
                    a['location'] < 0 or a['location'] in locations or a['name'] in names or
                    occupied & span or max(span) >= layout['stride']):
                raise ValueError('invalid or overlapping vertex attribute')
            occupied |= span
            locations.add(a['location'])
            names.add(a['name'])
    for block in manifest['uniformBlocks'].values():
        offset, names = 0, set()
        for member in block['members']:
            if member['name'] in names:
                raise ValueError('duplicate uniform member')
            names.add(member['name'])
            alignment, size = TYPES[member['type']]
            if 'count' in member:
                if member['count'] < 1:
                    raise ValueError('uniform array count must be positive')
                alignment = max(16, alignment)
                member['arrayStride'] = align(size, 16)
                size = member['arrayStride'] * member['count']
            member['offset'] = align(offset, alignment)
            if member['type'].startswith('mat'):
                member['matrixStride'] = 16
            offset = member['offset'] + size
        block['size'] = align(offset, 16)
        block['layout'] = 'std140-column-major'
    manifest['materials'] = registry
    digest = hashlib.sha256()
    for path in [MANIFEST, skin_materials.REGISTRY, skin_materials.SHADER,
                 Path(manifest['deformationSource']), Path('tools/shaders/generate.py')] + [Path(p) for p in manifest['passSources'].values()]:
        digest.update(path.as_posix().encode())
        digest.update((root / path).read_bytes())
    manifest['sourceRevision'] = digest.hexdigest()
    return manifest


def declarations(block, native):
    members = '\n'.join('%s %s%s;' % (m['type'], m['name'],
                        '[%d]' % m['count'] if 'count' in m else '') for m in block['members'])
    if native:
        return 'layout(std140,set=%d,binding=%d) uniform Parameters {\n%s\n};\n' % (block['set'], block['binding'], members)
    return '\n'.join('uniform ' + line for line in members.splitlines()) + '\n'


def sources(root, meta, dialect, variant):
    native = dialect == 'vulkan'
    version = {'webgl2': '#version 300 es\nprecision highp float;\nprecision highp int;\n',
               'desktop-gl': '#version 330 core\n', 'vulkan': '#version 450\n'}[dialect]
    if dialect == 'webgl2' and variant == 'terrain-compose':
        version += 'precision highp usampler2D;\n'
    attributes = ''.join('layout(location=%d) in vec%d %s;\n' %
                         (a['location'], a['components'], a['name']) for a in meta['vertexLayouts'][variant]['attributes'])
    if variant in ('primitive', 'point', 'terrain-compose', 'terrain-reduce'):
        vertex = (version + attributes + declarations(meta['uniformBlocks'][variant], native) +
                  ('layout(location=0) ' if native else '') + 'out vec2 uv;\n' +
                  ('layout(location=1) ' if native else '') + 'out vec4 tint;\n' +
                  'void main(){uv=texcoord;tint=vertexColor;gl_Position=vec4(position.x*2.0/viewport.x-1.0,1.0-position.y*2.0/viewport.y,0.0,1.0);}')
        if variant in ('terrain-compose', 'terrain-reduce'):
            textures = meta['terrainComposeTextures'] if variant == 'terrain-compose' else [meta['terrainReduceTexture']]
            samplers = ''.join(('layout(set=%d,binding=%d) ' % (t['set'], t['binding']) if native else '') +
                               'uniform %s %s;\n' % (t['type'], t['name']) for t in textures)
            fragment = (version + samplers + declarations(meta['uniformBlocks'][variant + '-fragment'], native) +
                        ('layout(location=0) ' if native else '') + 'in vec2 uv;\n' +
                        'layout(location=0) out vec4 color;\n' +
                        (root / meta['passSources'][variant + 'Fragment']).read_text())
            return {'vert': vertex, 'frag': fragment}
        if variant == 'point':
            fragment = (version + declarations(meta['uniformBlocks']['point-fragment'], native) +
                        ('layout(location=0) ' if native else '') + 'in vec2 uv;\n' +
                        ('layout(location=1) ' if native else '') + 'in vec4 tint;\n' +
                        'layout(location=0) out vec4 color;\n' +
                        (root / meta['passSources']['pointFragment']).read_text())
            return {'vert': vertex, 'frag': fragment}
        texture = meta['primitiveTexture']
        fragment = (version + ('layout(set=%d,binding=%d) ' % (texture['set'], texture['binding']) if native else '') +
                    'uniform sampler2D image;\n' + declarations(meta['uniformBlocks']['primitive-fragment'], native) +
                    ('layout(location=0) ' if native else '') + 'in vec2 uv;\n' +
                    ('layout(location=1) ' if native else '') + 'in vec4 tint;\n' +
                    'layout(location=0) out vec4 color;\n' +
                    'void main(){color=tint;if(textured>0.5)color*=textureGrad(image,clamp(uv,uvBounds.xy,uvBounds.zw),dFdx(uv),dFdy(uv));color.a*=alpha;}')
        return {'vert': vertex, 'frag': fragment}
    if variant in ('world', 'cloud'):
        varyings = [('vec3', 'light'), ('vec2', 'uv'), ('vec3', 'normal')]
        vertex_outputs = ''.join(('layout(location=%d) ' % i if native else '') +
                                 'out %s %s;\n' % (kind, name) for i, (kind, name) in enumerate(varyings))
        depth = '#define RENDER_CANONICAL_DEPTH() ' + ('\n' if native else 'gl_Position.z=2.0*gl_Position.z-gl_Position.w\n')
        vertex = (version + attributes + vertex_outputs + declarations(meta['uniformBlocks'][variant], native) + depth +
                  (root / meta['passSources']['worldVertex']).read_text())
        texture = meta['worldTexture']
        fragment = (version + ('layout(set=%d,binding=%d) ' % (texture['set'], texture['binding']) if native else '') +
                    'uniform sampler2D world;\n' + re.sub(r'\bout\b', 'in', vertex_outputs) + 'layout(location=0) out vec4 color;\n')
        if variant == 'world':
            fragment += declarations(meta['uniformBlocks']['world-fragment'], native)
        fragment += (root / meta['passSources'][variant + 'Fragment']).read_text()
        return {'vert': vertex, 'frag': fragment}
    outputs = ''.join(('layout(location=%d) ' % i if native else '') + ('out %s %s;\n' % (kind, name)) for i, (kind, name) in enumerate((('vec2', 'uv'), ('vec2', 'detailUV'), ('vec3', 'normal'))))
    vertex = version + attributes + outputs + declarations(meta['uniformBlocks'][variant], native)
    if variant == 'skin-rig':
        body = (root / meta['deformationSource']).read_text().split('R"GLSL(', 1)[1].split(')GLSL"', 1)[0]
        # All uniforms come from the manifest; deformation remains verbatim.
        body = re.sub(r'^uniform [^;]+;\n', '', body, flags=re.M)
    else:
        body = ('void main(){uv=texcoord.xy;detailUV=texcoord.zw;normal=surfaceNormal;'
                'gl_Position=vec4(position.xy/1.25+normalize(surfaceNormal).xy*shell*furLength,'
                'position.z-shell*shellDepth,1.0);}\n')
    if native:
        # GL clip depth is -w..w; SDL GPU/Vulkan uses 0..w.
        head, tail = body.rsplit('}', 1)
        body = head + 'gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;\n}' + tail
    vertex += body
    samplers = ''.join(('layout(set=%d,binding=%d) ' % (t['set'], t['binding']) if native else '') +
                       'uniform sampler2D %s;\n' % t['name'] for t in meta['textures'])
    inputs = re.sub(r'\bout\b', 'in', outputs)
    fragment = (version + ('#define dFdy(value) (-dFdy(value))\n' if native else '') + '#define SKIN_TEXTURE texture\n' + samplers + inputs +
                'layout(location=0) out vec4 color;\n' + declarations(meta['uniformBlocks']['skin-fragment'], native) +
                (root / meta['materialSource']).read_text() +
                '\nvoid main(){vec4 shaded=skinShadeAtlas(paint,material,region,normal,uv,detailUV,shell);'
                'if(shaded.a<0.5)discard;color=vec4(shaded.rgb,1.0);}\n')
    return {'vert': vertex, 'frag': fragment}


def webgpu_sources(root, meta, variant):
    """Derive separate image/sampler GLSL for Naga's SPIR-V frontend.

    Vulkan's combined sampler uniforms cannot be consumed by this translator.
    Material code remains verbatim; the atlas sampling wrapper is extracted
    from that same source, with constructors placed directly at texture calls.
    """
    result = sources(root, meta, 'vulkan', variant)
    fragment = result['frag']
    if variant in ('skin', 'skin-rig'):
        material = (root / meta['materialSource']).read_text()
        match = re.search(r'vec4 skinShadeAtlas\([^\n]+detailUV[^\n]+\) \{\n(.*?)\n\}', material, re.S)
        if not match:
            raise ValueError('authoritative atlas sampling wrapper not found')
        body = match.group(1)
        for texture in meta['textures']:
            name = texture['name']
            body = body.replace('SKIN_TEXTURE(' + name + ',', 'texture(sampler2D(' + name + 'Texture,' + name + 'Sampler),')
        body = body.replace('return skinShade(', 'vec4 shaded=skinShade(')
        fragment = fragment.replace('#define SKIN_TEXTURE texture\n', '')
        fragment = fragment.split('\nvoid main(){vec4 shaded=', 1)[0] + '\nvoid main(){' + body + '\nif(shaded.a<0.5)discard;color=vec4(shaded.rgb,1.0);}'
        textures = meta['textures']
    elif variant == 'terrain-compose':
        textures = meta['terrainComposeTextures']
    elif variant == 'terrain-reduce':
        textures = [meta['terrainReduceTexture']]
    elif variant == 'primitive':
        textures = [meta['primitiveTexture']]
    elif variant in ('world', 'cloud'):
        textures = [meta['worldTexture']]
    else:
        textures = []
    for index, texture in enumerate(textures):
        name = texture['name']
        kind = texture.get('type', 'sampler2D')
        image_kind = 'utexture2D' if kind == 'usampler2D' else 'texture2D'
        old = 'layout(set=%d,binding=%d) uniform %s %s;' % (texture['set'], texture['binding'], kind, name)
        new = ('layout(set=2,binding=%d) uniform %s %sTexture;\n'
               'layout(set=2,binding=%d) uniform sampler %sSampler;') % (index*2, image_kind, name, index*2+1, name)
        if old not in fragment:
            raise ValueError('missing combined sampler declaration: ' + name)
        fragment = fragment.replace(old, new)
        for function in ('texture', 'textureGrad', 'texelFetch', 'textureSize'):
            fragment = fragment.replace(function + '(' + name + ',', function + '(' + kind + '(' + name + 'Texture,' + name + 'Sampler),')
    result['frag'] = fragment
    return result


def layout_header(meta):
    """C++ consumers get the same offsets/bindings as tooling without JSON parsing."""
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later', '// Generated by tools/shaders/generate.py; do not edit.', '#pragma once',
             '#include <cstdint>', 'namespace GAGCore::RendererShaderLayouts {',
             'struct Attribute { const char *name; std::uint32_t location, components, offset; };']
    for name, layout in meta['vertexLayouts'].items():
        key = name.replace('-', '_')
        lines.append('inline constexpr std::uint32_t %s_stride = %d;' % (key, layout['stride']))
        lines.append('inline constexpr Attribute %s_attributes[] = {' % key)
        lines.extend('    {"%s", %d, %d, %d},' % (a['name'], a['location'], a['components'], a['offset']) for a in layout['attributes'])
        lines.append('};')
    for name, block in meta['uniformBlocks'].items():
        key = name.replace('-', '_')
        for field in ('set', 'binding', 'size'):
            lines.append('inline constexpr std::uint32_t %s_%s = %d;' % (key, field, block[field]))
        for member in block['members']:
            lines.append('inline constexpr std::uint32_t %s_%s_offset = %d;' % (key, member['name'], member['offset']))
    for texture in meta['textures'] + [meta['primitiveTexture'], meta['worldTexture']] + meta['terrainComposeTextures'] + [meta['terrainReduceTexture']]:
        for field in ('set', 'binding'):
            lines.append('inline constexpr std::uint32_t %s_%s = %d;' % (texture.get('layoutName', texture['name']), field, texture[field]))
    return '\n'.join(lines + ['}']) + '\n'



def artifact_entrypoint(path, format, stage):
    """Read actual compiled entry points; never guess backend tool names."""
    if format == 'spirv':
        binary = path.read_bytes()
        if len(binary) < 20 or len(binary) % 4:
            raise ValueError('invalid SPIR-V artifact: ' + str(path))
        words = struct.unpack('<%dI' % (len(binary) // 4), binary)
        if words[0] != 0x07230203:
            raise ValueError('invalid SPIR-V magic: ' + str(path))
        entries, offset = [], 5
        while offset < len(words):
            length, opcode = words[offset] >> 16, words[offset] & 0xffff
            if not length or offset + length > len(words):
                raise ValueError('invalid SPIR-V instruction: ' + str(path))
            if opcode == 15 and length >= 4:  # OpEntryPoint
                model = 0 if stage == 'vertex' else 4
                if words[offset + 1] == model:
                    encoded = struct.pack('<%dI' % (length - 3), *words[offset + 3:offset + length])
                    entries.append(encoded.split(b'\0', 1)[0].decode('utf8'))
            offset += length
    elif format == 'wgsl':
        entries = re.findall(r'@' + stage + r'\s+fn\s+(\w+)\s*\(', path.read_text())
    else:
        entries = re.findall(r'\bvoid\s+(main)\s*\(', path.read_text())
    if len(entries) != 1:
        raise ValueError('expected one ' + stage + ' entry point: ' + str(path))
    return entries[0]


def register_artifact(meta, output, family, variant, stage, path, format, entrypoint=None):
    """Only produced artifacts advertise support; unavailable formats stay absent."""
    entrypoint = entrypoint or artifact_entrypoint(path, format, stage)
    meta.setdefault('shaderArtifacts', {}).setdefault(family, {}).setdefault(variant, {})[stage] = {
        'path': path.relative_to(output).as_posix(), 'format': format, 'stage': stage,
        'entrypoint': entrypoint, 'license': meta['license'], 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def generate(output, compiler=None, root=ROOT, naga=None, spirv_cross=None, dxc=None):
    meta = metadata(root)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'RendererShaderLayouts.h').write_text(layout_header(meta))
    for stale in [p for family in ('webgpu', 'direct3d12', 'metal', 'native-sources') for p in (output / family).glob('*')]:
        if stale.is_file():
            stale.unlink()
    for dialect in ('webgl2', 'desktop-gl', 'vulkan'):
        directory = output / dialect
        directory.mkdir(exist_ok=True)
        for variant in meta['variants']:
            for stage, source in sources(root, meta, dialect, variant).items():
                destination = directory / (variant + '.' + stage)
                destination.write_text(source.replace('\n', '\n// SPDX-License-Identifier: ' + meta['license'] + '\n', 1))
                if dialect != 'vulkan':
                    register_artifact(meta, output, dialect, variant, 'vertex' if stage == 'vert' else 'fragment', destination, 'glsl-es300' if dialect == 'webgl2' else 'glsl330')
                Path(str(destination) + '.spv').unlink(missing_ok=True)
    (output / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
    if compiler:
        version = subprocess.check_output([compiler, '--version'], text=True)
        expected = meta['nativeCompiler']['version']
        if not re.search(r'^Glslang Version: \d+:' + re.escape(expected) + r'$', version, re.M):
            raise ValueError('glslangValidator must be pinned version ' + expected)
        for source in sorted((output / 'vulkan').glob('*')):
            if source.suffix in ('.vert', '.frag'):
                subprocess.run([compiler, '-V', '--target-env', meta['nativeCompiler']['target'],
                                str(source), '-o', str(source) + '.spv'], check=True)
                register_artifact(meta, output, 'vulkan', source.stem, 'vertex' if source.suffix == '.vert' else 'fragment', Path(str(source) + '.spv'), 'spirv')
        meta['compilerReceipt'] = version
    if naga:
        if not compiler:
            raise ValueError('WGSL generation requires pinned glslang')
        version = subprocess.check_output([naga, '--version'], text=True).strip()
        expected = meta['webgpuCompiler']['version']
        if version != expected:
            raise ValueError('naga must be pinned version ' + expected)
        directory = output / 'webgpu'
        directory.mkdir(exist_ok=True)
        for variant in meta['variants']:
            for stage, source in webgpu_sources(root, meta, variant).items():
                destination = directory / (variant + '.' + stage)
                destination.write_text(source)
                binary = str(destination) + '.spv'
                subprocess.run([compiler, '-V', '-Os', '--target-env', meta['nativeCompiler']['target'],
                                str(destination), '-o', binary], check=True)
                subprocess.run([naga, '--keep-coordinate-space', binary, str(destination) + '.wgsl'], check=True)
                wgsl = Path(str(destination) + '.wgsl')
                wgsl.write_text('// SPDX-License-Identifier: ' + meta['license'] + '\n' + wgsl.read_text())
                register_artifact(meta, output, 'webgpu', variant, 'vertex' if stage == 'vert' else 'fragment', Path(str(destination) + '.wgsl'), 'wgsl')
        meta['webgpuCompilerReceipt'] = {'naga': version, 'coordinates': 'canonical-depth-no-y-flip',
                                         **meta['webgpuCompiler']['sampledResources']}
    if spirv_cross or dxc:
        if not compiler or not spirv_cross:
            raise ValueError('native translation requires pinned glslang and SPIRV-Cross')
        pins = meta['nativeTranslationTools']
        cross_digest = hashlib.sha256(Path(spirv_cross).read_bytes()).hexdigest()
        if cross_digest != pins['spirvCross']['linuxX86BinarySha256']:
            raise ValueError('SPIRV-Cross binary does not match pinned Linux build')
        if dxc:
            dxc_digest = hashlib.sha256(Path(dxc).read_bytes()).hexdigest()
            dxc_version = subprocess.check_output([dxc, '--version'], text=True).strip()
            if dxc_digest != pins['dxc']['linuxX86BinarySha256'] or dxc_version != pins['dxc']['reportedVersion']:
                raise ValueError('DXC binary does not match pinned Linux build')
        source_directory = output / 'native-sources'
        source_directory.mkdir(exist_ok=True)
        dxil_directory = output / 'direct3d12'
        if dxc:
            dxil_directory.mkdir(exist_ok=True)
        metal_sources = {}
        for variant in meta['variants']:
            for suffix, stage, profile in (('vert', 'vertex', 'vs_6_0'), ('frag', 'fragment', 'ps_6_0')):
                binary = output / 'vulkan' / (variant + '.' + suffix + '.spv')
                hlsl = source_directory / (variant + '.' + suffix + '.hlsl')
                msl = source_directory / (variant + '.' + suffix + '.metal')
                subprocess.run([spirv_cross, str(binary), '--hlsl', '--shader-model', str(pins['spirvCross']['shaderModel']), '--output', str(hlsl)], check=True)
                subprocess.run([spirv_cross, str(binary), '--msl', '--msl-version', str(pins['spirvCross']['metalVersion']), '--msl-decoration-binding', '--output', str(msl)], check=True)
                for source in (hlsl, msl):
                    source.write_text('// SPDX-License-Identifier: ' + meta['license'] + '\n' + source.read_text())
                entry = re.search(r'\b' + stage + r'\s+\w+\s+(\w+)\s*\(', msl.read_text())
                if not entry:
                    raise ValueError('Metal source entry point not found')
                metal_sources.setdefault(variant, {})[stage] = {'path': msl.relative_to(output).as_posix(), 'entrypoint': entry.group(1), 'sha256': hashlib.sha256(msl.read_bytes()).hexdigest()}
                if dxc:
                    destination = dxil_directory / (variant + '.' + suffix + '.dxil')
                    subprocess.run([dxc, '-E', 'main', '-T', profile, str(hlsl), '-Fo', str(destination)], check=True)
                    reflection = subprocess.check_output([dxc, '-dumpbin', str(destination)], text=True)
                    entries = re.findall(r'^; EntryFunctionName: (\w+)$', reflection, re.M)
                    if len(entries) != 1:
                        raise ValueError('DXIL must contain one reflected entry point')
                    register_artifact(meta, output, 'direct3d12', variant, stage, destination, 'dxil', entries[0])
        meta['nativeTranslationReceipt'] = {'spirvCrossSha256': cross_digest, 'metalSources': metal_sources,
                                           'metalLibrariesProduced': False}
        if dxc:
            meta['nativeTranslationReceipt'].update({'dxcSha256': dxc_digest, 'dxcReportedVersion': dxc_version})
    (output / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
    return meta


def compile_metal(output, sdk, tool_lock, root=ROOT, developer_dir=None):
    """Compile translated sources using an explicitly verified Apple tool lock.

    Xcode selection remains local to these subprocesses. Unknown compiler/SDK
    binaries fail closed; source translation alone never advertises Metal.
    """
    lock = json.loads(Path(tool_lock).read_text())
    xcode_pin = json.loads((root / 'platforms/mobile/toolchain.json').read_text())['ios']['xcode']
    prefix = ['/usr/bin/env', 'DEVELOPER_DIR=' + developer_dir] if developer_dir else []
    xcrun = prefix + ['xcrun', '--sdk', sdk]
    xcode = subprocess.check_output(prefix + ['xcodebuild', '-version'], text=True).strip()
    if xcode.splitlines()[0] != 'Xcode ' + xcode_pin or lock['xcodeVersion'] != xcode:
        raise ValueError('Metal requires the repository-pinned Xcode and verified build version')
    sdk_version = subprocess.check_output(xcrun + ['--show-sdk-version'], text=True).strip()
    if lock['sdk'] != sdk or lock['sdkVersion'] != sdk_version:
        raise ValueError('Metal SDK does not match verified tool lock')
    binaries = {}
    for name in ('metal', 'metallib'):
        binary = Path(subprocess.check_output(xcrun + ['--find', name], text=True).strip())
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        if digest != lock[name + 'Sha256']:
            raise ValueError('Metal tool binary does not match verified lock: ' + name)
        binaries[name] = binary
    metadata_path = output / 'metadata.json'
    meta = json.loads(metadata_path.read_text())
    sources = meta.get('nativeTranslationReceipt', {}).get('metalSources')
    if not sources or set(sources) != set(meta['variants']):
        raise ValueError('Metal requires complete authoritative translated source receipts')
    if any(set(stages) != {'vertex', 'fragment'} for stages in sources.values()):
        raise ValueError('Metal requires complete vertex/fragment stage receipts')
    meta.setdefault('shaderArtifacts', {}).pop('metal', None)
    meta['nativeTranslationReceipt']['metalLibrariesProduced'] = False
    metadata_path.write_text(json.dumps(meta, indent=2) + '\n')
    directory = output / 'metal'
    directory.mkdir(exist_ok=True)
    produced = []
    # Metal 2.x dialect names include the platform in Apple's compiler.
    standard = 'macos-metal2.1' if sdk == 'macosx' else 'ios-metal2.1'
    # Register artifacts only after the complete set compiles and links.
    for variant, stages in sources.items():
        for stage, receipt in stages.items():
            source = output / receipt['path']
            if hashlib.sha256(source.read_bytes()).hexdigest() != receipt['sha256']:
                raise ValueError('Metal translated source changed after receipt')
            suffix = 'vert' if stage == 'vertex' else 'frag'
            air = directory / (variant + '.' + suffix + '.air')
            library = directory / (variant + '.' + suffix + '.metallib')
            subprocess.run(xcrun + ['metal', '-std=' + standard, '-c', str(source), '-o', str(air)], check=True)
            subprocess.run(xcrun + ['metallib', str(air), '-o', str(library)], check=True)
            data = library.read_bytes()
            if not data.startswith(b'MTLB') or receipt['entrypoint'].encode() + b'\0' not in data:
                raise ValueError('Metal library missing format magic or compiled entrypoint name')
            produced.append((variant, stage, library, receipt))
    for variant, stage, library, source in produced:
        register_artifact(meta, output, 'metal', variant, stage, library, 'metallib', source['entrypoint'])
        meta['shaderArtifacts']['metal'][variant][stage]['sourceSha256'] = source['sha256']
    meta['nativeTranslationReceipt'].update({'metalLibrariesProduced': True, 'metalToolLock': lock})
    metadata_path.write_text(json.dumps(meta, indent=2) + '\n')
    return meta


def verify_bundle(directory, root=ROOT):
    """Verify portable prebuilt artifacts against this exact source revision."""
    directory = Path(directory).resolve()
    expected = metadata(root)
    meta = json.loads((directory / 'metadata.json').read_text())
    for key, value in expected.items():
        if meta.get(key) != value:
            raise ValueError('Prebuilt shader source/layout mismatch: ' + key)
    if (directory / 'RendererShaderLayouts.h').read_text() != layout_header(expected):
        raise ValueError('Prebuilt renderer layout header mismatch')
    files = {'metadata.json', 'RendererShaderLayouts.h'}
    formats = {'webgl2': 'glsl-es300', 'desktop-gl': 'glsl330', 'vulkan': 'spirv', 'webgpu': 'wgsl', 'direct3d12': 'dxil', 'metal': 'metallib'}
    def resolve_receipt(receipt):
        relative = Path(receipt['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Prebuilt shader path escapes bundle')
        source = directory / relative
        if not source.is_file() or source.is_symlink() or not source.resolve().is_relative_to(directory):
            raise ValueError('Prebuilt shader artifact missing or outside bundle')
        if hashlib.sha256(source.read_bytes()).hexdigest() != receipt['sha256']:
            raise ValueError('Prebuilt shader artifact hash mismatch: ' + relative.as_posix())
        files.add(relative.as_posix())
        return source
    artifacts = meta.get('shaderArtifacts', {})
    if not {'webgl2', 'desktop-gl'} <= artifacts.keys():
        raise ValueError('Prebuilt bundle lacks portable GL fallback shaders')
    for family, variants in artifacts.items():
        if family not in formats or set(variants) != set(expected['variants']):
            raise ValueError('Prebuilt shader family is unknown or incomplete')
        for variant, stages in variants.items():
            if set(stages) != {'vertex', 'fragment'}:
                raise ValueError('Prebuilt shader stages incomplete')
            for stage, receipt in stages.items():
                if receipt['format'] != formats[family] or receipt['stage'] != stage or receipt['license'] != expected['license'] or not re.fullmatch(r'[A-Za-z_]\w*', receipt['entrypoint']):
                    raise ValueError('Prebuilt shader receipt mismatch')
                source = resolve_receipt(receipt)
                if family in ('webgl2', 'desktop-gl'):
                    suffix = 'vert' if stage == 'vertex' else 'frag'
                    authoritative = sources(root, expected, family, variant)[suffix].replace('\n', '\n// SPDX-License-Identifier: ' + expected['license'] + '\n', 1)
                    if source.read_text() != authoritative:
                        raise ValueError('Prebuilt GL source differs from authoritative source')
                if family in ('webgl2', 'desktop-gl', 'vulkan', 'webgpu'):
                    if artifact_entrypoint(source, receipt['format'], stage) != receipt['entrypoint']:
                        raise ValueError('Prebuilt compiled entrypoint mismatch')
                elif family == 'direct3d12':
                    if not source.read_bytes().startswith(b'DXBC'):
                        raise ValueError('Prebuilt DXIL container missing')
                elif not source.read_bytes().startswith(b'MTLB') or receipt['entrypoint'].encode() + b'\0' not in source.read_bytes():
                    raise ValueError('Prebuilt Metal function missing')
    metal_sources = meta.get('nativeTranslationReceipt', {}).get('metalSources', {})
    if metal_sources:
        if set(metal_sources) != set(expected['variants']) or any(set(stages) != {'vertex', 'fragment'} for stages in metal_sources.values()):
            raise ValueError('Prebuilt Metal source receipts incomplete')
        for stages in metal_sources.values():
            for stage, receipt in stages.items():
                source = resolve_receipt(receipt)
                entry = re.search(r'\b' + stage + r'\s+\w+\s+(\w+)\s*\(', source.read_text())
                if not entry or entry.group(1) != receipt['entrypoint']:
                    raise ValueError('Prebuilt Metal source entrypoint mismatch')
    return meta, sorted(files)


def import_bundle(source, destination, root=ROOT):
    """Stage a verified prebuilt set without invoking or changing translators."""
    source, destination = Path(source).resolve(), Path(destination)
    meta, files = verify_bundle(source, root)
    if source == destination.resolve():
        return meta
    if source.is_relative_to(destination.resolve()) or destination.resolve().is_relative_to(source):
        raise ValueError('Prebuilt input and output directories must not overlap')
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.renderer-shaders-', dir=destination.parent))
    backup = None
    try:
        for relative in files:
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / relative, target)
        verify_bundle(staging, root)
        if destination.exists():
            backup = Path(tempfile.mkdtemp(prefix='.renderer-shaders-old-', dir=destination.parent))
            backup.rmdir()
            os.replace(destination, backup)
        os.replace(staging, destination)
    except BaseException:
        if backup and backup.exists() and not destination.exists():
            os.replace(backup, destination)
        raise
    finally:
        if staging.exists():
            shutil.rmtree(staging)
        if backup and backup.exists():
            shutil.rmtree(backup)
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--glslang', help='Optional path to pinned build-only glslangValidator')
    parser.add_argument('--naga', help='Optional pinned build-only Naga 27.0.0 for WGSL')
    parser.add_argument('--spirv-cross', help='Pinned build-only SPIRV-Cross for HLSL/MSL translation')
    parser.add_argument('--dxc', help='Pinned build-only DXC for ahead-of-time DXIL')
    parser.add_argument('--metal-sdk', choices=('macosx', 'iphoneos', 'iphonesimulator'), help='Compile previously translated MSL with pinned Apple tools')
    parser.add_argument('--metal-tool-lock', type=Path, help='Verified Xcode/SDK version and metal/metallib binary digest JSON')
    parser.add_argument('--developer-dir', help='Explicit selected Xcode, without changing system selection')
    parser.add_argument('--prebuilt', type=Path, help='Import verified shaders from this exact source revision without local translators')
    args = parser.parse_args()
    if args.prebuilt:
        import_bundle(args.prebuilt, args.output)
    if args.metal_sdk:
        if not args.metal_tool_lock:
            parser.error('--metal-sdk requires --metal-tool-lock from verified Apple tooling')
        compile_metal(args.output, args.metal_sdk, args.metal_tool_lock, developer_dir=args.developer_dir)
    elif not args.prebuilt:
        generate(args.output, args.glslang, naga=args.naga, spirv_cross=args.spirv_cross, dxc=args.dxc)


if __name__ == '__main__':
    main()
