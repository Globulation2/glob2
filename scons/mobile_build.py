"""Mobile C++ artifacts; platform projects own application packaging."""
import hashlib
import json
import os
from pathlib import Path
from SCons.Script import Environment, Default, Delete
from build_layout import write_if_changed, prepare_directory
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
    config = output / 'include/glob2/BuildConfig.h'
    write_if_changed(config, '''#pragma once
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "0.9.5.0"
#define PACKAGE_DATA_DIR "."
#define PACKAGE_SOURCE_DIR "."
#define PRIMARY_FONT "sans.ttf"
#define GLOB2_MOBILE 1
#define GLOB2_NO_VOICE 1
''')
    env.Append(CPPPATH=[str(output / 'include'), str(prefix / 'include'), str(prefix / 'include/SDL2')] + list(INCLUDE_DIRECTORIES),
        CPPDEFINES=['HAVE_CONFIG_H'], CCFLAGS=toolchain['cflags'] + ['-g', '-O2' if identity['mode'] == 'release' else '-O0'],
        CXXFLAGS=['-std=gnu++20', '-fexceptions'], LINKFLAGS=toolchain['ldflags'], LIBS=[env.File(path) for path in libraries])
    files = ['src/' + name for name in CLIENT_SOURCES if name not in ('VoiceRecorder.cpp', 'net/irc/IRCTextMessageHandler.cpp')]
    if identity['target'] == 'ios':
        files.remove('src/Glob2.cpp')
        files += ['mobile/ios/SafeArea.mm', 'mobile/ios/Documents.mm', 'mobile/ios/CertificateTrust.cpp']
    files += ['libgag/src/' + name for name in GAG_SOURCES]
    files += ['libusl/src/' + name for name in USL_SOURCES]
    files += ['browser/VoiceRecorder.cpp', 'browser/IRCTextMessageHandler.cpp', 'mobile/MobilePaths.cpp', 'mobile/Documents.cpp', 'mobile/CertificateTrust.cpp']
    if identity['target'] == 'android':
        files += ['mobile/android/Documents.cpp', 'mobile/android/CertificateTrust.cpp']
        env.Append(LIBS=['android', 'log', 'dl', 'm'])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        env.Append(CPPDEFINES=['main=SDL_main'])
        objects = [env.SharedObject(str(object_root / (name + '.o')), name) for name in files]
        program = env.SharedLibrary(str(output / 'lib/main'), objects)
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
