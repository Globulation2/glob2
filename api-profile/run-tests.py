import json
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/lazy-gradient-api'
BIN = ROOT / 'build/darwin/client/release/src'
records = []
with tempfile.TemporaryDirectory(prefix='glob2-lazy-tests-') as profile:
    for mode in ['eager', 'lazy']:
        env = os.environ.copy()
        env.update(GLOB2_LAZY_BUILDING_GRADIENTS=str(int(mode=='lazy')), GLOB2_USER_DIR=profile,
                   SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        for name in ['PathGradientHarness', 'BuildingGradientInvalidationHarness', 'ImmobileUnitGradientHarness', 'SavegameSafetyHarness']:
            if name == 'PathGradientHarness' and mode == 'lazy':
                continue  # The oracle directly exercises both kernels in one run.
            command = [str(BIN / name)]
            if name == 'SavegameSafetyHarness':
                command = ['python3', 'test/run-savegame-safety-tests.py', str(BIN / name)]
                env.pop('GLOB2_USER_DIR', None)
            log = OUT / f'{name}-{mode}.log'
            print(f'Running {name} {mode}', flush=True)
            with log.open('w') as stream:
                subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
            records.append(dict(name=name, mode=mode, command=command, passed=True))
            (OUT / 'tests.json').write_text(json.dumps(records, indent=2)+'\n')
            print('PASS', flush=True)
