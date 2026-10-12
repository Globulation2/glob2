#!/usr/bin/env python3
"""Retain renderer provenance and run the existing release torus benchmark.

No timings are synthesized: raw production benchmark logs are authoritative.
Inventory without --binary is useful on machines with incomplete SDKs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import shlex
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def capture(command, timeout=30):
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
        return {'command': command, 'exit_code': result.returncode,
                'stdout': result.stdout, 'stderr': result.stderr}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'command': command, 'unavailable': str(error)}


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def compiler_command(line):
    """Recognize actual compiler invocations, including the configured cache wrapper."""
    try:
        command = shlex.split(line)
    except ValueError:
        return None
    if not command:
        return None
    index = 0
    if Path(command[index]).name in ('ccache', 'sccache'):
        index += 1
    if index >= len(command):
        return None
    name = Path(command[index]).name
    compilers = ('g++', 'gcc', 'clang++', 'clang', 'c++', 'cc')
    if name not in compilers and not any(name.endswith('-' + compiler) for compiler in compilers):
        return None
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/renderer-feasibility')
    parser.add_argument('--binary', type=Path, help='release torus-render-benchmark executable')
    parser.add_argument('--build-log', type=Path, help='completed release build log with actual compiler/link commands')
    parser.add_argument('--build-command', help='exact release build command; required with --binary')
    parser.add_argument('--base', default='HEAD', help='recorded integration base revision')
    parser.add_argument('--workload', choices=['flat-warm','flat-streaming','torus'], default='flat-warm')
    parser.add_argument('--renderer', default='gpu', help='existing benchmark renderer selector')
    parser.add_argument('--zoom', type=float, help='fixed zoom for native-scale composition measurements')
    parser.add_argument('--save', type=Path, help='optional saved game instead of seeded flat fixture')
    parser.add_argument('--frames', type=int, default=300)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--repeat', type=int, default=7)
    args = parser.parse_args()
    if args.binary and (not args.build_command or 'release=1' not in args.build_command or
                        any(flag in args.build_command for flag in ('dev_fast=1', 'pch=1', 'unity=1'))):
        parser.error('--binary requires a recorded release=1 build command without fast/PCH/unity flags')
    if args.zoom is not None and (not 0 < args.zoom <= 32):
        parser.error('--zoom must be positive and at most 32')
    if args.frames < 1 or args.warmup < 0 or args.repeat < 1:
        parser.error('frames/repeat must be positive; warmup must be nonnegative')
    if args.binary and not args.binary.is_file():
        parser.error('benchmark binary does not exist')
    if args.save and not args.save.is_file():
        parser.error('saved-game fixture does not exist')
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'platform': platform.platform(), 'machine': platform.machine(),
              'revision': capture(['git', 'rev-parse', 'HEAD']),
              'base': capture(['git', 'rev-parse', args.base]),
              'working_tree': capture(['git', 'status', '--porcelain']),
              'build_command': args.build_command, 'workload': args.workload,
              'requested_backend':args.renderer, 'zoom':args.zoom, 'probes': [], 'runs': [],
              'limitations': ['Inventory/device creation do not establish rendered feature coverage.',
                              'Torus benchmark excludes presentation and simulation; see its guide.',
                              'Release flags are declared provenance, not inferred from the executable.']}
    for command in [['g++', '--version'], ['clang++', '--version'], ['emcc', '--version'],
                    ['pkg-config', '--modversion', 'sdl3'], ['nvidia-smi', '--query-gpu=name,driver_version',
                     '--format=csv,noheader'], ['vulkaninfo', '--summary'], ['glxinfo', '-B']]:
        report['probes'].append(capture(command))
    report['pins'] = {str(p.relative_to(ROOT)): {'sha256': digest(p), 'contents': json.loads(p.read_text())}
                      for p in [ROOT / 'scons/sdl3-versions.json', ROOT / 'browser/toolchain.json']}
    report['drm_devices'] = []
    for device in sorted(Path('/sys/class/drm').glob('card[0-9]*/device')):
        if not (device / 'vendor').is_file():
            continue
        report['drm_devices'].append({'device': str(device.resolve()),
            'vendor': (device / 'vendor').read_text().strip(),
            'pci_device': (device / 'device').read_text().strip(),
            'driver': str((device / 'driver').resolve())})
    report['changes_from_base']=capture(['git','diff','--name-only',args.base,'HEAD'])
    if args.build_log:
        commands=[]; inputs={}
        for line in args.build_log.read_text().splitlines():
            command=compiler_command(line)
            if command is None: continue
            commands.append(command)
            for item in command:
                path=ROOT/item
                if item.endswith(('.cpp','.c')) and path.is_file(): inputs[item]=digest(path)
        report['build_provenance']={'log':str(args.build_log.resolve()),'sha256':digest(args.build_log),
                                    'commands':commands,'source_sha256':inputs}
    if args.binary:
        binary = args.binary.resolve()
        report['binary'] = {'path': str(binary), 'sha256': digest(binary)}
        if args.save:
            report['fixture'] = {'path': str(args.save.resolve()), 'sha256': digest(args.save)}
        for repeat in range(args.repeat):
            with tempfile.TemporaryDirectory(prefix='glob2-render-baseline-') as profile:
                env = {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_BENCH_')}
                env.update(GLOB2_USER_DATA_DIR=profile,
                           GLOB2_BENCH_FRAMES=str(args.frames), GLOB2_BENCH_WARMUP_FRAMES=str(args.warmup),
                           GLOB2_BENCH_FRAME_TIMES='1', GLOB2_BENCH_PAUSE_PRESENTATION='1',
                           GLOB2_BENCH_CAPTURE=str((args.output / f'frame-{repeat}.ppm').resolve()))
                if args.zoom is not None:
                    env['GLOB2_BENCH_ZOOM']=str(args.zoom)
                if args.workload.startswith('flat'):
                    env['GLOB2_BENCH_FLAT']='1'
                if args.workload=='flat-streaming':
                    env['GLOB2_BENCH_CAMERA_MOTION']='1'
                if args.save:
                    env['GLOB2_BENCH_GAME'] = str(args.save.resolve())
                else:
                    env.update(GLOB2_BENCH_SIZE='256x256', GLOB2_BENCH_UNITS='1000')
                command = [str(binary), '--renderer', args.renderer, '--no-fullscreen', '--mute',
                           '--window-size', '1280x800']
                started=time.perf_counter()
                run = subprocess.run(command, env=env, cwd=ROOT, capture_output=True, text=True, timeout=600)
                elapsed=time.perf_counter()-started
                log = args.output / f'run-{repeat}.log'
                log.write_text(run.stdout + run.stderr)
                report['runs'].append({'command': command, 'exit_code': run.returncode, 'log': str(log),
                    'environment': {k: v for k, v in env.items() if k.startswith('GLOB2_BENCH_')},
                    'log_sha256': digest(log), 'process_wall_seconds':elapsed})
                if run.returncode:
                    break
    (args.output / 'baseline.json').write_text(json.dumps(report, indent=2) + '\n')
    print(args.output / 'baseline.json')
    return int(any(run['exit_code'] for run in report['runs']))


if __name__ == '__main__':
    raise SystemExit(main())
