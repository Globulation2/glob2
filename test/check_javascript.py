"""Frozen JavaScript profile trace, worker equivalence and saved continuation."""
import argparse
import gzip
import subprocess
from pathlib import Path
from check_telemetry_simulation import detailed_ticks

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'test/fixtures/javascript'


def run(binary, output, saved, workers, checkpoint=False):
    output.mkdir(parents=True, exist_ok=False)
    command = [str(binary), '--run-game', '--load-game', str(saved), '--ticks', '256',
               '--compute-threads', str(workers), '--telemetry', 'checksums',
               '--replay', 'true', '--save', 'final', '--output-dir', str(output)]
    if checkpoint:
        command += ['--save', 'every:128']
    with (output / 'engine.log').open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    return detailed_ticks((output / 'game.replay.checksums').read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    expected = detailed_ticks(gzip.decompress((FIXTURE / 'profile1-256.checksums.gz').read_bytes()))
    initial = FIXTURE / 'profile1-initial.game.gz'
    baseline = output / 'workers1'
    assert run(binary, baseline, initial, 1, True) == expected
    parallel = output / 'workers4'
    assert run(binary, parallel, initial, 4, True) == expected
    for name in ('game.replay', 'game.replay.checksums', 'final.game.gz'):
        assert (baseline / name).read_bytes() == (parallel / name).read_bytes(), name
    tail = output / 'resumed'
    ticks = run(binary, tail, baseline / 'checkpoint-128.game.gz', 4)
    assert len(ticks) == 128 and all(expected[t] == v for t, v in ticks.items())
    # The MapHeader SHA1 depends on prior saves; all other bytes must match.
    whole = gzip.decompress((baseline / 'final.game.gz').read_bytes())
    resumed = gzip.decompress((tail / 'final.game.gz').read_bytes())
    assert whole[:35] == resumed[:35] and whole[55:] == resumed[55:]
    print('PASS JavaScript profile 1: 256 frozen tick checksums, exact 1/4-worker '
          'replays/saves, and 128 resumed ticks with identical saved state')


if __name__ == '__main__':
    main()
