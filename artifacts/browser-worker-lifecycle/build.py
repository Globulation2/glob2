from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[2]
sdk = Path(os.environ['EMSDK'])
compiler = sdk / 'upstream/emscripten/em++'
env = dict(os.environ, EM_CACHE=str(root / 'artifacts/browser-worker-lifecycle/cache'),
           EM_PORTS=str(root / 'artifacts/browser-worker-lifecycle/ports'))
for name in ('audio-baseline', 'net-baseline', 'audio', 'net'):
    command = [str(compiler), 'artifacts/browser-worker-lifecycle/' + name + '.cpp',
               '-Isrc/net', '-std=c++20', '-pthread', '-sPTHREAD_POOL_SIZE=1',
               '-sALLOW_BLOCKING_ON_MAIN_THREAD=0']
    command += ['--use-port=sdl2'] if name.startswith('audio') else ['-fexceptions']
    command += ['-o', 'artifacts/browser-worker-lifecycle/' + name + '.js']
    subprocess.run(command, cwd=root, env=env, check=True)
