"""Emscripten toolchain, independent of native configuration and SDK discovery."""
from pathlib import Path
import json
import os
import subprocess
from SCons.Script import Environment, Default, Value, GetOption, Action, COMMAND_LINE_TARGETS
from build_layout import write_if_changed, prepare_directory, PACKAGE_VERSION
from javascript import javascript_objects, numeric_guard
from sources import CLIENT_SOURCES, GAG_SOURCES, USL_SOURCES, INCLUDE_DIRECTORIES

PORTS = ['--use-port=sdl2', '--use-port=browser/ports/glob2_webp.py',
         '--use-port=browser/ports/glob2_sdl2_image.py',
         '--use-port=sdl2_ttf', '--use-port=sdl2_net', '--use-port=vorbis',
         '--use-port=zlib']


def _build_variant(directory, identity, arguments, threaded=False):
    root = Path.cwd()
    output = Path(directory).resolve()
    (output / 'tmp').mkdir(parents=True, exist_ok=True)
    if threaded:
        prepare_directory(output, dict(identity, web_threads=True))
    from dev_store import browser_sdk, cache, isolated, key, command_path
    sdk = browser_sdk(root, arguments.get('emsdk', os.environ.get('EMSDK')))
    compiler = sdk / 'upstream/emscripten/em++'
    lock = json.loads((root / 'browser/toolchain.json').read_text())
    if not compiler.exists():
        raise ValueError('Emscripten SDK not found; run python3 browser/setup.py or pass emsdk=/path/to/emsdk')
    if not GetOption('clean'):
        version = subprocess.check_output([str(compiler), '--version'], text=True)
        if lock['emscripten'] not in version.splitlines()[0]:
            raise ValueError('This target requires Emscripten ' + lock['emscripten'])
    emscripten = compiler.parent
    build_environment = dict(os.environ)
    # Cache is target/config-specific, including port downloads and compiled system libraries.
    shared_cache = cache(root, 'emscripten-' + key(root, ['browser/toolchain.json', 'browser/ports/glob2_webp.py', 'browser/ports/glob2_sdl2_image.py'], [PORTS, threaded])) if not isolated() else output
    build_environment['EM_CACHE'] = str(shared_cache / 'cache')
    build_environment['EM_PORTS'] = str(shared_cache / 'ports')
    # emsdk's template derives paths from EM_CONFIG; anchor it to the selected
    # immutable SDK while keeping the generated build configuration local.
    if not os.environ.get('EM_CONFIG') and (sdk / '.emscripten').is_file():
        configuration = (sdk / '.emscripten').read_text()
        configuration = '\n'.join('emsdk_path = ' + repr(str(sdk))
            if line.startswith('emsdk_path =') else line for line in configuration.splitlines()) + '\n'
        write_if_changed(output / '.emscripten', configuration)
        build_environment['EM_CONFIG'] = str(output / '.emscripten')
    build_environment.update(TMPDIR=str(output / 'tmp'), TMP=str(output / 'tmp'), TEMP=str(output / 'tmp'))
    env = Environment(platform='posix', tools=['gcc', 'g++', 'ar', 'gnulink', 'compilation_db'],
                      ENV=build_environment, CC=command_path(emscripten / 'emcc'), CXX=command_path(compiler),
                      LINK=command_path(compiler), AR=command_path(emscripten / 'emar'), RANLIB=command_path(emscripten / 'emranlib'))
    env['PROGSUFFIX'] = '.js'
    if threaded:
        env.Append(CCFLAGS=['-pthread'], LINKFLAGS=['-pthread', '-sOFFSCREENCANVAS_SUPPORT=1',
            '-sDEFAULT_PTHREAD_STACK_SIZE=8388608',
            '-sPTHREAD_POOL_SIZE_STRICT=0',
            "'-sPTHREAD_POOL_SIZE=Math.min(navigator.hardwareConcurrency||1,4)'",
            '-sALLOW_BLOCKING_ON_MAIN_THREAD=0',
            '--js-library', 'browser/threaded-egl.js'])
    if threaded:
        env.Append(LINKFLAGS=['-Wl,--wrap=' + name for name in
            ('SDL_OpenAudio', 'SDL_CloseAudio', 'SDL_PauseAudio', 'SDL_LockAudio', 'SDL_UnlockAudio')])
    config = output / 'include/glob2/BuildConfig.h'
    write_if_changed(config, f'''#pragma once
#define HAVE_OPENGL 1
#define GLOB2_WEBGL2 1
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "{PACKAGE_VERSION}"
#define PACKAGE_DATA_DIR "/"
#define PACKAGE_SOURCE_DIR "/"
#define PRIMARY_FONT "sans.ttf"
''')
    include_paths = [str(output / 'include')] + list(INCLUDE_DIRECTORIES)
    env.Append(CPPPATH=include_paths + ["#third_party/quickjs-ng"], CPPDEFINES=['HAVE_CONFIG_H'],
               CXXFLAGS=['-std=gnu++20', '-fexceptions', '-g2', '-O2' if identity['mode']=='release' else '-O0'] + PORTS)
    env.Append(LINKFLAGS=['-fexceptions', '-O2' if identity['mode']=='release' else '-O0',
        '-sLEGACY_GL_EMULATION=1', '-sFETCH=1', '-sMIN_WEBGL_VERSION=2', '-sMAX_WEBGL_VERSION=2',
        '-sALLOW_MEMORY_GROWTH',
        '-sINITIAL_MEMORY=134217728', '-sSTACK_SIZE=8388608', '-sASSERTIONS=1',
        '-sFORCE_FILESYSTEM', '-lidbfs.js', '-lwebsocket.js',
        "'-sEXPORTED_RUNTIME_METHODS=[\"callMain\",\"FS\"]'",
        '--pre-js', 'browser/storage.js', '--pre-js', 'browser/file-selection.js', '--pre-js', 'browser/audio.js', '--pre-js', 'browser/runtime.js'] + PORTS)
    # Export during the build, not while evaluating SCons or during dry runs.
    sys_path = __import__('sys').path
    if str(root) not in sys_path: sys_path.insert(0, str(root))
    from tools.package_assets import source_files, export_assets
    asset_root = output / 'runtime-assets'
    asset_stamp = asset_root.with_suffix('.json')
    def prepare_assets(target, source, env):
        export_assets(root, asset_root, platform='web', optimized=identity['mode']=='release')
        return 0
    asset_inputs = list(source_files(root, 'web'))
    assets = env.Command(str(asset_stamp), [str(p) for p in asset_inputs] +
        ['tools/package_assets.py', 'tools/asset-requirements.txt', Value([identity['mode'], [str(p) for p in asset_inputs]])],
        Action(prepare_assets, 'Exporting verified browser assets'))
    env.Precious(assets)  # Keep the ownership audit while an export is rebuilt.
    if not (asset_root / 'data').is_dir():
        env.AlwaysBuild(assets)
    for asset_directory in ('data', 'maps', 'campaigns', 'scripts'):
        env.Append(LINKFLAGS=['--preload-file', str(asset_root / asset_directory) + '@/' + asset_directory])
    env['LINKCOM'] = '${TEMPFILE("$LINK -o $TARGET $LINKFLAGS $__RPATH $SOURCES $_LIBDIRFLAGS $_LIBFLAGS", "$LINKCOMSTR")}'
    def prepare_ports(target, source, env):
        # The pinned SDL_net port recursively requests non-threaded SDL while
        # holding the cache lock. Warm that dependency before building mt ports.
        if threaded:
            result = subprocess.run([str(compiler), *PORTS, '-x', 'c++', '-c',
                '-o', str(output / 'ports-bootstrap.o'), '-'], input='', text=True, env=env['ENV'])
            if result.returncode:
                return result.returncode
        return subprocess.run(
            [str(compiler), *(['-pthread'] if threaded else []), *PORTS, '-x', 'c++', '-c', '-o', str(target[0]), '-'],
            input='', text=True, env=env['ENV']).returncode
    ports = env.Command(str(output / 'ports-ready.o'), [Value(lock), Value(PORTS), Value(threaded), 'browser/ports/glob2_webp.py', 'browser/ports/glob2_sdl2_image.py'],
                        Action(prepare_ports, 'Preparing pinned Emscripten ports'))
    files = ['src/' + s for s in CLIENT_SOURCES if s not in ('VoiceRecorder.cpp', 'net/NetTransport.cpp', 'net/WssTransport.cpp', 'net/LanIdentity.cpp', 'net/ServerControl.cpp', 'net/irc/IRCTextMessageHandler.cpp', 'online/HttpFetch.cpp')]
    files += ['libgag/src/' + s for s in GAG_SOURCES if s != 'ApplicationHost.cpp']
    files += ['libusl/src/' + s for s in USL_SOURCES]
    files += ['browser/VoiceRecorder.cpp', 'browser/ApplicationHost.cpp', 'browser/NetTransport.cpp', 'browser/IRCTextMessageHandler.cpp', 'browser/Launcher.cpp', 'browser/HttpFetch.cpp']
    if threaded:
        files += ['browser/Audio.cpp']
    if any(target in COMMAND_LINE_TARGETS for target in ('android-tests', 'ios-tests', 'web-tests')):
        from test_provenance import register_test_provenance
        provenance_header = register_test_provenance(env, output)
    strict = env.Clone()
    strict.Append(CXXFLAGS=['-fno-fast-math', '-ffp-contract=off'])
    objects = []
    for f in files:
        local = strict if f.startswith('src/script/') or f == 'src/ai/AIJavaScript.cpp' else env
        if f == 'src/Glob2.cpp':
            local = local.Clone()
            local.Append(CPPDEFINES=[('main', 'glob2ApplicationMain')])
        objects.append(local.Object(str(output / 'obj' / (f + '.o')), f))
    numeric_guard(strict, [obj for name, obj in zip(files, objects) if name.startswith('src/script/') or name == 'src/ai/AIJavaScript.cpp'])
    objects += javascript_objects(env, output / "obj/third_party", identity["mode"] == "release")
    env.Requires(objects, ports)
    env.Depends(objects, str(config))
    program = env.Program(str(output / 'index.js'), objects)
    env.Depends(program, assets)
    if 'web-tests' in COMMAND_LINE_TARGETS:
        import sys
        sys.path.insert(0, str(root / 'test'))
        import tests as registry
        tests = env.Clone()
        tests.Append(CPPPATH=['test', 'test/support', 'src/render', 'libgag/src'])
        tests.Append(LINKFLAGS=['--preload-file', 'test/fixtures@/test/fixtures',
                               '--preload-file', 'games@/games', '-sEXIT_RUNTIME=0'])
        test_objects = []
        for entry in registry.SUPPORT + registry.ENGINE_SUPPORT + registry.scripting_entries() + ['ComputeExecutorHarness.cpp', 'GradientPipelineHarness.cpp',
                'SharedWorkerLifecycleTest.cpp', 'BuildingGradientInvalidationHarness.cpp', 'PathGradientHarness.cpp']:
            source, options = (entry, {}) if isinstance(entry, str) else entry
            local = tests.Clone()
            local.Append(CXXFLAGS=options.get('cxxflags', []))
            local.Append(CPPDEFINES=options.get('defines', []))
            path = 'test/' + source
            if source.endswith('TestMain.cpp'):
                local.Append(CPPDEFINES=[('main', 'glob2ApplicationMain')])
            targets = local.Object(str(output / 'obj/tests' / (path + '.o')), path)
            if source.endswith('TestMain.cpp'):
                local.Depends(targets, provenance_header)
            test_objects += targets
        env.Requires(test_objects, ports)
        production = [obj for name, obj in zip(files, objects) if name != 'src/Glob2.cpp']
        production += objects[len(files):]
        harness = tests.Program(str(output / 'script-tests.js'), production + test_objects)
        tests.Depends(harness, ['browser/storage.js', 'browser/file-selection.js',
                               'browser/audio.js', 'browser/runtime.js'])
        env.Depends(harness, assets)
        tests.Depends(harness, [str(p) for directory in ('data', 'maps', 'campaigns', 'scripts', 'test/fixtures', 'games')
                               for p in Path(directory).rglob('*') if p.is_file()])
        tests.SideEffect([str(output / ('script-tests.' + extension)) for extension in ('wasm', 'data')], harness)
        if threaded:
            tests.Depends(harness, 'browser/threaded-egl.js')
        env.Alias('web-tests', harness)

    env.Depends(program, ['browser/storage.js', 'browser/file-selection.js', 'browser/audio.js', 'browser/runtime.js', 'browser/toolchain.json'])
    env.Depends(program, [str(p) for directory in ('data','maps','campaigns','scripts')
                         for p in Path(directory).rglob('*') if p.is_file()])
    if threaded:
        env.Depends(program, 'browser/threaded-egl.js')
    env.SideEffect([str(output / ('index.'+ext)) for ext in ('wasm','data')], program)
    env.Clean(program, [str(output / ('index.'+ext)) for ext in ('wasm','data')])
    database = env.CompilationDatabase(str(output / 'compile_commands.json'))
    env.Alias('compile_commands.json', database)
    write_if_changed(output / 'options.json', json.dumps(dict(arguments), sort_keys=True, indent=2)+'\n')

    return env, program, database


def build_web(directory, identity, arguments):
    env, serial, database = _build_variant(directory, identity, arguments)
    _, threaded, _ = _build_variant(Path(directory) / 'threaded', identity, arguments, True)
    def shell(target, source, env):
        write_if_changed(str(target[0]), Path('browser/shell.html').read_text().replace(
            '{{{ SCRIPT }}}', '<script src="loader.js"></script>'))
        return 0
    page = env.Command(str(Path(directory) / 'index.html'),
        ['browser/shell.html', 'browser/loader.js', serial, threaded], Action(shell, 'Packaging browser runtimes'))
    loader = env.Install(directory, 'browser/loader.js')
    env.Depends(page, loader)
    env.Alias('web-package', [page, loader])
    env.Alias('web-threaded', threaded)
    env.Alias('web-serial', serial)
    Default(page, loader, database)
