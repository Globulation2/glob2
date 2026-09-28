#!/usr/bin/env python3
"""Re-run a retained game with a repository-built client and check exact outputs.

Run from the source checkout: python /path/to/evidence/reproduce.py --binary
build/darwin/client/release/src/glob2 --fixtures /path/to/unpacked/fixtures
--case arena128-2 --mode lazy --output artifacts/lazy-reproduction
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--fixtures', type=Path, required=True)
parser.add_argument('--case', required=True)
parser.add_argument('--mode', choices=['eager', 'lazy'], required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
case = args.fixtures.resolve() / args.case
settings = json.loads((case / 'case.json').read_text())
command = [str(args.binary.resolve()), '--run-game', '--map-file', str(case / 'map-r0.map'),
           '--game-seed', str(settings['game_seed']), '--ticks', str(settings['ticks']),
           '--telemetry', 'checksums', '--telemetry', 'team-timeline', '--save', 'initial', '--save', 'final',
           '--lazy-building-gradients', str(args.mode == 'lazy').lower(),
           '--output-dir', str(args.output.resolve())]
for player in settings['players']:
    command += ['--player', player]
env = os.environ.copy()
env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
subprocess.run(command, env=env, check=True)

def sha256(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()

for name in ['game.replay.checksums', 'initial.game', 'final.game']:
    assert sha256(args.output / name) == sha256(case / name), name
print('PASS: exact checksum trace and save bytes')
