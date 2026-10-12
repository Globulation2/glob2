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
    execute(baseline, ['--generator', '15', '--map-seed', '4242', '--set', 'width=7', '--set', 'height=7', '--set', 'teams=4', '--game-seed', '19', '--player', 'maxima', '--player', 'cortex', '--player', 'nicowar', '--player', 'maxima', '--ticks', '1', '--save', 'initial'], fixture)
    initial = str(fixture / 'initial.game')
    common = ['--load-game', initial, '--ticks', '1024', '--telemetry', 'checksums', '--write-replay', '--save', 'final']
    references = {}
    counts = (1, 2, 4, 8, 'auto')
    for label, exe, extra in [('baseline', baseline, ['--compute-threads', '1']),
                              ('default', binary, []),
                              *[(f'compute-{n}', binary, ['--compute-threads', str(n)]) for n in counts]]:
        run = output / label
        execution = execute(exe, common + extra, run)
        if label == 'default':
            report = execution['result']
            assert report['compute_requested_threads'] == 'auto'
            assert report['compute_threads'] in (1, report['compute_resolved_threads'])
            assert report['compute_workers'] == report['compute_threads'] - 1
            assert 'compute_experiments' not in report
        hashes = {name: digest(run / name) for name in ('game.replay.checksums', 'game.replay', 'final.game')}
        if not references: references = hashes
        assert hashes == references, (label, hashes, references)
    # Same checkpoint in all executions, compared with uninterrupted execution.
    tail_args = ['--load-game', str(output / 'baseline/final.game'), '--ticks', '1280', '--telemetry', 'checksums', '--save', 'final']
    full_args = ['--load-game', initial, '--ticks', '1280', '--telemetry', 'checksums', '--save', 'final']
    full = output / 'uninterrupted'
    execute(baseline, full_args + ['--compute-threads', '1'], full)
    full_ticks = detailed_ticks((full / 'game.replay.checksums').read_bytes())
    resumed = output / 'baseline-continuation'
    execute(baseline, tail_args + ['--compute-threads', '1'], resumed)
    for n in counts:
        run = output / f'continuation-{n}'
        execute(binary, tail_args + ['--compute-threads', str(n)], run)
        ticks = detailed_ticks((run / 'game.replay.checksums').read_bytes())
        assert ticks and all(full_ticks[tick] == value for tick, value in ticks.items()), f'continuation differs: {n}'
        assert digest(run / 'final.game') == digest(resumed / 'final.game'), f'final continuation save differs from serial reload: {n}'
    # Version 121 stored one gradient manager for every Econo/Nicowar
    # controller. Loading it must split that state without leaving aliases.
    legacy = ROOT / 'test/fixtures/echo/v121-shared-gradient-256.game.gz'
    legacy_args = ['--load-game', str(legacy), '--ticks', '512',
                   '--telemetry', 'checksums', '--save', 'final']
    legacy_reference = None
    for n in counts:
        run = output / f'echo-v121-{n}'
        execute(binary, legacy_args + ['--compute-threads', str(n)], run)
        hashes = {name: digest(run / name) for name in ('game.replay.checksums', 'final.game')}
        if legacy_reference is None: legacy_reference = hashes
        assert hashes == legacy_reference, f'v121 shared-runtime continuation differs: {n}'
    for label, players in (('runtime-new', ('econo', 'nicowar', 'econo', 'nicowar')),
                           ('castor-new', ('castor',) * 4),
                           ('numbi-new', ('numbi',) * 4)):
        fixture = output / f'{label}-fixture'
        setup = ['--map-file', str(ROOT / 'maps/FourSquares1.map.gz'),
                 '--game-seed', '123']
        for ai in players: setup += ['--player', ai]
        execute(binary, setup + ['--ticks', '1', '--save', 'initial'], fixture)
        common = ['--load-game', str(fixture / 'initial.game.gz'),
                  '--ticks', '1024', '--telemetry', 'checksums', '--write-replay',
                  '--save', 'final']
        reference = None
        for n in counts:
            run = output / f'{label}-{n}'
            execute(binary, common + ['--compute-threads', str(n)], run)
            hashes = {name: digest(run / name) for name in
                      ('game.replay.checksums', 'game.replay', 'final.game')}
            if reference is None: reference = hashes
            assert hashes == reference, f'{label} game differs: {n}'
    print('PASS shared compute: exact traces, replay bytes, final saves, and runtime/Castor/Numbi continuation at 1/2/4/8/auto threads')


if __name__ == '__main__':
    main()
