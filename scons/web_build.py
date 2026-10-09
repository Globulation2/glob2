"""Emscripten toolchain, independent of native configuration and SDK discovery."""
from pathlib import Path
import json
import os
import re
import subprocess
from SCons.Script import Environment, Default, Value, GetOption, Action, COMMAND_LINE_TARGETS
from build_layout import write_if_changed, prepare_directory, PACKAGE_VERSION
from javascript import javascript_objects, numeric_guard, strict_numeric_source, guarded_numeric_source
import official_instance
import ccache
from build_timing import record
from dev_build import configure as configure_development, can_build_dependencies, dependency_jobs
from shared_dependencies import ensure as ensure_dependency
from dev_compile import objects as development_objects, unique
from sources import CLIENT_SOURCES, GAG_SOURCES, USL_SOURCES, INCLUDE_DIRECTORIES
import web_assets
import skin_materials

PORTS = ['--use-port=zlib']


def _build_variant(directory, identity, arguments, threaded=False, packaged=None, asset_directory=None):
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
    from sdl3_dependencies import build as build_sdl3
    sdl_prefix = output / 'sdl3/prefix'
    build_environment = dict(os.environ)
    # Cache is target/config-specific, including port downloads and compiled system libraries.
    shared_cache = cache(root, 'emscripten-' + key(root, ['browser/toolchain.json', 'scons/sdl3-versions.json', 'scons/sdl3-vendored.json', 'scons/opus-versions.json', 'scons/opus_dependencies.py'], [PORTS, threaded])) if not isolated() else output
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
    (output / 'tmp').mkdir(parents=True, exist_ok=True)
    sdl_prefix = ensure_dependency(build_sdl3, sdl_prefix, output / 'sdl3/sources', execute=can_build_dependencies(),
        jobs=dependency_jobs(arguments), emscripten=emscripten, environment=build_environment, threaded=threaded)
    from opus_dependencies import build as build_opus, LIBRARIES as OPUS_LIBRARIES
    opus_prefix = output / 'opus/prefix'
    opus_prefix = ensure_dependency(build_opus, opus_prefix, output / 'opus/sources', execute=can_build_dependencies(),
        jobs=dependency_jobs(arguments), emscripten=emscripten, environment=build_environment, threaded=threaded)
    env = Environment(platform='posix', tools=['gcc', 'g++', 'ar', 'gnulink', 'compilation_db'],
                      ENV=build_environment, CC=command_path(emscripten / 'emcc'), CXX=command_path(compiler),
                      LINK=command_path(compiler), AR=command_path(emscripten / 'emar'), RANLIB=command_path(emscripten / 'emranlib'))
    env['OPUS_PREFIX'] = str(opus_prefix)
    env['PROGSUFFIX'] = '.js'
    env.Append(LINKFLAGS=['--js-library', 'browser/canvas-size.js'])
    if threaded:
        env.Append(CCFLAGS=['-pthread'], LINKFLAGS=['-pthread', '-sOFFSCREENCANVAS_SUPPORT=1',
            '-sDEFAULT_PTHREAD_STACK_SIZE=8388608',
            '-sPTHREAD_POOL_SIZE_STRICT=0',
            "'-sPTHREAD_POOL_SIZE=Math.max(navigator.hardwareConcurrency||1,1)'",
            '-sALLOW_BLOCKING_ON_MAIN_THREAD=0',
            '--js-library', 'browser/threaded-egl.js'])
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
    skin_materials.generate(Path(__file__).resolve().parents[1], output)
    include_paths = [str(output / 'include'), str(sdl_prefix / 'include')] + list(INCLUDE_DIRECTORIES)
    env.Append(CPPPATH=include_paths + [str(opus_prefix / 'include'), str(opus_prefix / 'include/opus'), "#third_party/quickjs-ng"], CPPDEFINES=['HAVE_CONFIG_H'] + official_instance.cppdefines(official_instance.origin(arguments)),
               CXXFLAGS=['-std=gnu++20', '-fwasm-exceptions', '-g2', '-O2' if identity['mode']=='release' else '-O0'] + PORTS)
    env.Append(LINKFLAGS=['-fwasm-exceptions', '-O2' if identity['mode']=='release' else '-O0',
        '-sLEGACY_GL_EMULATION=1', '-sFETCH=1', '-sMIN_WEBGL_VERSION=2', '-sMAX_WEBGL_VERSION=2',
        '-sALLOW_MEMORY_GROWTH',
        '-sINITIAL_MEMORY=134217728', '-sSTACK_SIZE=8388608', '-sASSERTIONS=1',
        '-sFORCE_FILESYSTEM', '-lidbfs.js', '-lwebsocket.js',
        "'-sEXPORTED_FUNCTIONS=[\"_main\",\"_malloc\",\"_free\"]'",
        "'-sEXPORTED_RUNTIME_METHODS=[\"callMain\",\"FS\"]'",
        '--pre-js', 'browser/storage.js', '--pre-js', 'browser/file-selection.js', '--pre-js', 'browser/audio.js', '--pre-js', 'browser/recording.js', '--pre-js', 'browser/runtime.js',
        '--post-js', 'browser/webgl-shaders.js'] + PORTS)
    if arguments.get('web_profile') == '1':
        # Preserve function names for browser CPU profiles without changing optimization.
        env.Append(LINKFLAGS=['--profiling-funcs'])
    # Export during the build, not while evaluating SCons or during dry runs.
    sys_path = __import__('sys').path
    if str(root) not in sys_path: sys_path.insert(0, str(root))
    from tools.package_assets import source_files, export_assets
    # Game data ships as content-addressed packages next to the page (see
    # scons/web_assets.py), not as one --preload-file blob: the page shows
    # progress, caches them across visits and fetches optional data later. The
    # serial variant exports and packages the data once; the threaded runtime
    # embeds the same manifest and loads the same packages.
    if packaged is None:
        asset_output = Path(asset_directory).resolve() if asset_directory else output
        asset_root = asset_output / 'runtime-assets'
        asset_stamp = asset_root.with_suffix('.json')
        def prepare_assets(target, source, env):
            export_assets(root, asset_root, platform='web', optimized=True, lossy=identity['mode']=='release')
            return 0
        asset_inputs = list(source_files(root, 'web'))
        exported = env.Command(str(asset_stamp), [str(p) for p in asset_inputs] +
            ['tools/package_assets.py', 'tools/terrain_tileset.py', 'tools/asset-requirements.txt', 'tools/image_encoding.json', Value([identity['mode'], [str(p) for p in asset_inputs]])],
            Action(prepare_assets, 'Exporting verified browser assets'))
        env.Precious(exported)  # Keep the ownership audit while an export is rebuilt.
        if not (asset_root / 'data').is_dir():
            env.AlwaysBuild(exported)
        asset_manifest = asset_output / 'asset-manifest.js'
        # The plan also reads the browser copies (browser/derive_assets.py) and the game
        # sprite names in GlobalContainer::loadGameGraphics, terrain and building tables.
        plan_inputs = ['scons/web_assets.py', 'deploy/sim_version.py', 'browser/derive_assets.py',
                       'src/app/GlobalContainer.cpp', 'src/map/TerrainPresentation.h', 'data/terrain/tileset.json', 'data/resources/registry.json']
        plan_inputs += [str(p) for p in Path('browser/assets').glob('*') if p.is_file()]
        plan_inputs += [str(p) for p in Path('src/building/types').glob('BuildingTypes*.cpp')]
        plan_inputs += [str(p) for p in Path('data/buildings').rglob('*.json')]
        plan_inputs += [str(p) for p in Path('data/resources').rglob('*.json')]
        assets = env.Command(str(asset_manifest), [exported] + plan_inputs,
            Action(lambda target, source, env: web_assets.build(root, asset_output, target[0].abspath, asset_root) and 0,
                   'Packaging browser game data'))
    else:
        assets, asset_manifest, asset_root = packaged
    env.Append(LINKFLAGS=['--pre-js', str(asset_manifest), '--pre-js', 'browser/asset-loader.js'])
    env.Append(LIBPATH=[str(sdl_prefix / 'lib')], LIBS=['SDL3_ttf', 'SDL3_image', 'SDL3_net', 'SDL3', 'freetype', 'webpdemux', 'webpmux', 'webp', 'sharpyuv'])
    env.Append(LIBS=[env.File(str(opus_prefix / 'lib' / ('lib' + name + '.a'))) for name in OPUS_LIBRARIES])
    env['LINKCOM'] = '${TEMPFILE("$LINK -o $TARGET $LINKFLAGS $__RPATH $SOURCES $_LIBDIRFLAGS $_LIBFLAGS", "$LINKCOMSTR")}'
    def prepare_ports(target, source, env):
        # Warm serial system ports before requesting their threaded variants.
        if threaded:
            result = subprocess.run([str(compiler), *PORTS, '-x', 'c++', '-c',
                '-o', str(output / 'ports-bootstrap.o'), '-'], input='', text=True, env=env['ENV'])
            if result.returncode:
                return result.returncode
        return subprocess.run(
            [str(compiler), *(['-pthread'] if threaded else []), *PORTS, '-x', 'c++', '-c', '-o', str(target[0]), '-'],
            input='', text=True, env=env['ENV']).returncode
    ports = env.Command(str(output / 'ports-ready.o'), [Value(lock), Value(PORTS), Value(threaded)],
                        Action(prepare_ports, 'Preparing pinned Emscripten ports'))
    files = ['src/' + s for s in CLIENT_SOURCES if s not in ('audio/VoiceRecorder.cpp', 'net/NetTransport.cpp', 'net/TcpTransport.cpp', 'net/WssTransport.cpp', 'net/LanIdentity.cpp', 'online/HttpFetch.cpp')]
    files += ['libgag/src/' + s for s in GAG_SOURCES if s not in ('ApplicationHost.cpp', 'RecordingEncoder.cpp', 'RecordingSession.cpp')]
    files += ['libusl/src/' + s for s in USL_SOURCES]
    files += ['browser/HiveBrowserHost.cpp', 'browser/VoiceRecorder.cpp', 'browser/ApplicationHost.cpp', 'browser/RecordingPlatform.cpp', 'browser/NetTransport.cpp', 'browser/Launcher.cpp', 'browser/HttpFetch.cpp']
    files += ['browser/Audio.cpp']
    if any(target in COMMAND_LINE_TARGETS for target in ('android-tests', 'ios-tests', 'web-tests')):
        from test_provenance import register_test_provenance
        provenance_header = register_test_provenance(env, output)
    env['BUILDDIR'] = str(output)
    configure_development(env, identity)
    if ccache.enabled():
        ccache.enable(env)
    env.Alias('dev-dependencies', [])
    strict = env.Clone()
    strict.Append(CXXFLAGS=['-fno-fast-math', '-ffp-contract=off'])
    by_source = {}
    for compile_env, subset in ((strict, [f for f in files if strict_numeric_source(f)]), (env, [f for f in files if not strict_numeric_source(f) and f != 'src/app/Glob2.cpp'])):
        by_source.update(development_objects(compile_env, subset, lambda name: str(output / 'obj' / (name + '.o'))))
    entry = env.Clone()
    entry.Append(CPPDEFINES=['SDL_MAIN_HANDLED', ('main', 'glob2ApplicationMain')])
    by_source.update(development_objects(entry, ['src/app/Glob2.cpp'], lambda name: str(output / 'obj' / (name + '.o'))))
    objects = unique([by_source[f] for f in files])
    numeric_guard(strict, [by_source[name] for name in files if guarded_numeric_source(name)])
    script_objects = javascript_objects(env, output / "obj/third_party", identity["mode"] == "release")
    objects += script_objects
    env.Requires(objects, ports)
    env.Depends(objects, str(config))
    program = record(env, env.Program(str(output / 'index.js'), objects), "link")
    env.Depends(program, assets)
    if 'web-tests' in COMMAND_LINE_TARGETS:
        import sys
        sys.path.insert(0, str(root / 'test'))
        import tests as registry
        tests = env.Clone()
        # The harness page has no asset packages beside it: preload the exported data.
        flags, kept = list(tests['LINKFLAGS']), []
        for index, flag in enumerate(flags):
            if flag in (str(asset_manifest), 'browser/asset-loader.js') and kept and kept[-1] == '--pre-js':
                kept.pop()
            else:
                kept.append(flag)
        tests['LINKFLAGS'] = kept
        for asset_directory in ('data', 'maps', 'campaigns', 'scripts'):
            tests.Append(LINKFLAGS=['--preload-file', str(asset_root / asset_directory) + '@/' + asset_directory])
        tests.Append(CPPPATH=['test', 'test/support', 'libgag/src'])
        tests.Append(LINKFLAGS=['--preload-file', 'test/fixtures@/test/fixtures',
                               '--preload-file', 'examples/javascript@/examples/javascript',
                               '--preload-file', 'games@/games', '-sEXIT_RUNTIME=0'])
        test_objects = []
        for entry in registry.SUPPORT + registry.ENGINE_SUPPORT + registry.scripting_entries() + ['#src/common/EntityRandomTest.cpp', '#src/unit/EntityRandomLifecycleTest.cpp', ('#src/scripting/sgsl/LegacyScriptCoverageTest.cpp', dict(cxxflags=['-fno-access-control'])), '#src/common/ComputeExecutorHarness.cpp', '#src/map/gradient/GradientPipelineHarness.cpp',
                '#src/game/SharedWorkerLifecycleTest.cpp', '#src/map/gradient/BuildingGradientInvalidationHarness.cpp',
                ('#src/engine/sim/snapshot/WorldSnapshotTest.cpp', dict(cxxflags=['-fno-access-control'])),
                '#src/render/scene/SceneExtractTest.cpp', '#src/render/scene/SceneBufferTest.cpp',
                '#src/render/overlay/OverlayFillTest.cpp',
                ('#src/map/gradient/PathGradientHarness.cpp', dict(cxxflags=['-fno-access-control'])),
                ('#src/map/gradient/GradientPreparationTest.cpp', dict(cxxflags=['-fno-access-control'])),
                ('#src/ai/cortex/CortexActionCoverageTest.cpp', dict(cxxflags=['-fno-access-control'])),
                ('#src/map/MapQueryTest.cpp', dict(cxxflags=['-fno-access-control'])),
                ('#src/hud/GUIInteractionCoverageTest.cpp', dict(cxxflags=['-fno-access-control'])),
                '#src/resource/ResourceRegistryTest.cpp', '#src/map/TerrainResourcesHarness.cpp',
                '#src/building/types/BuildingCatalogTest.cpp', '#src/building/BuildingCatalogFixtureHarness.cpp',
                '#src/building/BuildingServicesTest.cpp', '#src/building/BuildingProductionCombatTest.cpp',
                '#src/ai/shared_runtime/RuntimeContinuationTest.cpp',
                ('#src/ai/AICustomCatalogTest.cpp', dict(cxxflags=['-fno-access-control'])),
                ('#src/unit/MarketsV2Test.cpp', dict(cxxflags=['-fno-access-control']))]:
            source, options = (entry, {}) if isinstance(entry, str) else entry
            local = tests.Clone()
            local.Append(CXXFLAGS=options.get('cxxflags', []))
            local.Append(CPPDEFINES=options.get('defines', []))
            path = registry.source_path(source)
            if source.endswith('TestMain.cpp'):
                local.Append(CPPDEFINES=['SDL_MAIN_HANDLED', ('main', 'glob2ApplicationMain')])
            targets = local.Object(str(output / 'obj/tests' / (path + '.o')), path)
            if source.endswith('TestMain.cpp'):
                local.Depends(targets, provenance_header)
            test_objects += targets
        env.Requires(test_objects, ports)
        production = unique([by_source[name] for name in files if name != 'src/app/Glob2.cpp'])
        production += script_objects
        harness = tests.Program(str(output / 'script-tests.js'), production + test_objects)
        tests.Depends(harness, ['browser/storage.js', 'browser/file-selection.js',
                               'browser/audio.js', 'browser/runtime.js', 'browser/webgl-shaders.js', 'browser/canvas-size.js'])
        env.Depends(harness, assets)
        tests.Depends(harness, [str(p) for directory in ('data', 'maps', 'campaigns', 'scripts', 'test/fixtures', 'examples/javascript', 'games')
                               for p in Path(directory).rglob('*') if p.is_file()])
        tests.SideEffect([str(output / ('script-tests.' + extension)) for extension in ('wasm', 'data')], harness)
        if threaded:
            tests.Depends(harness, 'browser/threaded-egl.js')
        env.Alias('web-tests', harness)

    env.Depends(program, ['browser/storage.js', 'browser/file-selection.js', 'browser/audio.js', 'browser/recording.js', 'browser/runtime.js', 'browser/webgl-shaders.js',
                          'browser/asset-loader.js', 'browser/canvas-size.js', 'browser/toolchain.json', assets])
    if threaded:
        env.Depends(program, 'browser/threaded-egl.js')
    env.SideEffect([str(output / 'index.wasm')], program)
    env.Clean(program, [str(output / ('index.'+ext)) for ext in ('wasm','data')] + ([] if threaded else [str(output / 'assets')]))
    database = env.CompilationDatabase(str(output / 'compile_commands.json'))
    env.Alias('compile_commands.json', database)
    write_if_changed(output / 'options.json', json.dumps(dict(arguments), sort_keys=True, indent=2)+'\n')

    return env, program, database, (assets, asset_manifest, asset_root)


def build_web(directory, identity, arguments):
    variant = identity.get('web_variant', 'both')
    if variant != 'both' and 'web-package' in COMMAND_LINE_TARGETS:
        raise ValueError('web-package requires web_variant=both')
    serial = threaded = None
    if variant != 'threaded':
        env, serial, database, packaged = _build_variant(directory, identity, arguments)
    if variant != 'serial':
        threaded_env, threaded, threaded_database, threaded_packaged = _build_variant(Path(directory) / 'threaded', identity, arguments, True, packaged if serial else None, asset_directory=directory)
        if not serial:
            env, database, packaged = threaded_env, threaded_database, threaded_packaged
    def shell(target, source, env):
        page = Path('browser/shell.html').read_text().replace('/* AI_STUDIO_BRIDGE */', Path('browser/studio-envelope.js').read_text() + '\n' + Path('browser/generator-studio.js').read_text() + '\n' + Path('browser/studio.js').read_text() + '\n' + Path('browser/set-preview.js').read_text()).replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>')
        # The page shows WebAssembly download progress against these sizes.
        sizes = {}
        if serial: sizes['index.wasm'] = Path(directory) / 'index.wasm'
        if threaded: sizes['threaded/index.wasm'] = Path(directory) / 'threaded/index.wasm'
        page = page.replace('<script src="loader.js"></script>', '<script>globalThis.Glob2BuildVariants=' + json.dumps(['serial', 'threaded'] if variant == 'both' else [variant]) + ';</script><script src="loader.js"></script>')
        sizes = json.dumps({name: path.stat().st_size for name, path in sizes.items()}, separators=(',', ':'), sort_keys=True)
        page, count = re.subn(r'data-wasm-bytes="0"', f"data-wasm-bytes='{sizes}'", page, count=1)
        if count != 1:
            raise ValueError('browser/shell.html lacks the data-wasm-bytes placeholder')
        write_if_changed(str(target[0]), page)
        write_if_changed(str(target[1]), page)
        write_if_changed(str(target[2]), page)
        write_if_changed(str(target[3]), page)
        return 0
    page = env.Command([str(Path(directory) / 'index.html'), str(Path(directory) / 'studio.html'), str(Path(directory) / 'set-preview.html'), str(Path(directory) / 'generator-studio.html')],
        ['browser/shell.html', 'browser/studio-envelope.js', 'browser/generator-studio.js', 'browser/studio.js', 'browser/set-preview.js', 'browser/loader.js'] + [runtime for runtime in (serial, threaded) if runtime], Action(shell, 'Packaging browser runtimes'))
    # The music decoder has independent Wasm memory in both browser variants.
    music = env.Clone()
    music['CCFLAGS'] = [flag for flag in music.Split(music.get('CCFLAGS', [])) if flag != '-pthread']
    from opus_dependencies import build as build_opus
    music_opus = ensure_dependency(build_opus, Path(directory) / 'music-opus/prefix', Path(directory) / 'music-opus/sources',
        execute=can_build_dependencies(), jobs=dependency_jobs(arguments), emscripten=Path(__import__('shlex').split(env.subst('$CC'))[0]).parent, environment=env['ENV'], threaded=False)
    music['LIBS'] = [music.File(str(music_opus / 'lib' / ('lib' + name + '.a')))
                     for name in ('opusfile', 'opus', 'ogg')]
    music['LINKFLAGS'] = ['-O2', '-fwasm-exceptions', '--no-entry',
        '-sMODULARIZE=1', '-sEXPORT_NAME=createMusicRuntime', '-sENVIRONMENT=worker',
        '-sALLOW_MEMORY_GROWTH=1', '-sMAXIMUM_MEMORY=268435456',
        '-sEXPORTED_FUNCTIONS=["_malloc","_free"]',
        '-sEXPORTED_RUNTIME_METHODS=["UTF8ToString","HEAPU8","HEAP16"]']
    configure_development(music, identity)
    music_objects = [music.Object(str(Path(directory) / 'music-obj' / (source + '.o')), source)
        for source in ('browser/MusicWorker.cpp', 'src/audio/MusicProducer.cpp', 'src/audio/MusicStream.cpp')]
    record(music, music_objects, "compile")
    music_program = record(music, music.Program(str(Path(directory) / 'music-runtime.js'), music_objects), "link")
    music.SideEffect(str(Path(directory) / 'music-runtime.wasm'), music_program)
    music_scripts = env.Install(directory, ['browser/music-worker.js', 'browser/music-output.js'])
    env.Depends(page, [music_program, music_scripts])
    # One recording module serves both game runtimes, and is fetched only on use.
    from recording_dependencies import build as build_recording, attach as attach_recording
    recording_prefix = Path(directory) / 'recording/prefix'
    recording_prefix = ensure_dependency(build_recording, os.environ.get('GLOB2_RECORDING_PREFIX', recording_prefix), Path(directory) / 'recording/sources',
            explicit=bool(os.environ.get('GLOB2_RECORDING_PREFIX')), execute=can_build_dependencies(), jobs=dependency_jobs(arguments),
            cc=env['CC'], cxx=env['CXX'], ar=env['AR'], ranlib=env['RANLIB'],
            target='wasm', arch='wasm32', sdk_identity=json.loads(Path('browser/toolchain.json').read_text()), cflags=['-msimd128', '-fwasm-exceptions'], environment=env['ENV'])
    recording = env.Clone()
    recording['LIBS'] = []
    recording['CCFLAGS'] = ['-O3', '-msimd128']
    recording['CXXFLAGS'] = ['-std=gnu++20', '-fwasm-exceptions']
    recording['LINKFLAGS'] = ['-O3', '-msimd128', '-fwasm-exceptions', '--no-entry',
        '-sMODULARIZE=1', '-sEXPORT_NAME=createRecordingRuntime', '-sENVIRONMENT=worker',
        '-sFILESYSTEM=0', '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=33554432',
        '-sMAXIMUM_MEMORY=1073741824', '-sSTACK_SIZE=8388608',
        '-sEXPORTED_FUNCTIONS=["_malloc","_free"]', '-sEXPORTED_RUNTIME_METHODS=["ccall","HEAPU8"]']
    attach_recording(recording, recording_prefix, 'wasm')
    configure_development(recording, identity)
    recording_objects = [recording.Object(str(Path(directory) / 'recording-obj' / (source + '.o')), source)
        for source in ('browser/RecordingWorker.cpp', 'libgag/src/RecordingEncoder.cpp', 'libgag/src/RecordingSession.cpp', 'libgag/src/RecordingMetadata.cpp')]
    record(recording, recording_objects, "compile")
    recording_program = record(recording, recording.Program(str(Path(directory) / 'recording-runtime.js'), recording_objects), 'link')
    recording.SideEffect(str(Path(directory) / 'recording-runtime.wasm'), recording_program)
    recording_worker = env.Install(directory, ['browser/recording-worker.js','browser/recording-storage.js','browser/recording-video.js'])
    recording_notices = [env.Install(str(Path(directory)/'licenses/recording'),str(p))
        for p in (recording_prefix/'share/licenses/recording').glob('*')]
    opus_notices = [env.Install(str(Path(directory)/'licenses/opus'), str(p))
        for p in (Path(env['OPUS_PREFIX'])/'share/licenses/opus').glob('*')]
    env.Depends(page, [recording_program, recording_worker, recording_notices, opus_notices])
    # Assistant programs never execute in the live game's WebAssembly memory.
    hive = env.Clone()
    hive['CCFLAGS'] = [flag for flag in hive.Split(hive.get('CCFLAGS', [])) if flag != '-pthread']
    hive['LIBS'] = []
    hive['LINKFLAGS'] = ['-fwasm-exceptions', '--no-entry', '-sMODULARIZE=1', '-sEXPORT_NAME=createHiveRuntime',
        '-sENVIRONMENT=worker', '-sFILESYSTEM=0', '-sALLOW_MEMORY_GROWTH=1',
        '-sMAXIMUM_MEMORY=268435456', '-sSTACK_SIZE=8388608',
        '-sEXPORTED_FUNCTIONS=["_glob2_hive_invoke","_malloc","_free"]', '-sEXPORTED_RUNTIME_METHODS=["ccall","stringToUTF8","lengthBytesUTF8"]']
    hive.Append(CXXFLAGS=['-fno-fast-math', '-ffp-contract=off'])
    configure_development(hive, identity)
    hive_objects = [hive.Object(str(Path(directory) / 'hive-obj' / (source + '.o')), source)
        for source in ('browser/HiveWorker.cpp', 'src/hive/HiveWorker.cpp', 'src/scripting/javascript/ScriptRuntime.cpp', 'src/scripting/javascript/ScriptValue.cpp')]
    hive_objects += javascript_objects(hive, Path(directory) / 'hive-obj/third_party', True)
    record(hive, hive_objects, "compile")
    hive_program = record(hive, hive.Program(str(Path(directory) / 'hive-runtime.js'), hive_objects), 'link')
    hive.SideEffect(str(Path(directory) / 'hive-runtime.wasm'), hive_program)
    hive_worker = env.Install(directory, 'browser/hive-worker.js')
    env.Depends(page, [hive_program, hive_worker])
    loader = env.Install(directory, 'browser/loader.js')
    localization = env.Install(directory, 'browser/i18n.js')
    localization += env.Install(str(Path(directory) / 'locales'), [str(path) for path in sorted(Path('platform/packages/i18n/locales').glob('*.json'))])
    env.Depends(page, [loader, localization])
    env.Alias('web-package', [page, loader])
    if threaded: env.Alias('web-threaded', threaded)
    if serial: env.Alias('web-serial', serial)
    Default(page, loader, database)
