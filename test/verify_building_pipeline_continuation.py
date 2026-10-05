#!/usr/bin/env python3
"""Compare every publication-delay save phase with uninterrupted detailed checksums."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
from benchmark_parallel_compute import execute, digest
from compare_save_continuation import records


def trace(path):
    return dict(records(path.read_bytes()))


def compress(path):
    expected = digest(path)
    temporary = path.with_name(path.name + '.gz.tmp')
    with path.open('rb') as src, gzip.open(temporary, 'wb', compresslevel=1) as dst:
        while data := src.read(2**20):
            dst.write(data)
    restored = hashlib.sha256()
    with gzip.open(temporary, 'rb') as stream:
        while data := stream.read(2**20):
            restored.update(data)
    if restored.hexdigest() != expected:
        raise RuntimeError(f'compressed evidence differs: {path}')
    temporary.replace(path.with_name(path.name + '.gz'))
    path.unlink()


def archive_outputs(directory):
    hashes = {}
    for name in ('game.replay', 'final.game', 'initial.game'):
        path = directory / name
        if path.exists():
            hashes[name] = digest(path)
            compress(path)
    return hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--checkpoint', required=True, type=Path)
    parser.add_argument('--start-tick', required=True, type=int)
    parser.add_argument('--ticks', type=int, default=64)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    binary, fixture, output = args.binary.resolve(), args.checkpoint.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    evidence = []
    for delay in (2, 4, 8):
        common = ['--ticks', str(args.start_tick + args.ticks), '--compute-experiments', 'hiring', '--compute-threads', '4', '--telemetry', 'checksums', '--replay', 'true']
        base = output / f'd{delay}-uninterrupted'
        row = execute(binary, ['--load-game', str(fixture), '--gradient-workers', '4', '--fork-rule', 'building-gradient-pipeline=1', '--fork-rule', f'buildingGradientDelay={delay}', '--save', 'every:1', *common], base)
        expected = trace(base / 'game.replay.checksums')
        for phase in range(1, delay + 1):
            checkpoint = base / f'checkpoint-{args.start_tick + phase}.game.gz'
            for workers in (0, 4):
                resumed = output / f'd{delay}-phase{phase}-w{workers}'
                result = execute(binary, ['--load-game', str(checkpoint), '--gradient-workers', str(workers), *common], resumed)
                actual = trace(resumed / 'game.replay.checksums')
                if not actual or any(expected.get(tick) != data for tick, data in actual.items()):
                    raise RuntimeError(f'save continuation mismatch: {resumed}')
                evidence.append({'delay': delay, 'phase': phase, 'workers': workers, 'compared_ticks': len(actual), 'checkpoint_sha256': digest(checkpoint), 'command': result['command'], 'output_hashes': archive_outputs(resumed)})
                compress(resumed / 'game.replay.checksums')
                print('PASS', delay, phase, workers, len(actual), flush=True)
        compress(base / 'game.replay.checksums')
        archive_outputs(base)
        # Retain each tested phase; other identical-format checkpoints are redundant.
        for path in base.glob('checkpoint-*.game.gz'):
            tick = int(path.name.split('-')[1].split('.')[0])
            if tick > args.start_tick + delay:
                path.unlink()
    (output / 'continuations.json').write_text(json.dumps({'binary_sha256': digest(binary), 'fixture_sha256': digest(fixture), 'checks': evidence}, indent=2) + '\n')


if __name__ == '__main__':
    main()
