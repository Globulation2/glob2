"""Generate renderer assets from the shared manifest; translators are build-only."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import subprocess
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


def _generator(root):
    script = Path(root) / 'tools/shaders/generate.py'
    specification = importlib.util.spec_from_file_location('glob2_renderer_shaders', script)
    generator = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(generator)
    return generator


def require_committed_metal_lock(environment=None):
    """Release and distribution builds set GLOB2_METAL_REQUIRE_COMMITTED_LOCK=1."""
    environment = os.environ if environment is None else environment
    return environment.get('GLOB2_METAL_REQUIRE_COMMITTED_LOCK', '') not in ('', '0')


def choose_metal_lock(root, sdk, output, strict, developer_dir=None, generator=None):
    """The tool lock for compiling Metal for sdk, as (lock, kind).

    A reviewed toolchain from the committed buildsys/metal-tool-lock.json when one
    matches this Mac's Xcode, SDK and compiler (iOS also needs the pinned Xcode).
    Otherwise a strict (release) build fails with the differences from the
    nearest entry, and a development build records what this Mac has beside the
    shaders, warns, and compiles with it (kind 'development').
    """
    generator = generator or _generator(root)
    committed_path = Path(root) / generator.COMMITTED_METAL_TOOL_LOCK
    try:
        committed = json.loads(committed_path.read_text())
    except (OSError, ValueError):
        committed = None
    entries = generator.metal_tool_lock_entries(committed, sdk)
    observed, _ = generator.observe_metal_tools(sdk, root, developer_dir, require_pin=strict and sdk != 'macosx')
    candidates = [(entry, generator.metal_tool_lock_differences(entry, observed)) for entry in entries]
    for entry, differences in candidates:
        if not differences:
            return entry, 'committed'
    nearest = min((differences for _, differences in candidates), key=len,
                  default=[('toolchains[].sdks.' + sdk, None, observed)])
    detail = ', '.join(key for key, _, _ in nearest)
    if strict:
        raise ValueError("This Mac's Metal toolchain does not match the committed lock " +
                         str(generator.COMMITTED_METAL_TOOL_LOCK) + ' for ' + sdk + ' (' + detail +
                         '); release builds compile Metal only with reviewed tools. ' +
                         generator.METAL_TOOL_LOCK_REMEDY)
    record = Path(output) / ('metal-development-lock-' + sdk + '.json')
    record.parent.mkdir(parents=True, exist_ok=True)
    record.write_text(json.dumps(observed, indent=2) + '\n')
    print('renderer shaders: compiling development-only Metal libraries for ' + sdk + ' with ' +
          observed['xcodeVersion'].splitlines()[0] + ' (not the committed lock: ' + detail + '); recorded in ' +
          str(record) + '. Release builds refuse these tools.', file=sys.stderr)
    return observed, 'development'


def apple_translators(strict):
    """Pinned glslang and SPIRV-Cross for Metal, built from verified source when missing.

    GLOB2_GLSLANG / GLOB2_SPIRV_CROSS override them (an empty value disables
    Metal). A development build carries on without Metal when they cannot be
    provisioned; a strict build fails.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import glslang_tool
    import spirv_cross_tool
    tools = []
    for module, variable in ((glslang_tool, 'GLOB2_GLSLANG'), (spirv_cross_tool, 'GLOB2_SPIRV_CROSS')):
        if variable in os.environ:
            tool = os.environ[variable] or None
        else:
            try:
                tool = str(module.ensure(quiet=True))
            except (ValueError, OSError, subprocess.CalledProcessError) as error:
                if strict:
                    raise ValueError('Cannot provision ' + module.__name__ + ' for Metal shaders: ' + str(error))
                print('renderer shaders: warning: ' + module.__name__ + ' unavailable, building without Metal: ' +
                      str(error), file=sys.stderr)
                tool = None
        if not tool and strict:
            raise ValueError(variable + ' is empty, but release builds need Metal shaders')
        tools.append(tool)
    return tools


def _compile_metal_or_warn(generator, directory, root, sdk, strict, developer_dir):
    """Compile Metal into directory; a development build without Xcode warns instead."""
    try:
        lock, kind = choose_metal_lock(root, sdk, directory.parent, strict, developer_dir, generator)
        generator.compile_metal(directory, sdk, lock, root, developer_dir, lock_kind=kind)
        return kind
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        if strict:
            raise
        print('renderer shaders: warning: building without Metal shaders (SDL GPU unavailable): ' + str(error),
              file=sys.stderr)
        return None


def prepare(root, output, apple_sdk=None):
    """Generate this build's renderer shaders under output/shaders.

    apple_sdk ('macosx' for the desktop app, 'iphoneos' for iOS device builds)
    also translates and compiles the SDL GPU Metal libraries automatically, with
    no environment needed. GLOB2_METAL_REQUIRE_COMMITTED_LOCK=1 (release and
    distribution workflows) makes missing tools or a non-committed lock fatal.
    """
    root, output = Path(root), Path(output)
    generator = _generator(root)
    directory = output / 'shaders'
    prebuilt = os.environ.get('GLOB2_RENDER_SHADER_INPUT')
    metal_sdk = os.environ.get('GLOB2_METAL_SDK') or (apple_sdk if prebuilt else None)
    metal_lock = os.environ.get('GLOB2_METAL_TOOL_LOCK')
    developer_dir = os.environ.get('DEVELOPER_DIR')
    strict = require_committed_metal_lock()
    if prebuilt:
        meta, _ = generator.verify_bundle(Path(prebuilt), root)
        identity = {'prebuiltMetadata': hashlib.sha256(json.dumps(meta, sort_keys=True).encode()).hexdigest()}
        if metal_sdk:
            identity.update({'metalSdk': metal_sdk, 'strict': strict,
                             'metalToolLock': hashlib.sha256(Path(metal_lock).read_bytes()).hexdigest() if metal_lock
                             else 'auto'})
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
            if metal_sdk and metal_lock:
                generator.compile_metal(directory, metal_sdk, Path(metal_lock), root, developer_dir)
            elif metal_sdk:
                _compile_metal_or_warn(generator, directory, root, metal_sdk, strict, developer_dir)
            receipt.write_text(json.dumps(identity, sort_keys=True) + '\n')
        return directory
    if metal_sdk:
        raise ValueError('GLOB2_METAL_SDK needs GLOB2_RENDER_SHADER_INPUT; Apple builds compile Metal '
                         'automatically from their own target instead')
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import glslang_tool
    naga = os.environ.get('GLOB2_NAGA')
    dxc = os.environ.get('GLOB2_DXC')
    if apple_sdk:
        compiler, spirv_cross = apple_translators(strict)
        if not spirv_cross:
            apple_sdk = None
    else:
        # GLOB2_GLSLANG overrides (empty disables); otherwise use the pinned compiler
        # that buildsys/glslang_tool.py provisioned, so dev builds get Vulkan SPIR-V.
        compiler = glslang_tool.resolve_for_build()
        spirv_cross = os.environ.get('GLOB2_SPIRV_CROSS')
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
    if apple_sdk:
        committed = root / generator.COMMITTED_METAL_TOOL_LOCK
        identity.update({'appleSdk': apple_sdk, 'strict': strict,
                         'committedMetalLock': hashlib.sha256(committed.read_bytes()).hexdigest()
                         if committed.is_file() else None})
    receipt = directory / '.inputs.json'
    current = receipt.is_file() and json.loads(receipt.read_text()) == identity
    complete = ((directory / 'RendererShaderLayouts.h').is_file() and (directory / 'webgl2/primitive.frag').is_file()
                and (not compiler or (directory / 'vulkan/primitive.frag.spv').is_file())
                and (not naga or (directory / 'webgpu/primitive.frag.wgsl').is_file())
                and (not apple_sdk or (directory / 'metal/primitive.frag.metallib').is_file()))
    if not current or not complete:
        receipt.unlink(missing_ok=True)
        generator.generate(directory, compiler, root, naga, spirv_cross, dxc)
        if apple_sdk and not _compile_metal_or_warn(generator, directory, root, apple_sdk, strict, developer_dir):
            identity['metal'] = 'unavailable'
            return directory  # No receipt: the next build tries Metal again.
        receipt.write_text(json.dumps(identity, sort_keys=True) + '\n')
    return directory


# Build-only shader files that packages leave out: receipts, the generated layout
# header, translated sources and intermediate AIR.
RUNTIME_SHADER_IGNORE = ('.*', 'RendererShaderLayouts.h', 'native-sources', '*.air')


def install_runtime_shaders(source, destination):
    """Copy the runtime shader artifacts (metadata, GLSL, SPIR-V, metallib) into a package.

    A build without generated shaders copies nothing; the package's SDL GPU check
    then reports what is missing (and fails release builds).
    """
    import shutil
    if not Path(source).is_dir():
        return
    shutil.copytree(source, destination, dirs_exist_ok=True, ignore=shutil.ignore_patterns(*RUNTIME_SHADER_IGNORE))


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
MOBILE_SDL_GPU = {'android': ('vulkan', 'spirv', '.spv'), 'ios': ('metal', 'metallib', '.metallib'),
                  'macos': ('metal', 'metallib', '.metallib')}


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
                  'Apple builds compile Metal automatically with full Xcode, pinned glslang and SPIRV-Cross '
                  '(python3 buildsys/spirv_cross_tool.py); see the build log for why they did not '
                  '(docs/architecture/rendering.md, "Apple Metal shaders").')
        raise ValueError('SDL GPU shaders are missing for ' + target + ' packaging: ' + '; '.join(problems[:6]) +
                         ('; ...' if len(problems) > 6 else '') + '. ' + remedy)
