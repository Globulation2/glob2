#!/usr/bin/env python3
"""Build the actual programmable GL C++ lifecycle fixture using pinned local inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emxx', required=True, type=Path)
parser.add_argument('--sdl-prefix', required=True, type=Path)
parser.add_argument('--shader-directory', required=True, type=Path)
parser.add_argument('--output', default=Path('artifacts/programmable-gl-cpp'), type=Path)
parser.add_argument('--source-root', type=Path, help='Optional integration checkout for backend/core inputs')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
fixture_root = root
root = args.source_root.resolve() if args.source_root else root
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
shader_directory = args.shader_directory.resolve()
command = [str(args.emxx.resolve()), '-std=c++20', '-O1', '-fexceptions',
           '-sDISABLE_EXCEPTION_CATCHING=0', '-Ilibgag/include',
           '-I' + str(args.sdl_prefix.resolve() / 'include'),
           '-I' + str(shader_directory), str(fixture_root / 'tools/shaders/ProgrammableGLCppProof.cpp'),
           'libs/gag/src/ProgrammableGLDevice.cpp', 'libs/gag/src/RenderFrame.cpp',
           str(args.sdl_prefix.resolve() / 'lib/libSDL3.a'),
           '-sALLOW_MEMORY_GROWTH=1',
           '-sINITIAL_MEMORY=67108864', '-sENVIRONMENT=web',
           '-sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString', '-sMIN_WEBGL_VERSION=2',
           '-sMAX_WEBGL_VERSION=2', '--no-entry', '--preload-file',
           str(shader_directory) + '@/shaders', '-o', str(output / 'proof.js')]
inputs = [fixture_root / 'tools/shaders/ProgrammableGLCppProof.cpp',
          root / 'libs/gag/src/ProgrammableGLDevice.cpp',
          root / 'libs/gag/src/RenderFrame.cpp',
          root / 'libs/gag/include/RenderDevice.h',
          root / 'libs/gag/include/RenderFrame.h',
          root / 'libs/gag/include/RenderResources.h',
          args.sdl_prefix.resolve() / 'lib/libSDL3.a',
          shader_directory / 'metadata.json']
provenance = {
    'sourceRevision': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'command': command,
    'inputHashes': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in inputs},
    'compilerVersion': subprocess.check_output(
        [str(args.emxx.resolve()), '--version'], text=True),
}
subprocess.run(command, cwd=root, check=True)
provenance['outputHashes'] = {
    path.name: hashlib.sha256(path.read_bytes()).hexdigest()
    for path in output.glob('proof.*')}
(output / 'build-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
(output / 'index.html').write_text('''<!doctype html>
<canvas id="canvas" width="64" height="64"></canvas>
<script>var Module={canvas:document.getElementById('canvas'),
onRuntimeInitialized(){window.proofReady=true},printErr:console.error};</script>
<script src="proof.js"></script>
''')
