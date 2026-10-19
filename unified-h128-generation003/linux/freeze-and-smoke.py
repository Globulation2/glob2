import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

base = Path(__file__).resolve().parent
root = Path('/home/bradley/glob2-gradient-unified-h128')
sha = lambda p: hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()
while True:
    build = json.loads((base / 'build-status.json').read_text())
    if 'exit_code' in build:
        break
    time.sleep(5)
assert build['exit_code'] == 0
parity = json.loads((base / 'PARITY.json').read_text())
assert parity['cpp_python_predictions_equal'] and parity['integer_accumulator_bounds'] and parity['ubsan']
os.sched_setaffinity(0, {1,2,4,5,8,10,12,14})
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip() == '4a2cf3dac9eb4eab0aab932307ca5096be953491'
assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()
frozen = base / 'frozen'
frozen.mkdir(exist_ok=True)
paths = {'glob2': root / 'build/unified-release/src/glob2',
         'adaptive-gradient.json': root / 'tools/models/adaptive-gradient.json',
         'AdaptiveGradientModel.h': root / 'src/map/gradient/AdaptiveGradientModel.h'}
for name, path in paths.items():
    shutil.copy2(path, frozen / name)
    assert sha(path) == sha(frozen / name)
receipt = dict(source_revision=build['source_revision'], host='devlaptop.local',
               files={name: dict(path=str(frozen / name), sha256=sha(frozen / name)) for name in paths},
               ordinary_release=True, model_trained=True, research_candidate_only=True,
               created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
(base / 'FROZEN.json').write_text(json.dumps(receipt, indent=2) + '\n')
runtime = base.parent / 'runtime-linux'
with (runtime / 'supervisor.log').open('w') as log:
    result = subprocess.run(['nice', '-n', '19', 'python3', str(runtime / 'qualify.py')], cwd=root,
                            stdout=log, stderr=subprocess.STDOUT)
assert result.returncode == 0, 'Candidate smoke failed; inspect runtime-linux/supervisor.log'
