"""Mobile C++ artifacts; platform projects own application packaging."""
import hashlib
import json
import os
from pathlib import Path
from SCons.Script import Environment, Default, Delete, COMMAND_LINE_TARGETS
from build_layout import PACKAGE_VERSION, write_if_changed, prepare_directory
import ccache
from mobile_toolchain import discover, LOCK
from mobile_artifacts import verify_android_library, archive_object_name
from javascript import javascript_objects
from sources import CLIENT_SOURCES, GAG_SOURCES, USL_SOURCES, INCLUDE_DIRECTORIES


def build_mobile(directory, identity, arguments):
    output = Path(directory).resolve()
    # The Amazon flavor changes game behavior, not its pinned native dependencies.
    dependency_identity = dict(identity)
    dependency_identity.pop('amazon', None)
    toolchain = discover(dependency_identity, arguments)
    prefix = Path(arguments.get('mobile_deps', output / 'deps')).resolve()
    manifest = prefix / 'manifest.json'
    if not manifest.is_file():
        raise ValueError(f'Missing cross-compiled dependency manifest {manifest}; see docs/mobile/development.md. Host libraries are never used.')
    dependencies = json.loads(manifest.read_text())
    if dependencies.get('identity') != dependency_identity or dependencies.get('toolchain') != toolchain['fingerprint']:
        raise ValueError('Mobile dependencies belong to a different target/compiler configuration')
    artifacts = dependencies.get('archives', {})
    if not artifacts:
        raise ValueError('Mobile dependency manifest has no verified archives')
    libraries = []
    for name, digest in artifacts.items():
        path = (prefix / name).resolve()
        if not path.is_relative_to(prefix) or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Mobile dependency checksum mismatch: {name}')
        if identity['target'] == 'android': verify_android_library(path, identity['arch'])
        libraries.append(str(path))
    compiler_identity = {'fingerprint': toolchain['fingerprint'],
        'dependencies': hashlib.sha256(manifest.read_bytes()).hexdigest()}
    cache_key = hashlib.sha256(json.dumps(compiler_identity, sort_keys=True).encode()).hexdigest()[:20]
    prepare_directory(output / 'compilers' / cache_key, compiler_identity)
    object_root = output / 'obj' / cache_key
    environment = dict(os.environ)
    # Cross builds must not inherit host include/library injection.
    for name in ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET'):
        environment.pop(name, None)
    environment.update(TMPDIR=str(output / 'tmp'), TMP=str(output / 'tmp'), TEMP=str(output / 'tmp'))
    env = Environment(platform='posix', tools=['gcc', 'g++', 'ar', 'gnulink', 'compilation_db'],
        ENV=environment, CC=toolchain['cc'], CXX=toolchain['cxx'], LINK=toolchain['cxx'], AR=toolchain['ar'])
    if ccache.enabled():
        ccache.enable(env)
    config = output / 'include/glob2/BuildConfig.h'
    write_if_changed(config, f'''#pragma once
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "{PACKAGE_VERSION}"
#define PACKAGE_DATA_DIR "."
#define PACKAGE_SOURCE_DIR "."
#define PRIMARY_FONT "sans.ttf"
#define GLOB2_MOBILE 1
#define GLOB2_NATIVE_WSS 1
#define GLOB2_NO_VOICE 1
''' + ('#define GLOB2_CHINA_RELEASE 1\n' if identity.get('china') else '')
        + ('#define GLOB2_AMAZON_RELEASE 1\n' if identity.get('amazon') else ''))
    env.Append(CPPPATH=["#third_party/quickjs-ng", str(output / 'include'), str(prefix / 'include'), str(prefix / 'include/SDL2')] + list(INCLUDE_DIRECTORIES),
        CPPDEFINES=['HAVE_CONFIG_H'], CCFLAGS=toolchain['cflags'] + ['-g', '-O2' if identity['mode'] == 'release' else '-O0'],
        CXXFLAGS=['-std=gnu++20', '-fexceptions'], LINKFLAGS=toolchain['ldflags'], LIBS=[env.File(path) for path in libraries])
    script_objects = javascript_objects(env, object_root / 'third_party', identity['mode'] == 'release', shared=identity['target'] == 'android')
    files = ['src/' + name for name in CLIENT_SOURCES if name not in ('VoiceRecorder.cpp', 'net/irc/IRCTextMessageHandler.cpp')]
    if identity['target'] == 'ios':
        files.remove('src/Glob2.cpp')
        files += ['mobile/ios/SafeArea.mm', 'mobile/ios/Documents.mm', 'mobile/ios/CertificateTrust.cpp']
    files += ['libgag/src/' + name for name in GAG_SOURCES]
    files += ['libusl/src/' + name for name in USL_SOURCES]
    files += ['browser/VoiceRecorder.cpp', 'browser/IRCTextMessageHandler.cpp', 'mobile/MobilePaths.cpp', 'mobile/Documents.cpp', 'mobile/CertificateTrust.cpp', 'mobile/TemporaryFiles.cpp']
    if identity['target'] == 'android':
        files += ['mobile/android/Documents.cpp', 'mobile/android/CertificateTrust.cpp']
        env.Append(LIBS=['android', 'log', 'dl', 'm'])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        env.Append(CPPDEFINES=['main=SDL_main'])
        objects = [env.SharedObject(str(object_root / (name + '.o')), name) for name in files] + script_objects
        program = env.SharedLibrary(str(output / 'lib/main'), objects)
        if 'android-tests' in COMMAND_LINE_TARGETS:
            # Cross-compile the two doctest binaries from test/tests.py as Android PIE
            # executables for adb shell (mobile/android_device_tests.py). They reuse the
            # same game objects; test/support/TestMain.cpp is the entry point and
            # mobile/NativeTestMain.cpp interposes the platform bridges.
            import sys
            sys.path.insert(0, os.path.abspath('test'))  # SCons runs from the repository root
            import tests as registry
            tests = env.Clone()
            tests['CPPDEFINES'] = ['HAVE_CONFIG_H']  # TestMain.cpp defines SDL_MAIN_HANDLED itself
            tests.Append(CPPPATH=['test', 'test/support', 'src/render', 'libgag/src'])
            by_source = dict(zip(files, objects))
            client_objects = [obj for name, obj in by_source.items() if name != 'src/Glob2.cpp'] + script_objects
            library_objects = [obj for name, obj in by_source.items()
                               if name.startswith('libgag/') or name.startswith('libusl/')] + script_objects

            def available(options):
                required = options.get('require', ())
                # Android builds have no desktop OpenGL and are not MinGW.
                return 'opengl' not in required

            def test_objects(entries, prefix):
                out = []
                for entry in entries:
                    source, options = (entry, {}) if isinstance(entry, str) else entry
                    if not available(options):
                        continue
                    compile_env = tests
                    if options.get('cxxflags') or options.get('defines'):
                        compile_env = tests.Clone()
                        compile_env.Append(CXXFLAGS=options.get('cxxflags', []))
                        compile_env.Append(CPPDEFINES=options.get('defines', []))
                    path = source[1:] if source.startswith('#') else 'test/' + source
                    name = prefix + path.replace('/', '_').rsplit('.', 1)[0]
                    out += compile_env.Object(str(object_root / 'tests' / (name + '.o')), path)
                return out

            def production_objects(entries):
                out = []
                for entry in entries:
                    source, options = (entry, {}) if isinstance(entry, str) else entry
                    key = source.lstrip('#')
                    if key in by_source and not options.get('defines') and not options.get('cxxflags'):
                        out.append(by_source[key])
                    else:
                        out += test_objects([entry], 'unit-')
                return out

            bridge = tests.Object(str(object_root / 'tests' / 'NativeTestMain.o'), 'mobile/NativeTestMain.cpp')
            engine = tests.Program(str(output / 'tests' / 'glob2-engine-tests'),
                                   test_objects(registry.SUPPORT + registry.ENGINE_SUPPORT + registry.ENGINE_TESTS, 'engine-')
                                   + bridge + client_objects)
            unit = tests.Program(str(output / 'tests' / 'glob2-unit-tests'),
                                 test_objects(registry.SUPPORT + registry.UNIT_STUBS + registry.UNIT_TESTS, 'unit-')
                                 + production_objects(registry.UNIT_PRODUCTION_SOURCES) + bridge + library_objects)
            env.Alias('android-tests', engine + unit)
    else:
        # Xcode links the archive with the SDL startup and system frameworks.
        objc = env.Clone()
        objc.Append(CCFLAGS=['-fobjc-arc'])
        objects = [(objc if name == 'mobile/ios/Documents.mm' else env).Object(str(object_root / archive_object_name(name)), name) for name in files] + script_objects
        # ar replaces matching members but otherwise retains obsolete names.
        # Recreate this owned output so renamed/removed sources cannot survive.
        env['ARCOM'] = [Delete('$TARGET'), env['ARCOM']]
        program = env.StaticLibrary(str(output / 'lib/glob2'), objects)
    env.Depends(objects, [str(config), str(LOCK), str(manifest)])
    database = env.CompilationDatabase(str(output / 'compile_commands.json'))
    env.Alias('compile_commands.json', database)
    Default(program, database)
    write_if_changed(output / 'toolchain.json', json.dumps(toolchain, indent=2, sort_keys=True) + '\n')
