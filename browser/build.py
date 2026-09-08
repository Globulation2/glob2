#!/usr/bin/env python3
"""Build the isolated single-player experiment using Emscripten 4.0.15."""
import concurrent.futures
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
OUT = ROOT / 'build-browser'
OUT.mkdir(exist_ok=True)
EMXX = os.environ.get('EMXX', str(ROOT / 'tools/browser-emsdk/upstream/emscripten/em++'))
if not Path(EMXX).exists() and not shutil.which(EMXX):
    raise SystemExit('Install the SDK using the commands in browser/README.md.')
boost = Path(os.environ.get('BOOST_INCLUDE', '/opt/homebrew/include')) / 'boost'
if not boost.exists():
    raise SystemExit('Set BOOST_INCLUDE to the directory containing Boost headers.')
(OUT / 'include').mkdir(exist_ok=True)
link = OUT / 'include/boost'
if not link.exists():
    link.symlink_to(boost, target_is_directory=True)
(OUT / 'include/config.h').write_text('''#pragma once
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "Browser experiment"
#define PACKAGE_DATA_DIR "/"
#define PACKAGE_SOURCE_DIR "/"
#define PRIMARY_FONT "sans.ttf"
''')

def sources(path, variable):
    content = Path(path).read_text()
    return [str(Path(path).parent / s) for s in re.search(variable + r'\s*=\s*Split\("""(.*?)"""\)', content, re.S).group(1).split()]

files = sources('src/SConscript', 'source_files') + sources('libgag/src/SConscript', 'libgag_sources') + sources('libusl/src/SConscript', 'usl_sources')
files.remove('src/VoiceRecorder.cpp')
files.append('browser/VoiceRecorder.cpp')
includes = ['build-browser/include', '.', 'libgag/include', 'libusl/src']
includes += [str(p) for p in Path('src').rglob('*') if p.is_dir()]
includes.append('src')
ports = ['--use-port=sdl2', '--use-port=sdl2_image', '--use-port=sdl2_ttf', '--use-port=sdl2_net', '--use-port=vorbis', '--use-port=zlib', '-sSDL2_IMAGE_FORMATS=["png","jpg"]']
flags = ['-std=gnu++20', '-O2', '-g2', '-fexceptions', '-DHAVE_CONFIG_H', '-include', 'browser/BrowserPlatform.h'] + ['-I' + p for p in includes] + ports
# Resolve/download ports serially before parallel compilation touches the cache.
subprocess.run([EMXX, *flags, '-x', 'c++', '-c', '/dev/null', '-o', str(OUT / 'ports.o')], check=True)
signature = hashlib.sha256((' '.join(flags) + Path(__file__).read_text()).encode()).hexdigest()
stamp = OUT / 'build-signature'
rebuild = not stamp.exists() or stamp.read_text() != signature
headers = [p for d in ('src','libgag','libusl','browser') for p in Path(d).rglob('*.h')]
newest_header = max(p.stat().st_mtime for p in headers)

def compile_one(source):
    obj = OUT / 'obj' / (source + '.o')
    obj.parent.mkdir(parents=True, exist_ok=True)
    if rebuild or not obj.exists() or obj.stat().st_mtime < max(Path(source).stat().st_mtime, newest_header):
        result = subprocess.run([EMXX, *flags, '-c', source, '-o', str(obj)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(source + '\n' + result.stdout)
        print(source, flush=True)
    return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get('JOBS', '8'))) as pool:
    objects = list(pool.map(compile_one, files))
stamp.write_text(signature)
link_flags = ['-sASYNCIFY', '-sASYNCIFY_STACK_SIZE=1048576', '-sALLOW_MEMORY_GROWTH', '-sINITIAL_MEMORY=134217728', '-sSTACK_SIZE=8388608', '-sASSERTIONS=1', '-sFORCE_FILESYSTEM', '-lidbfs.js', '-sEXPORTED_RUNTIME_METHODS=["callMain","FS"]', '--shell-file', 'browser/shell.html']
for directory in ('data', 'maps', 'campaigns', 'scripts'):
    link_flags += ['--preload-file', directory + '@/' + directory, '--exclude-file', '*/SConscript']
subprocess.run([EMXX, *flags, *objects, *link_flags, '-o', str(OUT / 'index.html')], check=True)
print('Built build-browser/index.html', flush=True)
