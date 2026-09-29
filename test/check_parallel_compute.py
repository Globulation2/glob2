"""Real-engine thread-count equivalence and save continuation (CI + local)."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from benchmark_parallel_compute import execute, digest
from check_telemetry_simulation import detailed_ticks

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    baseline = args.baseline.resolve() if args.baseline else binary
    temporary = tempfile.TemporaryDirectory(prefix='glob2-compute-') if not args.output else None
    output = Path(temporary.name) if temporary else args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / 'fixture'
    execute(baseline, ['--generator', '15', '--map-seed', '4242', '--param', 'width=7', '--param', 'height=7', '--param', 'teams=4', '--game-seed', '19', '--player', 'maxima', '--player', 'cortex', '--player', 'nicowar', '--player', 'maxima', '--ticks', '1', '--save', 'initial'], fixture)
    initial = str(fixture / 'initial.game')
    common = ['--load-game', initial, '--ticks', '1024', '--telemetry', 'checksums', '--replay', 'true', '--save', 'final']
    references = {}
    for label, exe, extra in [('baseline', baseline, []), *[(f'{experiment}-{n}', binary, ['--compute-threads', str(n), '--compute-experiments', experiment]) for experiment in ('none', 'areas', 'initialize', 'hiring', 'ai', 'all') for n in (1, 2, 4, 8)]]:
        run = output / label
        execute(exe, common + extra, run)
        hashes = {name: digest(run / name) for name in ('game.replay.checksums', 'game.replay', 'final.game')}
        if not references: references = hashes
        assert hashes == references, (label, hashes, references)
    # Same checkpoint in all executions, compared with uninterrupted execution.
    tail_args = ['--load-game', str(output / 'baseline/final.game'), '--ticks', '1280', '--telemetry', 'checksums', '--save', 'final']
    full_args = ['--load-game', initial, '--ticks', '1280', '--telemetry', 'checksums', '--save', 'final']
    full = output / 'uninterrupted'
    execute(baseline, full_args, full)
    full_ticks = detailed_ticks((full / 'game.replay.checksums').read_bytes())
    resumed = output / 'baseline-continuation'
    execute(baseline, tail_args, resumed)
    for n in (1, 2, 4, 8):
        run = output / f'continuation-{n}'
        execute(binary, tail_args + ['--compute-threads', str(n), '--compute-experiments', 'all'], run)
        ticks = detailed_ticks((run / 'game.replay.checksums').read_bytes())
        assert ticks and all(full_ticks[tick] == value for tick, value in ticks.items()), f'continuation differs: {n}'
        assert digest(run / 'final.game') == digest(resumed / 'final.game'), f'final continuation save differs from serial reload: {n}'
    print('PASS compute experiments: exact traces, replay bytes, final saves, and uninterrupted continuation at 1/2/4/8 threads')


if __name__ == '__main__':
    main()
