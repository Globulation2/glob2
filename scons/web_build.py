"""Emscripten toolchain, independent of native configuration and SDK discovery."""
from pathlib import Path
import json
import os
import subprocess
from SCons.Script import Environment, Default, Value, GetOption, Action
from build_layout import write_if_changed
from sources import CLIENT_SOURCES, GAG_SOURCES, USL_SOURCES, INCLUDE_DIRECTORIES

PORTS = ['--use-port=sdl2', '--use-port=sdl2_image:formats=png,jpg',
         '--use-port=sdl2_ttf', '--use-port=sdl2_net', '--use-port=vorbis',
         '--use-port=zlib', '--use-port=boost_headers']


def build_web(directory, identity, arguments):
    root = Path.cwd()
    output = Path(directory).resolve()
    sdk = Path(arguments.get('emsdk', os.environ.get('EMSDK', root / 'tools/browser-emsdk'))).resolve()
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
    build_environment['EM_CACHE'] = str(output / 'cache')
    build_environment['EM_PORTS'] = str(output / 'ports')
    build_environment.update(TMPDIR=str(output / 'tmp'), TMP=str(output / 'tmp'), TEMP=str(output / 'tmp'))
    env = Environment(platform='posix', tools=['gcc', 'g++', 'ar', 'gnulink', 'compilation_db'],
                      ENV=build_environment, CC=str(emscripten / 'emcc'), CXX=str(compiler),
                      LINK=str(compiler), AR=str(emscripten / 'emar'), RANLIB=str(emscripten / 'emranlib'))
    env['PROGSUFFIX'] = '.html'
    config = output / 'include/glob2/BuildConfig.h'
    write_if_changed(config, '''#pragma once
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "Browser development"
#define PACKAGE_DATA_DIR "/"
#define PACKAGE_SOURCE_DIR "/"
#define PRIMARY_FONT "sans.ttf"
''')
    include_paths = [str(output / 'include')] + list(INCLUDE_DIRECTORIES)
    env.Append(CPPPATH=include_paths, CPPDEFINES=['HAVE_CONFIG_H'],
               CXXFLAGS=['-std=gnu++20', '-fexceptions', '-g2', '-O2' if identity['mode']=='release' else '-O0'] + PORTS)
    env.Append(LINKFLAGS=['-fexceptions', '-O2' if identity['mode']=='release' else '-O0',
        '-sASYNCIFY', '-sASYNCIFY_STACK_SIZE=1048576', '-sALLOW_MEMORY_GROWTH',
        '-sINITIAL_MEMORY=134217728', '-sSTACK_SIZE=8388608', '-sASSERTIONS=1',
        '-sFORCE_FILESYSTEM', '-lidbfs.js', '-lwebsocket.js',
        "'-sEXPORTED_RUNTIME_METHODS=[\"callMain\",\"FS\"]'",
        '--shell-file', 'browser/shell.html'] + PORTS)
    for asset_directory in ('data', 'maps', 'campaigns', 'scripts'):
        env.Append(LINKFLAGS=['--preload-file', asset_directory + '@/' + asset_directory])
    env['LINKCOM'] = '${TEMPFILE("$LINK -o $TARGET $LINKFLAGS $__RPATH $SOURCES $_LIBDIRFLAGS $_LIBFLAGS", "$LINKCOMSTR")}'
    def prepare_ports(target, source, env):
        return subprocess.run(
            [str(compiler), *PORTS, '-x', 'c++', '-c', '-o', str(target[0]), '-'],
            input='', text=True, env=env['ENV']).returncode
    ports = env.Command(str(output / 'ports-ready.o'), [Value(lock), Value(PORTS)],
                        Action(prepare_ports, 'Preparing pinned Emscripten ports'))
    files = ['src/' + s for s in CLIENT_SOURCES if s not in ('VoiceRecorder.cpp', 'net/NetTransport.cpp', 'net/irc/IRCTextMessageHandler.cpp')]
    files += ['libgag/src/' + s for s in GAG_SOURCES if s != 'ApplicationHost.cpp']
    files += ['libusl/src/' + s for s in USL_SOURCES]
    files += ['browser/VoiceRecorder.cpp', 'browser/ApplicationHost.cpp', 'browser/NetTransport.cpp', 'browser/IRCTextMessageHandler.cpp']
    objects = [env.Object(str(output / 'obj' / (f + '.o')), f) for f in files]
    env.Requires(objects, ports)
    env.Depends(objects, str(config))
    program = env.Program(str(output / 'index.html'), objects)
    env.Depends(program, ['browser/shell.html', 'browser/toolchain.json'])
    env.Depends(program, [str(p) for directory in ('data','maps','campaigns','scripts')
                         for p in Path(directory).rglob('*') if p.is_file()])
    env.SideEffect([str(output / ('index.'+ext)) for ext in ('js','wasm','data')], program)
    env.Clean(program, [str(output / ('index.'+ext)) for ext in ('js','wasm','data')])
    database = env.CompilationDatabase(str(output / 'compile_commands.json'))
    env.Alias('compile_commands.json', database)
    Default(program, database)
    write_if_changed(output / 'options.json', json.dumps(dict(arguments), sort_keys=True, indent=2)+'\n')
