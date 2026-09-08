from pathlib import Path
import os
import sys
import subprocess
from SCons.Script import Environment, Default, Tool


def build_gateway(directory, identity, arguments):
    env=Environment(ENV=dict(os.environ))
    temporary = str((Path(directory) / 'tmp').resolve())
    env['ENV'].update(TMPDIR=temporary, TMP=temporary, TEMP=temporary)
    if identity['toolchain'] in ('mingw', 'mingwcross'):
        Tool('mingw')(env)
    if identity['toolchain'] == 'mingwcross':
        env.Replace(CC='x86_64-w64-mingw32-gcc', CXX='x86_64-w64-mingw32-g++',
                    LINK='x86_64-w64-mingw32-g++')
    env.Append(CXXFLAGS=['-std=c++20','-Wall','-Wextra','-O2' if identity['mode']=='release' else '-g'])
    if sys.platform=='darwin' and identity['toolchain'] == 'darwin':
        try:
            prefix=subprocess.check_output(['brew','--prefix'],text=True).strip()
            env.Append(CPPPATH=[prefix+'/include'])
        except (FileNotFoundError, subprocess.CalledProcessError):
            pass
    env.Append(CPPDEFINES=['BOOST_ERROR_CODE_HEADER_ONLY'])
    if sys.platform=='win32' or identity['toolchain'] in ('mingw', 'mingwcross'):
        env.Append(LIBS=['ws2_32','mswsock'])
    else:
        env.Append(LIBS=['pthread'])
    env.Tool('compilation_db')
    obj=env.Object(directory+'/obj/Gateway.o','src/net/gateway/Gateway.cpp')
    program=env.Program(directory+'/glob2-ws-gateway',obj)
    database=env.CompilationDatabase(directory+'/compile_commands.json')
    env.Alias('compile_commands.json',database)
    Default(program,database)
