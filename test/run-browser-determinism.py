#!/usr/bin/env python3
"""Retain a native trace for comparison with browser and other platform builds."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--engine-binary', type=Path, help='Also retain the custom-resource composition trace')
args = parser.parse_args()
binary = args.binary.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
fixture = root / 'games/cross-replay.game.gz'
replay = output / 'native.replay'
command = [str(binary), '--nox', str(fixture), '1500', '1']
with tempfile.TemporaryDirectory(prefix='glob2-browser-determinism-') as profile:
    with (output / 'run.log').open('w') as log:
        subprocess.run(command, cwd=root, check=True, timeout=120, stdout=log,
                       stderr=subprocess.STDOUT, env=dict(os.environ,
                       GLOB2_USER_DIR=profile, GLOB2_REPLAY_PATH=str(replay),
                       GLOB2_CHECKSUM_SIDECAR='1', SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy'))
trace = Path(str(replay) + '.checksums').read_bytes()
assert len(trace) > 1000, 'Missing or empty simulation trace'
metadata = {'fixture': 'games/cross-replay.game.gz', 'seed': 42, 'ticks': 1500,
            'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
            'trace_sha256': hashlib.sha256(trace).hexdigest(), 'command': command}

# The committed turn-protocol match record, verified headlessly. Its per-tick
# checksum trace must match every other platform's (and the browsers').
record = root / 'test/fixtures/multiplayer/FourSquares1.g2mr'
match_map = root / 'maps/FourSquares1.map.gz'
verify_dir = output / 'verify-match'
shutil.rmtree(verify_dir, ignore_errors=True)
verify_command = [str(binary), '--verify-match', str(record), '--map', str(match_map), '--out', str(verify_dir)]
with (output / 'verify-match.log').open('w') as log:
    subprocess.run(verify_command, cwd=root, check=True, timeout=300, stdout=log, stderr=subprocess.STDOUT,
                   env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
verify_trace = (verify_dir / 'checksums.txt').read_bytes()
assert verify_trace.count(b'\n') > 600, 'Missing or short verify-match trace'
(output / 'verify-match.checksums.txt').write_bytes(verify_trace)
verdict = json.loads((verify_dir / 'verdict.json').read_text())
# Keep the uploaded evidence small: the replay and scratch profile are reproducible.
(verify_dir / 'match.replay').unlink()
shutil.rmtree(verify_dir / 'profile', ignore_errors=True)
metadata['verify_match'] = {'record': 'test/fixtures/multiplayer/FourSquares1.g2mr',
                            'record_sha256': hashlib.sha256(record.read_bytes()).hexdigest(),
                            'trace_sha256': hashlib.sha256(verify_trace).hexdigest(),
                            'verdict': verdict['verdict'], 'command': verify_command}
(output / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(metadata['trace_sha256'])

if args.engine_binary:
    # Keep the native custom-catalog continuation alongside the stock traces.
    # A fresh directory prevents an interrupted run from reusing stale evidence.
    resources = output / 'resources' / 'native'
    resources.mkdir(parents=True, exist_ok=False)
    engine = args.engine_binary.resolve()
    resource_command = [str(engine), '--test-suite=RuntimeResources',
                        '--test-case=frozen seeded resource compositions*',
                        '--reporters=junit', '--out=' + str(resources / 'tests.xml')]
    with tempfile.TemporaryDirectory(prefix='glob2-resource-determinism-') as profile:
        env = dict(os.environ, GLOB2_TEST_SOURCE_ROOT=str(root),
                   GLOB2_TEST_ARTIFACTS_ROOT=str(resources / 'cases'),
                   GLOB2_USER_DATA_DIR=profile, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        for key in ('GLOB2_TEST_ARTIFACTS', 'GLOB2_TEST_UPDATE_FIXTURES'):
            env.pop(key, None)
        with (resources / 'run.log').open('w') as log:
            subprocess.run(resource_command, cwd=root, check=True, timeout=300,
                           stdout=log, stderr=subprocess.STDOUT, env=env)
    traces = list(resources.rglob('seeded-compositions.trace'))
    if len(traces) != 1:
        raise RuntimeError(f'Expected one resource composition trace, got {traces}')
    trace = traces[0].read_bytes().replace(b'\r\n', b'\n')
    committed = root / 'test/fixtures/resources/seeded-compositions.trace'
    if len(trace.splitlines()) != 150 or trace != committed.read_bytes().replace(b'\r\n', b'\n'):
        raise RuntimeError('Incomplete or changed resource composition trace')
    producer = json.loads((resources / 'cases/build-provenance.json').read_text())
    (resources / 'manifest.json').write_text(json.dumps({
        'producer': producer, 'command': resource_command,
        'trace_sha256': hashlib.sha256(trace).hexdigest(),
        'fixture_sha256': hashlib.sha256(committed.read_bytes()).hexdigest(),
    }, indent=2) + '\n')
