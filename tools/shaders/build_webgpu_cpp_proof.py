#!/usr/bin/env python3
"""Build the actual WebGPU C++ correctness fixture using pinned local inputs."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emxx', required=True, type=Path)
parser.add_argument('--sdl-prefix', required=True, type=Path)
parser.add_argument('--shader-directory', required=True, type=Path)
parser.add_argument('--output', default=Path('artifacts/webgpu-cpp'), type=Path)
parser.add_argument('--thread-preparation', action='store_true', help='Prove preload proxy in an actual SDK pthread')
parser.add_argument('--source-root', default=Path(__file__).resolve().parents[2],
                    type=Path, help='Integrated checkout supplying all production sources and headers')
args = parser.parse_args()
root = args.source_root.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
shader_directory = args.shader_directory.resolve()
command = [str(args.emxx.resolve()), '-std=c++20', '-O1', '-fexceptions',
           '-sDISABLE_EXCEPTION_CATCHING=0', '-Ilibgag/include',
           '-I' + str(args.sdl_prefix.resolve() / 'include'),
           '-I' + str(shader_directory),
           'tools/shaders/WebGPUThreadPreparationProof.cpp' if args.thread_preparation else 'tools/shaders/WebGPUCppProof.cpp',
           'libgag/src/WebGPUDevice.cpp', 'libgag/src/RenderFrame.cpp',
           str(args.sdl_prefix.resolve() / 'lib/libSDL3.a'),
           '--js-library', 'browser/webgpu-device.js', '-sALLOW_MEMORY_GROWTH=1',
           '-sINITIAL_MEMORY=67108864', '-sENVIRONMENT=web',
           '-sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString', '-sMIN_WEBGL_VERSION=2',
           '-sMAX_WEBGL_VERSION=2', '--no-entry', '--preload-file',
           str(shader_directory) + '@/shaders', '-o', str(output / 'proof.js')]
if args.thread_preparation: command += ['-pthread', '-sPTHREAD_POOL_SIZE=1']
subprocess.run(command, cwd=root, check=True)
(output / 'index.html').write_text('''<!doctype html>
<canvas id="canvas" width="64" height="64"></canvas>
<script>var Module={canvas:document.getElementById('canvas'),
onRuntimeInitialized(){window.proofReady=true},printErr:console.error};</script>
<script src="proof.js"></script>
''')
