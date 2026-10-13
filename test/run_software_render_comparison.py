#!/usr/bin/env python3
"""Collect alternating release-renderer comparisons inside the audited cpuset wrapper.

This records raw evidence, not an automatic performance verdict. Profiling runs use
separate external controllers and PROFILE_BOUNDARY_STOP; never mix their timings.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile

CASES = {
    'static-map': {'PROFILE_MODE': 'map', 'PROFILE_WHOLE_MAP': '1', 'PROFILE_PAUSED': '1'},
    'animated-map': {'PROFILE_MODE': 'map', 'PROFILE_WHOLE_MAP': '1'},
    'half-gui': {'PROFILE_MODE': 'gui', 'PROFILE_ZOOM': '0.5', 'PROFILE_PAN': '1', 'PROFILE_PAUSED': '1'},
    'native-gui': {'PROFILE_MODE': 'gui', 'PROFILE_ZOOM': '1', 'PROFILE_PAN': '1', 'PROFILE_PAUSED': '1'},
    'streamed-map': {'PROFILE_MODE': 'map', 'PROFILE_WHOLE_MAP': '1', 'PROFILE_PAUSED': '1', 'PROFILE_TERRAIN_CACHE': '0'},
    'live-simulation': {'PROFILE_MODE': 'interactive', 'PROFILE_SECONDS': '10', 'PROFILE_FPS': '60', 'PROFILE_PAN': '1'},
}


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def fixed_observation(log, frames, zoom):
    camera = re.search(r'CAMERA zoom=([0-9.]+) fractional=([^ ]+) offset=([^\n]+)', log)
    ready = re.search(r'READY .* tick=(\d+) map=(\d+x\d+) teams=(\d+) '
                      r'units=(\d+) buildings=(\d+) surface=(\d+x\d+) '
                      r'camera=(-?\d+,-?\d+) mode=(\w+) clouds=(\d+) frames=(\d+) driver=(\S+)', log)
    completed = re.search(r'render_fps=[^\n]+ frames=(\d+)', log)
    checksum = re.search(r'simulation_checksum=(\d+)', log)
    if not camera or abs(float(camera[1]) - zoom) > 1e-6:
        raise ValueError('effective camera zoom differs from the requested workload')
    if not ready or not completed or not checksum:
        raise ValueError('missing workload, completed frame count or checksum')
    if int(ready[10]) != frames or int(completed[1]) != frames:
        raise ValueError('requested and completed frame counts differ')
    return {'camera': list(camera.groups()), 'ready': list(ready.groups()),
            'frames': int(completed[1]), 'checksum': checksum[1]}


def live_observation(log):
    progress = re.search(r'interactive_progress initial_tick=(\d+) final_tick=(\d+)', log)
    frames = re.search(r'interactive frames=(\d+) opportunities=(\d+) '
                       r'undrawn_opportunities=(\d+) missed_render_deadlines=(\d+) period_ns=(\d+)', log)
    if not progress or not frames or int(progress[2]) <= int(progress[1]) or not int(frames[1]):
        raise ValueError('live simulation must advance and display frames')
    distributions = {}
    for label in ('draw_and_present', 'displayed_scene_age', 'last_simulation_deadline_lateness'):
        values = re.search(rf'{label} mean_ms=([0-9.]+) median_ms=([0-9.]+) '
                           r'p95_ms=([0-9.]+) p99_ms=([0-9.]+)', log)
        if not values:
            raise ValueError(f'live run omitted {label} latency')
        distributions[label] = dict(zip(('mean_ms', 'median_ms', 'p95_ms', 'p99_ms'),
                                       map(float, values.groups())))
    return {'initial_tick': int(progress[1]), 'final_tick': int(progress[2]),
            'frames': int(frames[1]), 'opportunities': int(frames[2]),
            'undrawn_opportunities': int(frames[3]), 'missed_render_deadlines': int(frames[4]),
            'period_ns': int(frames[5]), 'latency': distributions, 'kind': 'live_progress_and_latency'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--save', type=Path, required=True)
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeat', type=int, default=12)
    parser.add_argument('--frames', type=int, default=240)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--case', choices=CASES, action='append')
    parser.add_argument('--inside-cpuset', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.repeat < 1 or args.frames < 1 or args.warmup < 0:
        parser.error('repeat/frames must be positive and warmup nonnegative')
    interposition = [key for key in os.environ if
        key in ('LD_PRELOAD', 'LD_AUDIT', 'DYLD_INSERT_LIBRARIES') or
        key.startswith('GLOB2_PROFILE_')]
    if interposition:
        parser.error('timing comparisons reject profiler/interposition environment: ' + ', '.join(interposition))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.inside_cpuset:
        # Reserve complete physical cores; leave two cores for ordinary work.
        return subprocess.call([
            sys.executable, str(Path(__file__).with_name('run_with_benchmark_cpuset.py')),
            '--audit', str(args.output / 'cpuset-audit.json'),
            '--cpus', '0,2,4,6,8,10', '--reserve-cpus', '0-11', '--',
            sys.executable, str(Path(__file__).resolve()), *sys.argv[1:], '--inside-cpuset',
        ])
    variants = [('baseline-immediate', args.baseline, 'immediate'),
                ('baseline-parallel', args.baseline, 'parallel'),
                ('candidate-immediate', args.candidate, 'immediate'),
                ('candidate-serial', args.candidate, 'serial'),
                ('candidate-parallel', args.candidate, 'parallel')]
    manifest = {'arguments': {k: str(v) for k, v in vars(args).items()},
                'sha256': {str(p.resolve()): digest(p) for p in (args.baseline, args.candidate, args.save)},
                'affinity': sorted(os.sched_getaffinity(0)), 'runs': []}
    fixed_checksums = set()
    observations = {}
    candidate_captures = {}
    for case in args.case or CASES:
        for repeat in range(args.repeat):
            order = variants[repeat % len(variants):] + variants[:repeat % len(variants)]
            if repeat % 2:
                order = list(reversed(order))
            for name, binary, mode in order:
                label = f'{case}-{repeat:02d}-{name}'
                with tempfile.TemporaryDirectory(prefix='glob2-render-comparison-') as profile:
                    env = {k: v for k, v in os.environ.items() if not k.startswith('PROFILE_')}
                    env.update(PROFILE_SAVE=str(args.save.resolve()), PROFILE_FRAMES=str(args.frames),
                               PROFILE_WARMUP=str(args.warmup), PROFILE_CLOUDS='0',
                               PROFILE_COMPUTE_TELEMETRY='off', GLOB2_UI_SCALE='1',
                               GLOB2_USER_DATA_DIR=profile, GLOB2_RENDER_EXECUTION=mode,
                               GLOB2_RENDER_WORKERS='2', GLOB2_PERF_DISABLE='1')
                    env.update(CASES[case])
                    if case == 'live-simulation':
                        del env['GLOB2_PERF_DISABLE']  # Display admission is counted by telemetry.
                    elif repeat == 0:
                        env['PROFILE_CAPTURE'] = str(args.output / f'{label}.bmp')
                    if args.assets:
                        env['GLOB2_ASSET_DIR'] = str(args.assets.resolve())
                    command = [str(binary.resolve()), '--renderer', 'software', '--window-size',
                               '1280x800', '--mute', '--no-fullscreen', '--compute-threads', '5']
                    result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, text=True, timeout=300)
                (args.output / f'{label}.log').write_text(result.stdout)
                entry = {'label': label, 'command': command,
                    'environment': {k: v for k, v in env.items() if k.startswith(('PROFILE_', 'GLOB2_'))},
                    'returncode': result.returncode, 'validated': False}
                manifest['runs'].append(entry)
                manifest_path = args.output / 'manifest.json'
                manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
                if result.returncode:
                    raise RuntimeError(f'{label} failed; inspect its log')
                if 'timing_valid=false' in result.stdout:
                    raise RuntimeError(f'{label} contains profiling instrumentation; discard timing')
                try:
                    if case == 'live-simulation':
                        entry['observation'] = live_observation(result.stdout)
                    else:
                        observation = fixed_observation(result.stdout, args.frames,
                            float(CASES[case].get('PROFILE_ZOOM', '1')))
                        observation['kind'] = 'fixed_work'
                        if case in observations and observations[case] != observation:
                            raise ValueError('dimensions, camera, population or frame count changed between variants')
                        observations[case] = observation
                        entry['observation'] = observation
                        fixed_checksums.add(observation['checksum'])
                        if len(fixed_checksums) != 1:
                            raise ValueError('simulation checksum differs from the comparison fixture')
                        if repeat == 0:
                            capture = Path(env['PROFILE_CAPTURE'])
                            entry['capture_sha256'] = digest(capture)
                            if name.startswith('candidate-'):
                                expected = candidate_captures.setdefault(case, entry['capture_sha256'])
                                if expected != entry['capture_sha256']:
                                    raise ValueError('candidate execution modes produced different captured pixels')
                except ValueError as error:
                    entry['validation_error'] = str(error)
                    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
                    raise RuntimeError(f'{label}: {error}; discard comparison') from error
                entry['validated'] = True
                manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
                print(label, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
