"""JavaScript execution: complete traces, worker equivalence and multiple saved continuations.

The serial run is the reference: four compute workers must reproduce its replay,
checksums and final save, and every saved continuation its remaining ticks. No
committed trace is compared; tools/sim_fingerprint.py and the cross-platform
scripting evidence cover simulation changes.
"""
import argparse
import gzip
import hashlib
import json
import subprocess
import struct
from pathlib import Path
from check_telemetry_simulation import complete_ticks

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'test/fixtures/javascript'


def run(binary, output, saved, workers, interval=None):
    output.mkdir(parents=True, exist_ok=False)
    command = [str(binary), 'game', 'run', '--load-game', str(saved), '--ticks', '256',
               '--compute-threads', str(workers), '--telemetry', 'checksums',
               '--write-replay', '--save', 'final', '--output-dir', str(output)]
    if interval:
        command += ['--save', 'every:' + str(interval)]
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (output / 'engine.log').open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    return (output / 'game.replay.checksums').read_bytes()


def compare_payloads(whole, resumed):
    whole, resumed = gzip.decompress(whole), gzip.decompress(resumed)
    # MapHeader SHA1 is overwritten on each save and depends on save history.
    # Find it using the header's encoded map-name length rather than a fixed
    # offset: realistic and original fixtures have different names.
    name_length = struct.unpack_from('>I', whole, 0)[0]
    resumed_length = struct.unpack_from('>I', resumed, 0)[0]
    assert name_length == resumed_length
    sha1 = 4 + name_length + 4 * 4 + 1  # name, major/minor, team count, map offset, saved
    assert whole[:sha1] == resumed[:sha1] and whole[sha1 + 20:] == resumed[sha1 + 20:]


def continuation_matches(ticks, records, boundary):
    """Every resumed tick reproduces the uninterrupted record, aggregate included."""
    if set(records) != set(range(boundary, 256)):
        return False
    return all(records[tick] == ticks[tick] for tick in records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', choices=('profile1', 'realistic-profile1', 'all'), default='all')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'fixtures': {},
                'checks': [], 'excludedSaveMetadata': 'MapHeader SHA1 only (depends on save history)'}
    names = ('profile1', 'realistic-profile1') if args.fixture == 'all' else (args.fixture,)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    for name in names:
        initial = FIXTURE / (name + '-initial159.game.gz')
        interval = 32 if name == 'realistic-profile1' else 128
        baseline = output / name / 'workers1'
        actual = run(binary, baseline, initial, 1, interval)
        ticks = complete_ticks(actual)
        assert set(ticks) == set(range(256)), name + ': incomplete trace'
        manifest['fixtures'][name] = {'initialSha256': hashlib.sha256(initial.read_bytes()).hexdigest(),
                                     'traceSha256': hashlib.sha256(actual).hexdigest()}
        parallel = output / name / 'workers4'
        assert run(binary, parallel, initial, 4, interval) == actual, name + ': four workers differ from one'
        for filename in ('game.replay', 'game.replay.checksums', 'final.game.gz'):
            assert (baseline / filename).read_bytes() == (parallel / filename).read_bytes(), filename
        # Include either side of the 32-tick scenario callback/AI cadence, using
        # several saved boundaries rather than only the halfway checkpoint.
        boundaries = (32, 64, 96, 128, 224) if name == 'realistic-profile1' else (128,)
        for boundary in boundaries:
            tail = output / name / ('resumed-' + str(boundary))
            resumed = run(binary, tail, baseline / f'checkpoint-{boundary}.game.gz', 4)
            records = complete_ticks(resumed)
            assert continuation_matches(ticks, records, boundary), \
                f'{name}: continuation differs at boundary {boundary}'
            compare_payloads((baseline / 'final.game.gz').read_bytes(), (tail / 'final.game.gz').read_bytes())
        manifest['checks'].append({'fixture': name, 'ticks': 256, 'workers': [1, 4], 'resumeBoundaries': list(boundaries)})
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'PASS {name}: complete tick checksums, exact 1/4-worker replay/save output, '
              f'{len(boundaries)} saved continuations', flush=True)


if __name__ == '__main__':
    main()
