import datetime
import json
import os
from pathlib import Path
import subprocess

base = Path(__file__).resolve().parent
receipt = json.loads((base / 'build-command.json').read_text())
environment = os.environ.copy()
environment.update(receipt['env'])
status = base / 'build-status.json'
receipt['started'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
receipt['supervisor_pid'] = os.getpid()
status.write_text(json.dumps(receipt, indent=2) + '\n')
with (base / 'build.log').open('w') as log:
    process = subprocess.Popen(['nice', '-n', str(receipt['nice']), *receipt['argv']],
                               cwd=receipt['cwd'], env=environment,
                               stdout=log, stderr=subprocess.STDOUT)
    receipt['build_pid'] = process.pid
    status.write_text(json.dumps(receipt, indent=2) + '\n')
    receipt['exit_code'] = process.wait()
receipt['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
status.write_text(json.dumps(receipt, indent=2) + '\n')
