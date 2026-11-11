#!/usr/bin/env python3
"""Command-line entry points through which the CMake build calls
the Python generators in buildsys/.

Every subcommand writes only when the content changed, keeping timestamps (and
therefore Ninja and compiler-cache state) stable.
"""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

from build_layout import PACKAGE_VERSION, build_identity, default_directory, write_if_changed  # noqa: E402


def _touch(stamp):
    if stamp:
        Path(stamp).parent.mkdir(parents=True, exist_ok=True)
        Path(stamp).touch()


def build_arguments(args):
    """The key=value build arguments CMake was configured with."""
    arguments = {}
    for word in getattr(args, 'arg', None) or []:
        key, separator, value = word.partition('=')
        if not separator or not key:
            raise SystemExit('build arguments are key=value words, got ' + repr(word))
        arguments[key] = value
    return arguments


def build_config(args):
    import build_config
    identity = build_identity(build_arguments(args))
    config = build_config.ConfigurationText()
    build_config.add_package(config, datadir=args.datadir, version=PACKAGE_VERSION, font=args.font,
                             darwin=identity['toolchain'] == 'darwin', windows=identity['toolchain'] == 'mingw',
                             china=bool(identity.get('china')), identity=identity)
    build_config.add_native_wss(config)
    if args.opengl:
        build_config.add_opengl(config)
    if args.fribidi:
        build_config.add_fribidi(config)
    if args.portaudio:
        build_config.add_portaudio(config)
    write_if_changed(Path(args.builddir) / 'include/glob2/BuildConfig.h', config.text())


def compile_profile(args):
    """glob2/CompileProfile.h, for every native build."""
    from sources import profile_header
    write_if_changed(Path(args.builddir) / 'include/glob2/CompileProfile.h', profile_header(args.profile))


def build_paths(args):
    import checkout_paths
    checkout_paths.write(args.builddir, PACKAGE_SOURCE_DIR=str(ROOT),
                         PACKAGE_RUNTIME_ASSET_DIR=args.runtime_assets,
                         GLOB2_RENDER_SHADER_DIR=str((Path(args.builddir) / 'shaders').resolve()))


def build_stamp(args):
    import checkout_paths
    checkout_paths.write_stamp(args.builddir, ROOT)


def skin_materials(args):
    import skin_materials
    skin_materials.generate(ROOT, args.builddir)
    _touch(args.stamp)


def shaders(args):
    import renderer_shaders
    renderer_shaders.verify_compute(ROOT)
    renderer_shaders.prepare(ROOT, Path(args.builddir))
    _touch(args.stamp)


def shader_inputs(args):
    import renderer_shaders
    print(';'.join(path.as_posix() for path in renderer_shaders.inputs(ROOT)))


def hive_api(args):
    import hive_api
    source = ROOT / 'services/online/packages/hive/src/commander-api.txt'
    write_if_changed(Path(args.builddir) / 'include/hive/PlayerAPIContract.h',
                     hive_api.render(source.read_text(encoding='utf-8')))
    _touch(args.stamp)


def skin_render(args):
    import skin_render
    write_if_changed(Path(args.builddir) / 'src/SkinRenderRecipe.h',
                     skin_render.content(ROOT, skin_render.inputs(ROOT)))
    _touch(args.stamp)


def skin_render_inputs(args):
    import skin_render
    print(';'.join(path.as_posix() for path in skin_render.inputs(ROOT)))


def official_instance(args):
    import official_instance
    origin = official_instance.origin(build_arguments(args))
    print(json.dumps({'origin': origin, 'former': ' '.join(official_instance.former_origins(origin))}))


def recording(args):
    """The h264 recording archives and system libraries recording_dependencies.attach links."""
    import recording_dependencies
    prefix = Path(args.prefix).resolve()
    manifest = prefix / 'recording-manifest.json'
    identity = json.loads(manifest.read_text())['identity'] if manifest.exists() else {}
    if identity.get('video_codec', 'h264') != args.codec:
        raise SystemExit('Recording prefix codec does not match recording_codec=' + args.codec)
    archives = [(prefix / 'lib' / ('lib' + name + '.a')).as_posix()
                for name in recording_dependencies.codec_libraries(args.codec)]
    if args.target == 'mingw32':
        system = ['mfplat', 'mfuuid', 'ole32', 'strmiids', 'bcrypt']
    elif args.target == 'darwin':
        system = ['-framework ' + name for name in ('VideoToolbox', 'CoreMedia', 'CoreVideo', 'Foundation', 'AudioToolbox')]
    else:
        system = (['va', 'va-drm'] if identity.get('vaapi_api') else []) + ['dl', 'm']
    print(';'.join(archives + system))


def recording_prefix(args):
    """Provision the pinned recording libraries and print their prefix.

    GLOB2_RECORDING_PREFIX, or a prefix with recording/ beside GLOB2_SDL3_PREFIX,
    is used as given; otherwise the shared dependency store builds or reuses one
    keyed by the recipe and compilers."""
    from recording_dependencies import build as build_recording, native_opus_configuration
    from shared_dependencies import ensure
    sdl = os.environ.get('GLOB2_SDL3_PREFIX')
    # The prefix beside the SDL3 one holds the h264 build only.
    shared = Path(sdl) / 'recording' if sdl and args.codec == 'h264' else None
    has_shared = bool(shared and (shared / 'recording-manifest.json').exists())
    explicit = os.environ.get('GLOB2_RECORDING_PREFIX')
    builddir = Path(args.builddir).resolve()
    prefix = Path(explicit) if explicit else shared if has_shared else builddir / 'recording/prefix'
    # CMake reads the prefix from stdout: send the builders' output (and their
    # child processes') to stderr meanwhile.
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        prefix = ensure(build_recording, prefix.resolve(), builddir / 'recording/sources',
                        explicit=bool(explicit or has_shared), execute=True,
                        cc=args.cc, cxx=args.cxx, ar=args.ar, ranlib=args.ranlib, target=args.target,
                        video_codec=args.codec, environment=dict(os.environ), jobs=args.jobs,
                        opus_configuration=native_opus_configuration(os.environ) if args.codec != 'h264' else None)
        sys.stdout.flush()
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    print(Path(prefix).resolve().as_posix())


def lean_image_prefix(args):
    """The private PNG/JPEG/WebP SDL_image a lean_images=1 release links, built on
    first use; also records it in image-runtime.json."""
    from native_image_dependency import ensure
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        prefix = ensure(ROOT, cc=args.cc, cxx=args.cxx, jobs=args.jobs, environment=dict(os.environ))
        sys.stdout.flush()
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    write_if_changed(Path(args.builddir) / 'image-runtime.json', json.dumps({'prefix': str(prefix)}) + '\n')
    print(Path(prefix).resolve().as_posix())


def _mobile(args):
    """The build identity, its dependency identity and the cross toolchain of a mobile build."""
    from dev_build import dependency_identity as application_dependency_identity
    from mobile_toolchain import discover
    arguments = build_arguments(args)
    identity = build_identity(arguments)
    if identity['target'] not in ('android', 'ios'):
        raise SystemExit('not a mobile build: ' + json.dumps(identity))
    dependency_identity = application_dependency_identity(identity)
    # The Amazon flavor changes game behavior, not its pinned native dependencies.
    dependency_identity.pop('amazon', None)
    return arguments, identity, dependency_identity, discover(dependency_identity, arguments)


def mobile_toolchain(args):
    """The cross compilers and their flags, which CMake needs before project()."""
    from dev_store import command_path
    _, identity, _, toolchain = _mobile(args)
    ar = Path(command_path(toolchain['ar']))
    ranlib = ar.with_name('llvm-ranlib' if identity['target'] == 'android' else 'ranlib')
    flags = toolchain['cflags']
    sysroot = flags[flags.index('-isysroot') + 1] if '-isysroot' in flags else ''
    print(json.dumps({'cc': Path(command_path(toolchain['cc'])).as_posix(),
                      'cxx': Path(command_path(toolchain['cxx'])).as_posix(),
                      'ar': ar.as_posix(), 'ranlib': ranlib.as_posix(), 'cflags': flags,
                      'ldflags': toolchain['ldflags'], 'sysroot': sysroot,
                      'system': 'Linux' if identity['target'] == 'android' else 'Darwin'}))


def _cmake_list(name, values):
    quoted = ['"' + str(value).replace('\\', '/').replace('"', '\\"') + '"' for value in values]
    return 'set(' + name + '\n  ' + '\n  '.join(quoted) + ')\n' if values else 'set(' + name + ')\n'


def mobile_configure(args):
    """What buildsys/mobile_build.py does before compiling: verify the pinned
    dependency bundle, write the generated headers, provision the recording
    libraries and stage their notices. Writes mobile-configuration.cmake."""
    import hashlib
    import shutil
    from dev_store import dependency_prefix, dependency_key, command_path, adopt
    from mobile_artifacts import verify_android_library
    from recording_dependencies import build as build_recording, codec_libraries
    from shared_dependencies import ensure
    from client_features import header as client_feature_header
    import official_instance
    import skin_materials
    import checkout_paths
    import renderer_shaders
    arguments, identity, dependency_identity, toolchain = _mobile(args)
    output = Path(args.builddir).resolve()
    prefix = (adopt(arguments['mobile_deps']) if arguments.get('mobile_deps')
              else dependency_prefix(ROOT, dependency_identity, toolchain['fingerprint']))
    manifest = prefix / 'manifest.json'
    if not manifest.is_file():
        raise SystemExit(f'Missing cross-compiled dependency manifest {manifest}; see docs/mobile/development.md. '
                         'Host libraries are never used.')
    dependencies = json.loads(manifest.read_text())
    if dependencies.get('identity') != dependency_identity or dependencies.get('toolchain') != toolchain['fingerprint']:
        raise SystemExit('Mobile dependencies belong to a different target/compiler configuration')
    if dependencies.get('dependency_key') and dependencies['dependency_key'] != dependency_key(
            ROOT, dependency_identity, toolchain['fingerprint']):
        raise SystemExit('Mobile dependency inputs changed; rebuild dependencies')
    def verified(name, digest):
        path = (prefix / name).resolve()
        if not path.is_relative_to(prefix) or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit('Mobile dependency checksum mismatch: ' + str(path))
        return path
    for name, digest in dependencies.get('files', {}).items():
        verified(name, digest)
    archives = dependencies.get('archives', {})
    if not archives:
        raise SystemExit('Mobile dependency manifest has no verified archives')
    libraries = []
    for name, digest in archives.items():
        path = verified(name, digest)
        if identity['target'] == 'android':
            verify_android_library(path, identity['arch'])
        libraries.append(path.as_posix())
    write_if_changed(output / 'include/glob2/BuildConfig.h', f'''#pragma once
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "{PACKAGE_VERSION}"
#define PACKAGE_DATA_DIR "."
#define PRIMARY_FONT "sans.ttf"
#define GLOB2_MOBILE 1
#define GLOB2_NATIVE_WSS 1
#define GLOB2_NO_VOICE 1
''' + ('#define GLOB2_PROGRAMMABLE_GL 1\n#define GLOB2_GLES3 1\n' if identity['target'] == 'android' else '')
        + ('#define GLOB2_CHINA_RELEASE 1\n' if identity.get('china') else '')
        + ('#define GLOB2_AMAZON_RELEASE 1\n' if identity.get('amazon') else '') + client_feature_header(identity))
    skin_materials.generate(ROOT, output)
    renderer_shaders.verify_compute(ROOT)
    renderer_shaders.prepare(ROOT, output)
    checkout_paths.write(output, PACKAGE_SOURCE_DIR='.', GLOB2_RENDER_SHADER_DIR=str((output / 'shaders').resolve()))
    environment = dict(os.environ)
    # Cross builds must not inherit host include/library injection.
    for name in ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET'):
        environment.pop(name, None)
    environment.update(TMPDIR=str(output / 'tmp'), TMP=str(output / 'tmp'), TEMP=str(output / 'tmp'))
    (output / 'tmp').mkdir(parents=True, exist_ok=True)
    explicit = os.environ.get('GLOB2_RECORDING_PREFIX')
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        ar = Path(command_path(toolchain['ar']))
        recording_prefix = ensure(
            build_recording, Path(explicit or output / 'recording/prefix'), output / 'recording/sources',
            explicit=bool(explicit), execute=True, jobs=args.jobs, sdk_identity=toolchain['fingerprint'],
            cc=command_path(toolchain['cc']), cxx=command_path(toolchain['cxx']), ar=str(ar),
            ranlib=str(ar.with_name('llvm-ranlib' if identity['target'] == 'android' else 'ranlib')),
            target=identity['target'], arch=identity['arch'], cflags=toolchain['cflags'],
            ldflags=toolchain['ldflags'], environment=environment)
        sys.stdout.flush()
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    recording_prefix = Path(recording_prefix).resolve()
    recording = [(recording_prefix / 'lib' / ('lib' + name + '.a')).as_posix() for name in codec_libraries('h264')]
    recording_link_options = []
    if identity['target'] == 'android':
        recording.append('mediandk')
        # Static FFmpeg assembly references private tables directly: bind only the
        # codec archives' symbols inside the JNI library (recording_dependencies.attach).
        recording_link_options.append('-Wl,--exclude-libs=' + ':'.join(
            'lib' + name + '.a' for name in codec_libraries('h264')))
        notices = sorted((recording_prefix / 'share/licenses/recording').glob('*'))
        if not notices or any(not path.is_file() for path in notices):
            raise SystemExit('Resolved recording prefix contains no complete recording notices: ' + str(recording_prefix))
        for notice in notices:
            staged = output / 'licenses/recording' / notice.name
            staged.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, staged)
    else:
        recording += ['-framework ' + name for name in ('VideoToolbox', 'CoreMedia', 'CoreVideo', 'Foundation', 'AudioToolbox')]
    write_if_changed(output / 'toolchain.json', json.dumps(toolchain, indent=2, sort_keys=True) + '\n')
    origin = official_instance.origin(arguments)
    content = ('# Generated by buildsys/cmake_generate.py mobile-configure; do not edit.\n'
               + _cmake_list('GLOB2_MOBILE_PREFIX', [prefix.as_posix()])
               + _cmake_list('GLOB2_MOBILE_LIBRARIES', libraries)
               + _cmake_list('GLOB2_MOBILE_MANIFEST', [manifest.as_posix()])
               + _cmake_list('GLOB2_MOBILE_RECORDING_PREFIX', [recording_prefix.as_posix()])
               + _cmake_list('GLOB2_MOBILE_RECORDING_LIBRARIES', recording)
               + _cmake_list('GLOB2_MOBILE_RECORDING_LINK_OPTIONS', recording_link_options)
               + _cmake_list('GLOB2_MOBILE_OFFICIAL_ORIGIN', [origin])
               + _cmake_list('GLOB2_MOBILE_OFFICIAL_FORMER_ORIGINS', [' '.join(official_instance.former_origins(origin))]))
    write_if_changed(output / 'mobile-configuration.cmake', content)


def embedded_provenance(args):
    """glob2/TestBuildProvenance.h: the identity mobile and browser test executables
    carry."""
    import test_provenance
    metadata = test_provenance._source_identity(ROOT)
    metadata.update(json.loads(Path(args.configuration).read_text()))
    contents = '#pragma once\n// Generated test evidence identity; compiled into the producer.\n'
    contents += '#define GLOB2_TEST_PROVENANCE_EMBEDDED 1\n'
    contents += '#define GLOB2_TEST_PROVENANCE_JSON ' + json.dumps(json.dumps(metadata, sort_keys=True)) + '\n'
    write_if_changed(Path(args.builddir) / 'include/glob2/TestBuildProvenance.h', contents)


WEB_PORTS = ['--use-port=zlib']


def _web_sdk(arguments):
    """The pinned Emscripten SDK (buildsys/web_build.py), its em++ checked against the lock."""
    import subprocess
    from dev_store import browser_sdk
    sdk = browser_sdk(ROOT, arguments.get('emsdk', os.environ.get('EMSDK')))
    compiler = sdk / 'upstream/emscripten/em++'
    if not compiler.exists():
        raise SystemExit('Emscripten SDK not found; run python3 platforms/web/setup.py or pass emsdk=/path/to/emsdk')
    lock = json.loads((ROOT / 'platforms/web/toolchain.json').read_text())
    version = subprocess.check_output([str(compiler), '--version'], text=True)
    if lock['emscripten'] not in version.splitlines()[0]:
        raise SystemExit('This target requires Emscripten ' + lock['emscripten'])
    return sdk, lock


def _web_environment(sdk, output, threaded):
    """EM_CACHE, EM_PORTS and EM_CONFIG of one browser variant, as web_build.py sets them."""
    from dev_store import cache, isolated, key
    shared = output if isolated() else cache(ROOT, 'emscripten-' + key(ROOT, [
        'platforms/web/toolchain.json', 'buildsys/sdl3-versions.json', 'buildsys/sdl3-vendored.json',
        'buildsys/opus-versions.json', 'buildsys/opus_dependencies.py'], [WEB_PORTS, threaded]))
    environment = {'EM_CACHE': (Path(shared) / 'cache').as_posix(), 'EM_PORTS': (Path(shared) / 'ports').as_posix()}
    if os.environ.get('EM_CONFIG'):
        environment['EM_CONFIG'] = os.environ['EM_CONFIG']
    elif (sdk / '.emscripten').is_file():
        # emsdk's template derives paths from EM_CONFIG; anchor it to the selected SDK.
        configuration = '\n'.join('emsdk_path = ' + repr(str(sdk)) if line.startswith('emsdk_path =') else line
                                  for line in (sdk / '.emscripten').read_text().splitlines()) + '\n'
        write_if_changed(output / '.emscripten', configuration)
        environment['EM_CONFIG'] = (output / '.emscripten').as_posix()
    return environment


def web_toolchain(args):
    """The Emscripten SDK and the serial variant's environment, which CMake needs before project()."""
    arguments = build_arguments(args)
    sdk, _ = _web_sdk(arguments)
    output = Path(args.builddir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    print(json.dumps({'emscripten': (sdk / 'upstream/emscripten').as_posix(),
                      'environment': _web_environment(sdk, output, False)}))


def web_configure(args):
    """What buildsys/web_build.py does before compiling, for both variants: the
    pinned SDL3 and Opus builds, BuildConfig.h, the skin materials, the browser
    shaders and BuildPaths.h; then the music Opus and the wasm recording
    libraries. Writes web-configuration.cmake."""
    import renderer_shaders
    import skin_materials
    import checkout_paths
    import official_instance
    from client_features import header as client_feature_header
    from shared_dependencies import ensure
    from sdl3_dependencies import build as build_sdl3
    from opus_dependencies import build as build_opus, LIBRARIES as OPUS_LIBRARIES
    from recording_dependencies import build as build_recording, codec_libraries
    from build_layout import prepare_directory
    arguments = build_arguments(args)
    identity = build_identity(arguments)
    sdk, lock = _web_sdk(arguments)
    emscripten = sdk / 'upstream/emscripten'
    root = Path(args.builddir).resolve()
    variant = identity.get('web_variant', 'both')
    variants = [name for name in ('serial', 'threaded') if variant in ('both', name)]
    legacy_gl = identity.get('web_legacy_gl', True)
    origin = official_instance.origin(arguments)
    lines = ['# Generated by buildsys/cmake_generate.py web-configure; do not edit.\n',
             _cmake_list('GLOB2_WEB_VARIANTS', variants),
             _cmake_list('GLOB2_WEB_OFFICIAL_ORIGIN', [origin]),
             _cmake_list('GLOB2_WEB_OFFICIAL_FORMER_ORIGINS', [' '.join(official_instance.former_origins(origin))]),
             _cmake_list('GLOB2_WEB_LEGACY_GL', ['ON' if legacy_gl else 'OFF']),
             _cmake_list('GLOB2_WEB_OPUS_LIBRARIES', list(OPUS_LIBRARIES))]
    renderer_shaders.verify_compute(ROOT)
    serial_environment = None
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        for name in variants:
            threaded = name == 'threaded'
            output = root / 'threaded' if threaded else root
            (output / 'tmp').mkdir(parents=True, exist_ok=True)
            if threaded:
                prepare_directory(output, dict(identity, web_threads=True))
            environment = _web_environment(sdk, output, threaded)
            build_environment = dict(os.environ, **environment)
            build_environment.update(TMPDIR=str(output / 'tmp'), TMP=str(output / 'tmp'), TEMP=str(output / 'tmp'))
            if not threaded or serial_environment is None:
                serial_environment = build_environment
            sdl = ensure(build_sdl3, output / 'sdl3/prefix', output / 'sdl3/sources', execute=True, jobs=args.jobs,
                         emscripten=emscripten, environment=build_environment, threaded=threaded)
            opus = ensure(build_opus, output / 'opus/prefix', output / 'opus/sources', execute=True, jobs=args.jobs,
                          emscripten=emscripten, environment=build_environment, threaded=threaded)
            write_if_changed(output / 'include/glob2/BuildConfig.h', '#pragma once\n'
                + ('#define HAVE_OPENGL 1' if legacy_gl else '') + '\n'
                + '#define GLOB2_PROGRAMMABLE_GL 1\n#define GLOB2_WEBGL2 1\n#define PACKAGE "glob2"\n'
                + '#define PACKAGE_NAME "Globulation 2"\n#define PACKAGE_VERSION "' + PACKAGE_VERSION + '"\n'
                + '#define PACKAGE_DATA_DIR "/"\n#define PRIMARY_FONT "sans.ttf"\n' + client_feature_header(identity))
            skin_materials.generate(ROOT, output)
            shaders = renderer_shaders.prepare(ROOT, output)
            checkout_paths.write(output, PACKAGE_SOURCE_DIR='/', GLOB2_RENDER_SHADER_DIR='/shaders')
            write_if_changed(output / 'options.json', json.dumps(arguments, sort_keys=True, indent=2) + '\n')
            prefix = 'GLOB2_WEB_' + name.upper()
            lines += [_cmake_list(prefix + '_OUT', [output.as_posix()]),
                      _cmake_list(prefix + '_SDL_PREFIX', [Path(sdl).resolve().as_posix()]),
                      _cmake_list(prefix + '_OPUS_PREFIX', [Path(opus).resolve().as_posix()]),
                      _cmake_list(prefix + '_SHADERS', [Path(shaders).resolve().as_posix()]),
                      _cmake_list(prefix + '_WEBGPU', ['ON' if (Path(shaders) / 'webgpu').is_dir() else 'OFF']),
                      _cmake_list(prefix + '_ENVIRONMENT', [key + '=' + value for key, value in sorted(environment.items())])]
        # The music decoder has its own (non-threaded) Opus; one recording module serves both.
        music = ensure(build_opus, root / 'music-opus/prefix', root / 'music-opus/sources', execute=True,
                       jobs=args.jobs, emscripten=emscripten, environment=serial_environment, threaded=False)
        explicit = os.environ.get('GLOB2_RECORDING_PREFIX')
        recording = ensure(build_recording, Path(explicit or root / 'recording/prefix'), root / 'recording/sources',
                           explicit=bool(explicit), execute=True, jobs=args.jobs,
                           cc=str(emscripten / 'emcc'), cxx=str(emscripten / 'em++'), ar=str(emscripten / 'emar'),
                           ranlib=str(emscripten / 'emranlib'), target='wasm', arch='wasm32', sdk_identity=lock,
                           cflags=['-msimd128', '-fwasm-exceptions'], environment=serial_environment)
        sys.stdout.flush()
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    recording = Path(recording).resolve()
    lines += [_cmake_list('GLOB2_WEB_MUSIC_OPUS_PREFIX', [Path(music).resolve().as_posix()]),
              _cmake_list('GLOB2_WEB_RECORDING_PREFIX', [recording.as_posix()]),
              _cmake_list('GLOB2_WEB_RECORDING_LIBRARIES',
                          [(recording / 'lib' / ('lib' + name + '.a')).as_posix() for name in codec_libraries('h264')])]
    write_if_changed(root / 'web-configuration.cmake', ''.join(lines))


def web_page(args):
    """index.html and its studio siblings, with the WebAssembly sizes the page shows
    download progress against (buildsys/web_build.py shell)."""
    import re
    directory = Path(args.builddir)
    web = ROOT / 'platforms/web'
    page = (web / 'shell.html').read_text().replace('/* AI_STUDIO_BRIDGE */', '\n'.join(
        (web / name).read_text() for name in ('studio-envelope.js', 'generator-studio.js', 'studio.js', 'set-preview.js'))
    ).replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>')
    variants = args.variant
    sizes = {('threaded/' if name == 'threaded' else '') + 'index.wasm':
             (directory / ('threaded' if name == 'threaded' else '') / 'index.wasm') for name in variants}
    page = page.replace('<script src="loader.js"></script>', '<script>globalThis.Glob2BuildVariants='
                        + json.dumps(['serial', 'threaded'] if len(variants) == 2 else variants)
                        + ';</script><script src="loader.js"></script>')
    sizes = json.dumps({name: path.stat().st_size for name, path in sizes.items()}, separators=(',', ':'), sort_keys=True)
    page, count = re.subn(r'data-wasm-bytes="0"', f"data-wasm-bytes='{sizes}'", page, count=1)
    if count != 1:
        raise SystemExit('platforms/web/shell.html lacks the data-wasm-bytes placeholder')
    for name in ('index.html', 'studio.html', 'set-preview.html', 'generator-studio.html'):
        write_if_changed(directory / name, page)


def web_assets_export(args):
    """The browser's runtime-assets export and its packaged data (asset-manifest.js)."""
    sys.path.insert(0, str(ROOT))
    import shared_assets
    import web_assets
    from runtime_assets import sprite_packing_profile
    from tools.package_assets import source_files, export_assets
    arguments = build_arguments(args)
    identity = build_identity(arguments)
    release = identity['mode'] == 'release'
    output = Path(args.builddir).resolve()
    asset_root = output / 'runtime-assets'
    packing = sprite_packing_profile(arguments.get('sprite_packing', 'auto'), release)
    inputs = list(source_files(ROOT, 'web'))
    tools = [ROOT / path for path in WEB_ASSET_TOOLS]

    def produce(destination):
        export_assets(ROOT, destination, platform='web', optimized=True, lossy=release, jobs=args.jobs,
                      terrain_sheets=packing != 'legacy', packed_hd_sprites=packing != 'legacy',
                      video_sprites=packing == 'video')
    key = shared_assets.content_key(ROOT, inputs + tools, ['runtime-assets-v1', 'web', release, packing])
    shared_assets.export(ROOT, asset_root, key, produce)  # also writes runtime-assets.json
    web_assets.build(ROOT, output, str(output / 'asset-manifest.js'), asset_root)
    # The packaging plan also reads the browser copies and the game's sprite, terrain and building tables.
    plan = [ROOT / path for path in WEB_PLAN_INPUTS]
    plan += [path for path in (ROOT / 'platforms/web/assets').glob('*') if path.is_file()]
    plan += sorted((ROOT / 'src/building/types').glob('BuildingTypes*.cpp'))
    plan += sorted((ROOT / 'content/data/buildings').rglob('*.json'))
    plan += sorted((ROOT / 'content/data/resources').rglob('*.json'))
    plan += sorted((ROOT / 'content/data/terrain/materials').glob('*.json'))
    files = [Path(path).resolve() for path in inputs] + tools + plan
    write_if_changed(args.depfile, _depfile_path(output / 'asset-manifest.js') + ': '
                     + ' \\\n  '.join(_depfile_path(path) for path in files) + '\n')


# web_build.py's asset export and packaging inputs beyond the exported sources.
WEB_ASSET_TOOLS = ('tools/package_assets.py', 'tools/packed_sprite_assets.py', 'buildsys/recording_dependencies.py',
                   'buildsys/recording-versions.json', 'buildsys/runtime_assets.py', 'buildsys/shared_assets.py',
                   'buildsys/web_assets.py', 'tools/terrain_tileset.py', 'tools/terrain_catalog.py',
                   'tools/asset-requirements.txt', 'tools/image_encoding.json')
WEB_PLAN_INPUTS = ('deploy/sim_version.py', 'platforms/web/derive_assets.py', 'src/app/GlobalContainer.cpp',
                   'src/map/TerrainPresentation.h', 'content/data/terrain/tileset.json',
                   'content/data/resources/registry.json')


def web_fonts(args):
    """The launcher's same-origin script fonts (web_assets.launcher_script_fonts)."""
    import web_assets
    web_assets.launcher_script_fonts(ROOT, args.builddir)


def package_version(args):
    print(PACKAGE_VERSION)


def mac_image_prefix(args):
    """The pinned PNG/JPEG SDL_image macOS bundles link (mac_image_dependency), built on
    first use for the bundle and package targets."""
    from mac_image_dependency import ensure
    environment = dict(os.environ)
    if args.sdl_prefix:
        environment['GLOB2_SDL3_PREFIX'] = str(Path(args.sdl_prefix).resolve())
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        prefix = ensure(ROOT, jobs=args.jobs, environment=environment)
        sys.stdout.flush()
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    print(Path(prefix).resolve().as_posix())


def bundle(args):
    """Glob2.app in the build directory (buildsys/bundle.py)."""
    sys.path.insert(0, str(ROOT))  # bundle.py imports tools.package_assets
    import bundle as mac_bundle
    arguments = build_arguments(args)
    environment = mac_bundle.bundle_environment(
        Path(args.builddir).resolve().as_posix(), args.binary, args.lean_image_prefix, args.recording_prefix,
        PACKAGE_VERSION, args.jobs, arguments.get('sprite_packing', 'auto'))
    mac_bundle.createBundle(None, None, environment)


def dmg(args):
    """The release disk image of an application bundle (buildsys/dmg.py)."""
    import dmg as disk_image
    disk_image.create_dmg([args.output], [args.app])


# content/data files installed beside the game.
DESKTOP_FILES = (('content/data/glob2.desktop', 'applications/org.globulation2.Globulation2.desktop'),
                 ('content/data/org.globulation2.Globulation2.metainfo.xml',
                  'metainfo/org.globulation2.Globulation2.metainfo.xml'),
                 ('content/data/completions/glob2.bash', 'bash-completion/completions/glob2'),
                 ('content/data/completions/_glob2', 'zsh/site-functions/_glob2'),
                 ('content/data/completions/glob2.fish', 'fish/vendor_completions.d/glob2.fish'),
                 ('platforms/linux/glob2.6', 'man/man6/glob2.6'),
                 *(('content/data/icons/glob2-icon-%dx%d.png' % (size, size),
                    'icons/hicolor/%dx%d/apps/glob2.png' % (size, size)) for size in (16, 24, 32, 48, 64, 128, 256)))


def _copy(source, destination):
    """Copy a file, a symbolic link as a link."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_symlink() or destination.exists():
        destination.unlink()
    if source.is_symlink():
        destination.symlink_to(os.readlink(source))
    else:
        import shutil
        shutil.copy2(source, destination)


def install(args):
    """Everything installed beside the glob2 executable, under $DESTDIR.

    The verified runtime-asset tree merges into <installdir>/glob2
    (runtime_assets.install_export); then the shaders, desktop integration,
    completions and manual; the SDL3 runtime and its notices when the build
    used GLOB2_SDL3_PREFIX; the recording notices; and a lean_images build's
    private SDL_image with its soname links and notice."""
    import runtime_assets
    destdir = os.environ.get('DESTDIR', '')
    def staged(path):
        return Path(destdir + str(path)) if destdir else Path(path)
    builddir = Path(args.builddir).resolve()
    installdir, bindir = Path(args.installdir), Path(args.bindir)
    runtime_assets.install_export(builddir / 'runtime-assets', staged(installdir / 'glob2'))
    shaders = builddir / 'shaders'
    for path in sorted(shaders.rglob('*')):
        relative = path.relative_to(shaders)
        if (path.is_file() and not path.name.startswith('.') and 'native-sources' not in relative.parts
                and path.suffix not in ('.h', '.air')):
            _copy(path, staged(installdir / 'glob2/shaders' / relative))
    for source, destination in DESKTOP_FILES:
        _copy(ROOT / source, staged(installdir / destination))
    libraries = staged(bindir.parent / 'lib/glob2')
    if args.sdl_prefix:
        sdl = Path(args.sdl_prefix).resolve()
        for path in sorted((sdl / 'lib').glob('libSDL3*.so*')):
            if not (args.lean_image_prefix and path.name.startswith('libSDL3_image')):
                _copy(path, libraries / path.name)
        for notice in sorted((sdl / 'share/licenses').glob('SDL3*/LICENSE.txt')):
            _copy(notice, staged(installdir / 'glob2/licenses' / notice.parent.name / notice.name))
    if args.recording_prefix:
        for notice in sorted((Path(args.recording_prefix) / 'share/licenses/recording').glob('*')):
            _copy(notice, staged(installdir / 'glob2/licenses/recording' / notice.name))
    if args.lean_image_prefix:
        # install_image_runtime.install_runtime: one real library, soname links to it.
        prefix = Path(args.lean_image_prefix)
        sources = sorted((prefix / 'lib').glob('libSDL3_image*.so*'))
        real = {path.resolve() for path in sources}
        if len(real) != 1:
            raise SystemExit('Lean image prefix must contain exactly one SDL_image runtime')
        canonical = next(iter(real))
        _copy(canonical, libraries / canonical.name)
        for path in sources:
            if path.name != canonical.name:
                link = libraries / path.name
                if link.is_symlink() or link.exists():
                    link.unlink()
                link.symlink_to(canonical.name)
        notice = prefix / 'share/licenses/SDL3_image/LICENSE.txt'
        if not notice.is_file():
            raise SystemExit('Lean image prefix is missing its license notice')
        _copy(notice, staged(installdir / 'doc/glob2/SDL3_image-LICENSE.txt'))


def ccache_directory(args):
    """The managed compiler cache, configured."""
    if os.environ.get('CCACHE_DIR'):
        import dev_store
        print(Path(dev_store.adopt(os.environ['CCACHE_DIR'])).as_posix())
        return
    import ccache
    import dev_store
    managed = dev_store.cache(ROOT, 'ccache', lease=False)
    ccache.configure_managed(managed)
    print(Path(managed).as_posix())


def configure(args):
    """Resolve the build arguments, write identity.json and print the identity.

    test/run_tests.py, tools/perf_ab.py and tools/dev_baseline.py read identity.json
    (build_layout.prepare_directory refuses a mismatched identity). CMake reads the printed
    JSON: the resolved identity and its default build directory."""
    arguments = build_arguments(args)
    try:
        resolved = build_identity(arguments)
    except ValueError as error:
        raise SystemExit('invalid build arguments: ' + str(error))
    native = resolved['target'] == 'native' and resolved['toolchain'] in ('linux', 'darwin', 'mingw')
    if not native and resolved['target'] not in ('android', 'ios', 'web'):
        raise SystemExit('the CMake build supports native Linux, macOS and MinGW, Android and iOS builds; got '
                         + json.dumps(resolved))
    content = dict(resolved, generator='cmake')
    write_if_changed(Path(args.builddir) / 'identity.json', json.dumps(content, sort_keys=True, indent=2) + '\n')
    print(json.dumps({'identity': resolved, 'directory': (ROOT / default_directory(resolved)).as_posix(),
                      'arguments': arguments}, sort_keys=True))


def test_provenance(args):
    """<binary>.provenance.json, the test binary's build identity."""
    import test_provenance
    configuration = json.loads(Path(args.configuration).read_text())
    test_provenance.write_sidecar(ROOT, args.binary, args.binary + '.provenance.json', configuration)


def provenance_configuration(args):
    """The compiler and flags a CMake provenance sidecar records, in test_provenance's shape."""
    configuration = dict(buildSystem='cmake', compiler=args.compiler, flags={
        'CCFLAGS': args.ccflags, 'CXXFLAGS': ' '.join(args.cxxflags.split()),
        'CPPDEFINES': ' '.join(args.define), 'LINKFLAGS': ' '.join(args.linkflag)})
    write_if_changed(args.output, json.dumps(configuration, sort_keys=True, indent=2) + '\n')


def _depfile_path(path):
    return Path(path).as_posix().replace(' ', '\\ ').replace('#', '\\#').replace('$', '$$')


def runtime_assets(args):
    """Export the runtime assets, then write a Ninja depfile.

    The export (runtime_assets.export_tree) clones a finished tree from the
    development store when one with the same content key exists. The depfile
    lists every input and the directories holding them, so Ninja re-runs the
    export when an input changes or a directory gains or loses a file."""
    import runtime_assets
    builddir = Path(args.builddir).resolve()
    platform = runtime_assets.host_platform()
    inputs = runtime_assets.export_inputs(ROOT, platform)
    # The release flag and the asset options pick the profile;
    # the CMake build always has h264 recording, so video packing is available.
    arguments = build_arguments(args)
    release = build_identity(arguments)['mode'] == 'release'
    lossy = runtime_assets.optimized_install_enabled(release, arguments.get('optimized_assets', 'auto'))
    packing = runtime_assets.sprite_packing_profile(arguments.get('sprite_packing', 'auto'), release, True)
    runtime_assets.export_tree(ROOT, builddir / 'runtime-assets', platform, inputs, lossy, packing,
                               packing == 'video', args.jobs)
    files = [Path(path).resolve() for path in inputs] + [ROOT / path for path in runtime_assets.EXPORT_TOOLS]
    directories = sorted({path.parent for path in files})
    names = [_depfile_path(path) for path in [*files, *directories]]
    write_if_changed(args.depfile, _depfile_path(builddir / 'runtime-assets.json') + ': '
                     + ' \\\n  '.join(names) + '\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)

    def command(name, function, builddir=True, stamp=False):
        sub = commands.add_parser(name)
        sub.set_defaults(function=function)
        if builddir:
            sub.add_argument('--builddir', required=True)
        if stamp:
            sub.add_argument('--stamp')
        return sub

    sub = command('build-config', build_config)
    sub.add_argument('--datadir', default='/usr/local/share')
    sub.add_argument('--font', default='sans.ttf')
    sub.add_argument('--opengl', action='store_true')
    sub.add_argument('--fribidi', action='store_true')
    sub.add_argument('--portaudio', action='store_true')
    sub.add_argument('--arg', action='append', default=[])
    sub = command('build-paths', build_paths)
    sub.add_argument('--runtime-assets', required=True)
    sub = command('compile-profile', compile_profile)
    sub.add_argument('--profile', required=True)
    command('build-stamp', build_stamp)
    command('skin-materials', skin_materials, stamp=True)
    command('shaders', shaders, stamp=True)
    command('shader-inputs', shader_inputs, builddir=False)
    command('hive-api', hive_api, stamp=True)
    command('skin-render', skin_render, stamp=True)
    command('skin-render-inputs', skin_render_inputs, builddir=False)
    sub = command('official-instance', official_instance, builddir=False)
    sub.add_argument('--arg', action='append', default=[])
    sub = command('recording', recording, builddir=False)
    sub.add_argument('--prefix', required=True)
    sub.add_argument('--target', choices=('linux', 'mingw32', 'darwin'), default='linux')
    sub.add_argument('--codec', choices=('h264', 'vp9', 'av1'), default='h264')
    sub = command('recording-prefix', recording_prefix)
    sub.add_argument('--target', choices=('linux', 'mingw32', 'darwin'), default='linux')
    sub.add_argument('--codec', choices=('h264', 'vp9', 'av1'), default='h264')
    # The default compiler names, so every build shares one store entry.
    sub.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    sub.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    sub.add_argument('--ar', default='ar')
    sub.add_argument('--ranlib', default='ranlib')
    sub.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    sub = command('lean-image-prefix', lean_image_prefix)
    sub.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    sub.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    sub.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    sub = command('mobile-toolchain', mobile_toolchain, builddir=False)
    sub.add_argument('--arg', action='append', default=[])
    sub = command('mobile-configure', mobile_configure)
    sub.add_argument('--arg', action='append', default=[])
    sub.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    sub = command('embedded-provenance', embedded_provenance)
    sub.add_argument('--configuration', required=True)
    sub = command('web-toolchain', web_toolchain)
    sub.add_argument('--arg', action='append', default=[])
    sub = command('web-configure', web_configure)
    sub.add_argument('--arg', action='append', default=[])
    sub.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    sub = command('web-page', web_page)
    sub.add_argument('--variant', action='append', default=[])
    sub = command('web-assets', web_assets_export)
    sub.add_argument('--arg', action='append', default=[])
    sub.add_argument('--depfile', required=True)
    sub.add_argument('--jobs', type=int, default=8)
    command('web-fonts', web_fonts)
    command('package-version', package_version, builddir=False)
    sub = command('mac-image-prefix', mac_image_prefix)
    sub.add_argument('--sdl-prefix', default='')
    sub.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    sub = command('bundle', bundle)
    sub.add_argument('--binary', required=True)
    sub.add_argument('--lean-image-prefix', required=True)
    sub.add_argument('--recording-prefix', default='')
    sub.add_argument('--jobs', type=int, default=8)
    sub.add_argument('--arg', action='append', default=[])
    sub = command('dmg', dmg, builddir=False)
    sub.add_argument('--app', required=True)
    sub.add_argument('--output', required=True)
    sub = command('install', install)
    sub.add_argument('--installdir', required=True)
    sub.add_argument('--bindir', required=True)
    sub.add_argument('--sdl-prefix', default='')
    sub.add_argument('--recording-prefix', default='')
    sub.add_argument('--lean-image-prefix', default='')
    command('ccache-directory', ccache_directory, builddir=False)
    sub = command('configure', configure)
    sub.add_argument('--arg', action='append', default=[])
    sub = command('test-provenance', test_provenance, builddir=False)
    sub.add_argument('--binary', required=True)
    sub.add_argument('--configuration', required=True)
    sub = command('provenance-configuration', provenance_configuration, builddir=False)
    sub.add_argument('--output', required=True)
    sub.add_argument('--compiler', required=True)
    sub.add_argument('--ccflags', default='')
    sub.add_argument('--cxxflags', default='')
    sub.add_argument('--define', action='append', default=[])
    sub.add_argument('--linkflag', action='append', default=[])
    sub = command('runtime-assets', runtime_assets)
    sub.add_argument('--depfile', required=True)
    sub.add_argument('--arg', action='append', default=[])
    sub.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args(argv)
    args.function(args)


if __name__ == '__main__':
    main()
