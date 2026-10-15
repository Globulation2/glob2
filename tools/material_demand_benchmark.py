#!/usr/bin/env python3
"""Check building-demand measurement fixtures; opt in to timing with --measure.

Pass engine-test binaries from the staged commits to compare the same public
production callers. Optional game binaries also verify a deterministic saved
eight-Nicowar Playground continuation against a baseline and a save/load split.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CASES = {'full_idle', 'sparse_depleted', 'dense_authored', 'capacity_limited',
         'construction', 'repair', 'shared', 'shared_dense', 'shared_construction', 'shared_repair',
         'workers_4', 'workers_12', 'mixed_population'}
OPERATIONS = {'single_reference', 'single_current', 'dense_reference', 'dense_current_queries',
              'wishes', 'staffing', 'call_lists', 'step', 'update'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixture_records(output, *, measure):
    rows = [json.loads(line) for line in output.splitlines()
            if line.startswith('{"fixture":"building-material-demand"')]
    repetitions = 7 if measure else 1
    operations = OPERATIONS | ({'summary'} if any(r['operation'] == 'summary' for r in rows) else set())
    keys = {(r['case'], r['operation'], r['repeat']) for r in rows}
    expected = {(case, op, repeat) for case in CASES for op in operations for repeat in range(repetitions)}
    if len(rows) != len(expected) or keys != expected:
        raise ValueError('Missing or duplicate demand benchmark cases')
    for row in rows:
        if type(row['iterations']) is not int or row['iterations'] <= 0:
            raise ValueError('Fixture iterations must be positive')
        duration = row['elapsed_ns']
        if (measure and (type(duration) is not int or duration < 0)) or (not measure and duration is not None):
            raise ValueError('Timing mode does not match the requested fixture mode')
    for case in CASES:
        for repeat in range(repetitions):
            digests = {r['operation']: r['digest'] for r in rows if r['case'] == case and r['repeat'] == repeat}
            for reference, current in [('single_reference', 'single_current'), ('dense_reference', 'dense_current_queries')]:
                if digests[reference] != digests[current]:
                    raise ValueError(f'{case}: reference and production query differ')
    return rows


def run_tests(binary, directory, *, measure):
    directory.mkdir(parents=True)
    env = dict(os.environ, GLOB2_TEST_ARTIFACTS_ROOT=str(directory.resolve()))
    if measure:
        env.pop('GLOB2_DEMAND_CHECK_ONLY', None)
    else:
        env['GLOB2_DEMAND_CHECK_ONLY'] = '1'
    # Listing cases emits build provenance without entering a measured fixture.
    listed = subprocess.run([str(binary), '-ts=BuildingMaterialDemandBenchmark', '-ltc'],
                            cwd=ROOT, env=env, text=True, capture_output=True)
    listed.check_returncode()
    provenance = json.loads((directory / 'build-provenance.json').read_text())
    flags = shlex.split(' '.join(provenance.get('flags', {}).get(k, '') for k in ('CCFLAGS', 'CXXFLAGS')))
    if measure and ('-O3' not in flags or '-O0' in flags):
        raise ValueError('--measure requires a release -O3 engine-test binary')
    command = [str(binary), '-ts=BuildingMaterialDemandBenchmark', '-tc=*', '-no-colors']
    result = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True)
    (directory / 'stdout.log').write_text(result.stdout)
    (directory / 'stderr.log').write_text(result.stderr)
    result.check_returncode()
    rows = fixture_records(result.stdout, measure=measure)
    proof = {'command': command, 'binary_sha256': sha(binary), 'binary_bytes': binary.stat().st_size,
             'provenance': provenance, 'samples': rows}
    if measure:
        proof['timing_summary'] = []
        for case, operation in sorted({(r['case'], r['operation']) for r in rows}):
            samples = [r['elapsed_ns'] / r['iterations'] for r in rows
                       if r['case'] == case and r['operation'] == operation]
            proof['timing_summary'].append({'case': case, 'operation': operation,
                                            'median_ns_per_operation': statistics.median(samples),
                                            'min_ns_per_operation': min(samples), 'max_ns_per_operation': max(samples)})
        if size := shutil.which('size'):
            sections = subprocess.run([size, str(binary)], capture_output=True, text=True, check=True)
            proof['section_sizes'] = sections.stdout
    return proof


def game_run(binary, args, directory):
    command = [str(binary), 'game', 'run', *args, '--compute-threads', '8', '--gpu', 'off',
               '--output-dir', str(directory)]
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'command.json').write_text(json.dumps(command, indent=2))
    (directory / 'stdout.log').write_text(result.stdout)
    (directory / 'stderr.log').write_text(result.stderr)
    result.check_returncode()
    return json.loads((directory / 'result.json').read_text())


def compare_traces(left, right, *, tail_from=None):
    for name in ('world.checksums', 'game.replay.checksums'):
        if tail_from is not None and name != 'world.checksums':
            continue
        a, b = left / name, right / name
        if not a.exists() or not b.exists():
            raise ValueError(f'Missing per-tick trace: {name}')
        if name != 'world.checksums':
            if not a.read_bytes() or a.read_bytes() != b.read_bytes():
                raise ValueError(f'Continuation diverged: {name}')
            continue
        first, second = a.read_text().splitlines(), b.read_text().splitlines()
        if tail_from is not None:
            first = [line for line in first if int(line.split()[0]) >= tail_from]
        if first != second or not second:
            raise ValueError(f'Continuation diverged: {name}')


def simulation_fixture(args, output):
    binary = args.game_binary.resolve()
    if args.save:
        save = args.save.resolve()
    else:
        seed_args = ['--map-file', str(ROOT / 'maps/Playground.map.gz'), '--game-seed', '713',
                     '--ticks', str(args.start_tick), '--save', 'final']
        seed_args += ['--player', 'nicowar'] * 8
        game_run(binary, seed_args, output / 'initial')
        save = output / 'initial/final.game.gz'
    common = ['--load-game', str(save), '--ticks', str(args.ticks), '--save', 'final',
              '--telemetry', 'checksums', '--write-replay']
    if args.measure:
        common += ['--benchmark-warmup', '128']
    current = game_run(binary, common, output / 'candidate')
    proof = {'game_launcher_sha256': sha(binary), 'save_sha256': sha(save), 'ticks': current['ticks'],
             'final_checksum': current['finalChecksum'], 'baseline_equal': None}
    if args.baseline_game:
        base = game_run(args.baseline_game.resolve(), common, output / 'baseline')
        compare_traces(output / 'baseline', output / 'candidate')
        if base['finalChecksum'] != current['finalChecksum']:
            raise ValueError('Final baseline checksum differs')
        # Same saved header and same orders must produce identical replay bytes.
        if sha(output / 'baseline/game.replay') != sha(output / 'candidate/game.replay'):
            raise ValueError('Baseline replay orders differ')
        proof['baseline_equal'] = True
        proof['baseline_launcher_sha256'] = sha(args.baseline_game.resolve())
    # Recording canonicalizes historical save headers before checksumming, just
    # as it does in the candidate/baseline runs above. Keep both split branches equal.
    full = game_run(binary, ['--load-game', str(save), '--ticks', str(args.ticks + 128),
                            '--telemetry', 'checksums', '--write-replay'], output / 'continuous')
    split = game_run(binary, ['--load-game', str(output / 'candidate/final.game.gz'),
                             '--ticks', str(args.ticks + 128), '--telemetry', 'checksums', '--write-replay'], output / 'resumed')
    compare_traces(output / 'continuous', output / 'resumed', tail_from=args.ticks)
    if full['finalChecksum'] != split['finalChecksum']:
        raise ValueError('Save/load continuation checksum differs')
    proof.update(save_load_equal=True, continued_checksum=split['finalChecksum'],
                 world_trace_sha256=sha(output / 'candidate/world.checksums'))
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-tests', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--measure', action='store_true', help='explicitly enable timing on an isolated machine')
    parser.add_argument('--game-binary', type=Path)
    parser.add_argument('--baseline-game', type=Path)
    parser.add_argument('--save', type=Path)
    parser.add_argument('--start-tick', type=int, default=24000)
    parser.add_argument('--ticks', type=int, default=25153)
    args = parser.parse_args()
    if (args.baseline_game or args.save) and not args.game_binary:
        parser.error('--baseline-game/--save require --game-binary')
    if args.ticks <= args.start_tick or args.start_tick < 1:
        parser.error('--ticks must exceed --start-tick > 0')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    proof = {'schema': 1, 'timing_enabled': args.measure,
             'calculation_fixture': run_tests(args.engine_tests.resolve(), output / 'calculations', measure=args.measure)}
    if args.game_binary:
        proof['simulation_fixture'] = simulation_fixture(args, output)
    (output / 'fixture.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(f'Fixture validated; timing={args.measure}; evidence={output}')


if __name__ == '__main__':
    main()
