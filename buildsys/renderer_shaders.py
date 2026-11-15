"""Generate renderer assets from the shared manifest; translators are build-only."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import sys


def inputs(root):
    """Checked-in files whose content decides the generated shader assets."""
    root = Path(root)
    sources = [root / 'tools/shaders/generate.py', root / 'libs/gag/shaders/renderer-manifest.json',
               root / 'libs/gag/shaders/skin-materials.json',
               root / 'libs/gag/include/SkinDeformation.h',
               root / 'libs/gag/include/DirectTerrainShader.h']
    sources.extend(sorted((root / 'libs/gag/shaders').glob('*.glsl')))
    return sources


def prepare(root, output):
    root, output = Path(root), Path(output)
    script = root / 'tools/shaders/generate.py'
    specification = importlib.util.spec_from_file_location('glob2_renderer_shaders', script)
    generator = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(generator)
    directory = output / 'shaders'
    prebuilt = os.environ.get('GLOB2_RENDER_SHADER_INPUT')
    metal_sdk = os.environ.get('GLOB2_METAL_SDK')
    metal_lock = os.environ.get('GLOB2_METAL_TOOL_LOCK')
    developer_dir = os.environ.get('DEVELOPER_DIR')
    if prebuilt:
        meta, _ = generator.verify_bundle(Path(prebuilt), root)
        identity = {'prebuiltMetadata': hashlib.sha256(json.dumps(meta, sort_keys=True).encode()).hexdigest()}
        if metal_sdk:
            if not metal_lock:
                raise ValueError('GLOB2_METAL_SDK requires verified GLOB2_METAL_TOOL_LOCK')
            identity.update({'metalSdk': metal_sdk, 'metalToolLock': hashlib.sha256(Path(metal_lock).read_bytes()).hexdigest()})
        receipt = directory / '.inputs.json'
        reusable = receipt.is_file() and json.loads(receipt.read_text()) == identity
        if reusable:
            try:
                installed, _ = generator.verify_bundle(directory, root)
                reusable = not metal_sdk or 'metal' in installed.get('shaderArtifacts', {})
            except (ValueError, OSError, KeyError):
                reusable = False
        if not reusable:
            generator.import_bundle(Path(prebuilt), directory, root)
            if metal_sdk:
                generator.compile_metal(directory, metal_sdk, Path(metal_lock), root, developer_dir)
            receipt.write_text(json.dumps(identity, sort_keys=True) + '\n')
        return directory
    if metal_sdk:
        raise ValueError('Apple offline compilation requires verified GLOB2_RENDER_SHADER_INPUT')
    # GLOB2_GLSLANG overrides (empty disables); otherwise use the pinned compiler
    # that buildsys/glslang_tool.py provisioned, so dev builds get Vulkan SPIR-V.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import glslang_tool
    compiler = glslang_tool.resolve_for_build()
    naga = os.environ.get('GLOB2_NAGA')
    spirv_cross = os.environ.get('GLOB2_SPIRV_CROSS')
    dxc = os.environ.get('GLOB2_DXC')
    sources = inputs(root)
    identity = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
    if compiler:
        compiler = str(Path(compiler).resolve())
        identity['compiler'] = hashlib.sha256(Path(compiler).read_bytes()).hexdigest()
    if naga:
        naga = str(Path(naga).resolve())
        identity['naga'] = hashlib.sha256(Path(naga).read_bytes()).hexdigest()
    for name, tool in [('spirv_cross', spirv_cross), ('dxc', dxc)]:
        if tool:
            identity[name] = hashlib.sha256(Path(tool).resolve().read_bytes()).hexdigest()
    directory = output / 'shaders'
    receipt = directory / '.inputs.json'
    if not receipt.is_file() or json.loads(receipt.read_text()) != identity or not (directory / 'RendererShaderLayouts.h').is_file() or not (directory / 'webgl2/primitive.frag').is_file() or (compiler and not (directory / 'vulkan/primitive.frag.spv').is_file()) or (naga and not (directory / 'webgpu/primitive.frag.wgsl').is_file()):
        generator.generate(directory, compiler, root, naga, spirv_cross, dxc)
        receipt.write_text(json.dumps(identity, sort_keys=True) + '\n')
    return directory


def verify_compute(root):
    # Compute shaders are embedded so headless/mobile packages need no asset lookup.
    compute_script = Path(root) / 'tools/shaders/gradient_compute.py'
    spec = importlib.util.spec_from_file_location('glob2_gradient_shaders', compute_script)
    compute = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compute)
    compute.verify(Path(root))
    rule_script = Path(root) / 'tools/shaders/gradient_preparation_rules.py'
    spec = importlib.util.spec_from_file_location('glob2_gradient_preparation_rules', rule_script)
    rules = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rules)
    rules.verify()



# SDL GPU driver family each mobile target needs, and the artifact format the
# runtime accepts for it (libs/gag/src/SDLGPUDevice.cpp sdlGPUUnavailableReason).
MOBILE_SDL_GPU = {'android': ('vulkan', 'spirv', '.spv'), 'ios': ('metal', 'metallib', '.metallib')}


def missing_sdl_gpu_shaders(directory, target):
    """Problems that would leave SDL GPU unavailable in a mobile package; empty when complete."""
    family, format_name, suffix = MOBILE_SDL_GPU[target]
    directory = Path(directory)
    try:
        meta = json.loads((directory / 'metadata.json').read_text())
    except (OSError, ValueError) as error:
        return ['renderer shader metadata unreadable: ' + str(error)]
    variants = meta.get('variants', [])
    artifacts = meta.get('shaderArtifacts', {}).get(family, {})
    problems = []
    if not artifacts:
        return ['no ' + family + ' (' + format_name + ') shader artifacts in ' + str(directory)]
    for variant in variants:
        for stage in ('vertex', 'fragment'):
            receipt = artifacts.get(variant, {}).get(stage)
            if not receipt:
                problems.append('missing ' + family + ' ' + variant + ' ' + stage + ' receipt')
                continue
            path = directory / receipt.get('path', '')
            if receipt.get('format') != format_name or path.suffix != suffix or not path.is_file():
                problems.append('missing or mismatched ' + receipt.get('path', '?'))
            elif hashlib.sha256(path.read_bytes()).hexdigest() != receipt.get('sha256'):
                problems.append('hash mismatch for ' + receipt['path'])
            elif format_name == 'spirv' and path.read_bytes()[:4] != bytes.fromhex('03022307'):
                problems.append('not SPIR-V: ' + receipt['path'])
            elif format_name == 'metallib' and not path.read_bytes().startswith(b'MTLB'):
                problems.append('not a Metal library: ' + receipt['path'])
    return problems


def require_sdl_gpu_shaders(directory, target):
    """Fail packaging loudly instead of shipping a mobile build without SDL GPU."""
    problems = missing_sdl_gpu_shaders(directory, target)
    if problems:
        remedy = ('Provision pinned glslang with python3 buildsys/glslang_tool.py (or set GLOB2_GLSLANG) '
                  'and rebuild.' if target == 'android' else
                  'Set GLOB2_RENDER_SHADER_INPUT to a verified translated bundle plus GLOB2_METAL_SDK and '
                  'GLOB2_METAL_TOOL_LOCK (docs/architecture/rendering.md, "Portable shader build artifacts").')
        raise ValueError('SDL GPU shaders are missing for ' + target + ' packaging: ' + '; '.join(problems[:6]) +
                         ('; ...' if len(problems) > 6 else '') + '. ' + remedy)
