import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/gradient-init'
BINARY = OUT / 'glob2-verify'
CASES = json.loads((ROOT / 'artifacts/lazy-gradient/cases.json').read_text())

def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()

records = []
env = os.environ.copy()
env['SDL_VIDEODRIVER'] = env['SDL_AUDIODRIVER'] = 'dummy'
for key in ['GLOB2_GRADIENT_DEMAND_CSV', 'GLOB2_LAZY_BUILDING_GRADIENTS']:
    env.pop(key, None)
for case in CASES:
    original = ROOT / 'artifacts/lazy-gradient/runs' / case['name']
    for mode in ['eager', 'lazy']:
        dest = OUT / 'validation' / case['name'] / mode
        dest.parent.mkdir(parents=True, exist_ok=True)
        assert not dest.exists(), dest
        command = [str(BINARY), '--run-game', '--map-file', str(original / 'generated/map-r0.map'),
                   '--game-seed', str(case['game_seed']), '--ticks', str(case['ticks']),
                   '--telemetry', 'checksums', '--telemetry', 'team-timeline', '--save', 'initial', '--save', 'final',
                   '--lazy-building-gradients', str(mode == 'lazy').lower(), '--output-dir', str(dest)]
        for ai in case['players']:
            command += ['--player', ai]
        print(f"Running {case['name']} {mode}", flush=True)
        with dest.with_suffix('.log').open('w') as stream:
            subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
        record = dict(case=case['name'], mode=mode, command=command, binary_sha256=digest(BINARY), files={})
        for name in ['game.replay.checksums', 'initial.game', 'final.game']:
            actual, expected = digest(dest / name), digest(original / 'base' / name)
            record['files'][name] = dict(sha256=actual, baseline_sha256=expected, identical=actual == expected)
            assert actual == expected, (case['name'], mode, name)
        records.append(record)
        (OUT / 'validation.json').write_text(json.dumps(records, indent=2) + '\n')
        print('PASS checksums and saves match original baseline', flush=True)
# Continue the original eager save, retaining its deliberately stale fields.
source = ROOT / 'artifacts/lazy-gradient/runs/arena512-4/base/final.game'
dest = OUT / 'continuation'
command = [str(BINARY), '--run-game', '--load-game', str(source), '--ticks', '17408',
           '--telemetry', 'checksums', '--save', 'final', '--lazy-building-gradients', 'true', '--output-dir', str(dest)]
with (OUT / 'continuation.log').open('w') as stream:
    subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
record = dict(command=command, binary_sha256=digest(BINARY), input_sha256=digest(source), files={})
for name in ['game.replay.checksums', 'final.game']:
    actual = digest(dest / name)
    expected = digest(ROOT / 'artifacts/lazy-gradient-prototype/continuation/base' / name)
    record['files'][name] = dict(sha256=actual, baseline_sha256=expected, identical=actual == expected)
    assert actual == expected, name
(OUT / 'continuation.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS old-save continuation', flush=True)
