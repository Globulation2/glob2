"""Mobile C++ artifacts; platform projects own application packaging."""
import hashlib
import json
import os
import runpy
from pathlib import Path
from SCons.Script import Environment, Default, Delete, COMMAND_LINE_TARGETS
from build_layout import write_if_changed, prepare_directory
import ccache
from mobile_toolchain import discover, LOCK
from mobile_artifacts import verify_android_library, archive_object_name
from sources import CLIENT_SOURCES, GAG_SOURCES, USL_SOURCES, INCLUDE_DIRECTORIES


def build_mobile(directory, identity, arguments):
    output = Path(directory).resolve()
    toolchain = discover(identity, arguments)
    prefix = Path(arguments.get('mobile_deps', output / 'deps')).resolve()
    manifest = prefix / 'manifest.json'
    if not manifest.is_file():
        raise ValueError(f'Missing cross-compiled dependency manifest {manifest}; see docs/mobile/development.md. Host libraries are never used.')
    dependencies = json.loads(manifest.read_text())
    if dependencies.get('identity') != identity or dependencies.get('toolchain') != toolchain['fingerprint']:
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
    write_if_changed(config, '''#pragma once
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "0.9.5.0"
#define PACKAGE_DATA_DIR "."
#define PACKAGE_SOURCE_DIR "."
#define PRIMARY_FONT "sans.ttf"
#define GLOB2_MOBILE 1
#define GLOB2_NATIVE_WSS 1
#define GLOB2_NO_VOICE 1
''' + ('#define GLOB2_CHINA_RELEASE 1\n' if identity.get('china') else ''))
    env.Append(CPPPATH=[str(output / 'include'), str(prefix / 'include'), str(prefix / 'include/SDL2')] + list(INCLUDE_DIRECTORIES),
        CPPDEFINES=['HAVE_CONFIG_H'], CCFLAGS=toolchain['cflags'] + ['-g', '-O2' if identity['mode'] == 'release' else '-O0'],
        CXXFLAGS=['-std=gnu++20', '-fexceptions'], LINKFLAGS=toolchain['ldflags'], LIBS=[env.File(path) for path in libraries])
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
        objects = [env.SharedObject(str(object_root / (name + '.o')), name) for name in files]
        program = env.SharedLibrary(str(output / 'lib/main'), objects)
        if 'android-tests' in COMMAND_LINE_TARGETS:
            # Run the real client harnesses as Android PIE executables under
            # adb shell. Reuse the same cross-compiled game objects; only the
            # entry point differs. SDL's dummy drivers avoid requiring a Java
            # Activity for these deterministic/input regression tests.
            tests = env.Clone()
            tests['CPPDEFINES'] = ['HAVE_CONFIG_H', 'SDL_MAIN_HANDLED']
            client_objects = [obj for name, obj in zip(files, objects)
                              if name != 'src/Glob2.cpp']
            harnesses = ('MobileInputHarness', 'MobilePresentationHarness',
                         'ResponsiveMenuHarness', 'GameGUITouchHarness',
                         'EngineSessionHarness', 'GameGUISelectionHarness',
                         'TerrainResourcesHarness', 'TeamStatsSaveHarness')
            binaries = []
            for harness in harnesses:
                entry = tests.Object(str(object_root / 'tests' / (harness + '.o')),
                                     'test/' + harness + '.cpp')
                entry += tests.Object(str(object_root / 'tests' / (harness + '-main.o')),
                                      'mobile/NativeTestMain.cpp')
                binaries += tests.Program(str(output / 'tests' / harness),
                                          (client_objects if harness != 'MobileInputHarness' else []) + entry)
            env.Alias('android-tests', binaries)
        if 'android-unit-tests' in COMMAND_LINE_TARGETS:
            # The same CppUnit suite as `scons -C test`, with an isolated
            # cross-compiled test dependency (never linked into the game APK).
            test_arch = {'arm64-v8a': 'arm64', 'armeabi-v7a': 'arm', 'x86_64': 'x64'}[identity['arch']]
            unit_prefix = Path(arguments.get('cppunit_deps', output / 'test-deps' /
                                             ('glob2-' + test_arch + '-android')))
            cppunit = unit_prefix / 'lib/libcppunit.a'
            if not cppunit.is_file():
                raise ValueError('Build the Android CppUnit dependency first: ' + str(cppunit))
            verify_android_library(cppunit, identity['arch'])
            unit = env.Clone()
            unit['CPPDEFINES'] = ['HAVE_CONFIG_H', 'SDL_MAIN_HANDLED']
            unit.Append(CPPPATH=[str(unit_prefix / 'include'), 'src/render'], LIBS=[unit.File(str(cppunit))])
            names = runpy.run_path('test/cppunit_sources.py')['CPPUNIT_SOURCES']
            unit_objects = []
            for name in names:
                source = os.path.normpath('test/' + name)
                unit_objects += unit.Object(str(object_root / 'cppunit' / (source + '.o')), source)
            unit_objects += unit.Object(str(object_root / 'cppunit/main.o'), 'mobile/NativeTestMain.cpp')
            suite = unit.Program(str(output / 'tests/TestsRunner'), unit_objects)
            env.Alias('android-unit-tests', suite)
    else:
        # Xcode links the archive with the SDL startup and system frameworks.
        objc = env.Clone()
        objc.Append(CCFLAGS=['-fobjc-arc'])
        objects = [(objc if name == 'mobile/ios/Documents.mm' else env).Object(str(object_root / archive_object_name(name)), name) for name in files]
        # ar replaces matching members but otherwise retains obsolete names.
        # Recreate this owned output so renamed/removed sources cannot survive.
        env['ARCOM'] = [Delete('$TARGET'), env['ARCOM']]
        program = env.StaticLibrary(str(output / 'lib/glob2'), objects)
    env.Depends(objects, [str(config), str(LOCK), str(manifest)])
    database = env.CompilationDatabase(str(output / 'compile_commands.json'))
    env.Alias('compile_commands.json', database)
    Default(program, database)
    write_if_changed(output / 'toolchain.json', json.dumps(toolchain, indent=2, sort_keys=True) + '\n')
