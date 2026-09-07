#!/usr/bin/env python3
"""Link the native regression against an existing SCons engine build."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir',type=Path,required=True)
args=parser.parse_args()
build=(ROOT/args.build_dir).resolve()
packages=['sdl2','SDL2_net','SDL2_ttf','SDL2_image','vorbisfile','speex','fribidi','epoxy']
def pkg(flag):
    return shlex.split(subprocess.check_output(['pkg-config',flag,*packages],text=True))
sources=(ROOT/'src/SConscript').read_text().split('"""')[1].split()
objects=[build/'src'/Path(s).with_suffix('.o') for s in sources if s!='Glob2.cpp']
objects += [build/'libgag/src/libgag.a',build/'libusl/src/libusl.a']
missing=[str(p) for p in objects if not p.is_file()]
if missing:parser.error('Build the engine first; missing: '+', '.join(missing))
with tempfile.TemporaryDirectory(prefix='g2-test-') as temp:
    binary=Path(temp)/'test'
    command=['c++','-std=gnu++20','-O1','-UNDEBUG','-I.','-Isrc','-Ilibgag/include','-Ilibusl/src',
             *['-I'+str(p) for p in (ROOT/'src').rglob('*') if p.is_dir()],*pkg('--cflags'),
             'test/ClearingFlagGradientTest.cpp',*map(str,objects),*pkg('--libs'),'-lboost_date_time',
             '-lpthread','-lz','-lGL','-lGLU','-o',str(binary)]
    subprocess.run(command,cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,check=True)
