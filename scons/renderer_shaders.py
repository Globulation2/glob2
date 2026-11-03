"""Generate renderer assets from the shared manifest; translators are build-only."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os


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
    compiler = os.environ.get('GLOB2_GLSLANG')
    naga = os.environ.get('GLOB2_NAGA')
    spirv_cross = os.environ.get('GLOB2_SPIRV_CROSS')
    dxc = os.environ.get('GLOB2_DXC')
    sources = [script, root / 'libs/gag/shaders/renderer-manifest.json',
               root / 'libs/gag/shaders/skin-materials.json',
               root / 'libs/gag/include/SkinDeformation.h']
    sources.extend(sorted((root / 'libs/gag/shaders').glob('*.glsl')))
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


def configure(env, root, output, browser=False):
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
    directory = prepare(root, output)
    env.Append(CPPPATH=[str(directory.resolve())])
    env.Append(CPPDEFINES=[('GLOB2_RENDER_SHADER_DIR', '\\"' + ('/shaders' if browser else str(directory.resolve())) + '\\"')])
    files = [str(path) for path in sorted(directory.rglob('*')) if path.is_file() and not path.name.startswith('.')
             and 'native-sources' not in path.relative_to(directory).parts
             and path.suffix not in ('.h', '.air')]
    env['RENDER_SHADER_FILES'] = files
    env['RENDER_SHADER_DIRECTORY'] = str(directory)
    return directory


def install(env):
    from SCons.Script import Alias
    directory = Path(env['RENDER_SHADER_DIRECTORY'])
    for source in env['RENDER_SHADER_FILES']:
        relative = Path(source).relative_to(directory)
        Alias('install', env.InstallAs(str(Path(env['INSTALLDIR']) / 'glob2/shaders' / relative), source))
