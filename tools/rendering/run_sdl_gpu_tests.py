#!/usr/bin/env python3
"""Build and run Vulkan renderer differential tests with explicit pinned inputs.

This is a development harness, not a game build or a performance measurement.
Requires SDL 3.4.16 prefix and generated primitive SPIR-V/layout assets.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdl-prefix', type=Path, required=True)
    parser.add_argument('--shader-directory', type=Path, required=True)
    parser.add_argument('--core-root', type=Path, default=ROOT, help='integration checkout containing RenderDevice core')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/renderer-feasibility')
    parser.add_argument('--desktop-gl', action='store_true', help='run identical skin proof through the desktop GL factory on this hardware')
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    prefix, shaders, core, output = [p.resolve() for p in (args.sdl_prefix, args.shader_directory, args.core_root, args.output)]
    inputs = [ROOT / 'libgag/src/SDLGPUDevice.cpp', ROOT / 'libgag/src/SDLGPUDeviceTest.cpp',
              core / 'libgag/src/RenderDevice.cpp', core / 'libgag/src/RenderFrame.cpp',
              core / 'libgag/src/RenderResources.cpp', ROOT / 'libgag/src/SoftwareRenderBackend.cpp',
              ROOT / 'libgag/src/RenderBackend.cpp', ROOT / 'libgag/src/SurfaceRaster.cpp']
    if args.desktop_gl:
        inputs.append(core / 'libgag/src/ProgrammableGLDevice.cpp')
    assets = [shaders / 'metadata.json', shaders / 'RendererShaderLayouts.h'] + [shaders / 'vulkan' / (name + '.' + stage + '.spv') for name in ('primitive', 'world', 'cloud', 'point', 'skin', 'skin-rig') for stage in ('vert', 'frag')]
    for path in inputs + assets + [prefix / 'include/SDL3/SDL.h']:
        if not path.is_file():
            parser.error(f'Missing input: {path}')
    assets += sorted(path for path in (shaders / 'vulkan').glob('*.spv') if path not in assets)
    assets += [shaders / name for name in ('webgl-skin-vertices.f32','webgl-rig-vertices.f32') if (shaders / name).is_file()]
    assets += [core / 'libgag/include' / name for name in ('RenderDevice.h','RenderFrame.h','RenderResources.h')]
    output.mkdir(parents=True, exist_ok=True)
    overlay=output / 'include'
    overlay.mkdir(exist_ok=True)
    native_header=ROOT / 'libgag/include/SDLGPUDevice.h'
    (overlay / native_header.name).write_bytes(native_header.read_bytes())
    assets.append(native_header)
    entry = output / 'sdl-gpu-test-main.cpp'
    entry.write_text('#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include "Glob2Test.h"\n')
    binary = output / 'sdl-gpu-device-tests'
    command = [args.compiler, '-std=c++20', '-O0', '-g', '-Wall', '-Wextra']
    if args.desktop_gl:
        command += ['-DGLOB2_PROGRAMMABLE_GL=1', '-DGLOB2_TEST_COMPARE_GL=1']
    for directory in [overlay, core / 'libgag/include', ROOT / 'libgag/include', ROOT / 'test/support', ROOT / 'third_party/nlohmann-json/include', shaders, prefix / 'include']:
        command.append('-I' + str(directory))
    command += [str(path) for path in inputs] + [str(entry), '-L' + str(prefix / 'lib'),
                '-Wl,-rpath,' + str(prefix / 'lib'), '-lSDL3', '-o', str(binary)]
    report = {'platform': platform.platform(), 'machine': platform.machine(), 'backend': 'programmable-gl' if args.desktop_gl else 'sdl-gpu/vulkan', 'build_command': command,
              'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_hashes': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs + assets},
              'limitations': ['Native packet fixtures on this host only; not full gameplay/recording or other native APIs.',
                              'Triangle interiors are compared; driver/software edge ownership is excluded.']}
    for label, argv, environment in [('build', command, os.environ.copy()),
                                     ('run', [str(binary)] + (['--test-case=*authoritative skin*'] if args.desktop_gl else []), dict(os.environ, GLOB2_TEST_SHADER_DIRECTORY=str(shaders), GLOB2_TEST_CAPTURE_DIRECTORY=str(output)))]:
        run = subprocess.run(argv, cwd=ROOT, env=environment, capture_output=True, text=True, timeout=180)
        log = output / f'sdl-gpu-tests-{label}.log'
        log.write_text(run.stdout + run.stderr)
        report[label] = {'command': argv, 'exit_code': run.returncode, 'log': str(log)}
        print(log)
        if run.returncode:
            break
    if binary.is_file():
        report['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
    (output / 'sdl-gpu-tests.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(report[label]['exit_code'] for label in ('build', 'run') if label in report))


if __name__ == '__main__':
    raise SystemExit(main())
