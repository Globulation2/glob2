#!/usr/bin/env python3
"""Retain a native trace for comparison with browser and other platform builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
binary = args.binary.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
fixture = root / 'games/cross-replay.game'
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
metadata = {'fixture': 'games/cross-replay.game', 'seed': 42, 'ticks': 1500,
            'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
            'trace_sha256': hashlib.sha256(trace).hexdigest(), 'command': command}
(output / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(metadata['trace_sha256'])
