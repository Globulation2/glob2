from pathlib import Path
import shlex
import subprocess

original = Path('/Users/bradley/.codex/worktrees/d037/glob2')
out = Path('artifacts/render-profile/default-validation')
log = (original / 'artifacts/render-profile/direct-game/release-device/build.log').read_text().splitlines()
build = shlex.split(next(line for line in log if ' -c ' in line and line.endswith('libs/gag/src/ProgrammableGLDevice.cpp')))
build[build.index('-o') + 1] = str(out / 'ProgrammableGLDevice.o')
build = ['-I' + str(original / arg[2:]) if arg.startswith('-Ibuild/') else arg for arg in build]
subprocess.run(build, check=True)
args = shlex.split(next(line for line in log if line.startswith('g++ -o ') and '/test/torus-render-benchmark ' in line))
first = args.index('build/darwin/client/release/libs/gag/src/libgag.a')
args = [str(original / arg) if arg.startswith('build/') else arg for arg in args[first:]]
fixture = out / 'rollback.cpp'
fixture.write_text((original / 'artifacts/render-profile/committed-copy/device.cpp').read_text().replace(
    'build/darwin/client/release/shaders', str((Path.cwd() / 'artifacts/render-profile/default-direct-shaders').resolve())))
binary = out / 'rollback'
subprocess.run(['clang++', '-O2', '-std=c++20', '-Ilibs/gag/include', '-I/opt/homebrew/include',
                str(fixture), str(out / 'ProgrammableGLDevice.o'), '-o', str(binary), *args], check=True)
subprocess.run([str(binary)], check=True)
