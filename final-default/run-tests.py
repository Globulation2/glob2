import json
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/lazy-gradient-final'
BIN = ROOT / 'build/darwin/client/release/src'
records = []
with tempfile.TemporaryDirectory(prefix='glob2-lazy-tests-') as profile:
    for mode in ['default']:
        env = os.environ.copy()
        env.pop('GLOB2_LAZY_BUILDING_GRADIENTS', None)
        env.update(GLOB2_USER_DIR=profile,
                   SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        for name in ['PathGradientHarness', 'BuildingGradientInvalidationHarness', 'ImmobileUnitGradientHarness', 'SavegameSafetyHarness', 'ClearingFlagGradientTest']:
            command = [str(BIN / name)]
            if name in ['SavegameSafetyHarness', 'ClearingFlagGradientTest']:
                command = ['python3', 'test/run-savegame-safety-tests.py', str(BIN / name)]
                env.pop('GLOB2_USER_DIR', None)
            log = OUT / f'{name}-{mode}.log'
            print(f'Running {name} {mode}', flush=True)
            with log.open('w') as stream:
                subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
            records.append(dict(name=name, mode=mode, command=command, passed=True))
            (OUT / 'tests.json').write_text(json.dumps(records, indent=2)+'\n')
            print('PASS', flush=True)
