#!/usr/bin/env python3
"""Build independent historical snapshots concurrently, without running games."""
from concurrent.futures import ThreadPoolExecutor,as_completed
import json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent
snapshots=json.loads((ROOT/'experiment.json').read_text())['snapshots']
# The original runner is paused while its September 5 build completes.
priority=[snapshots[-1],*snapshots[2:-1]]
def build(snap):
 date=snap['sample_date']
 with (ROOT/('prebuild-'+date+'.log')).open('w') as log:
  result=subprocess.call(['python3',str(ROOT/'run_experiment.py'),'--build-only','--only-date',date],stdout=log,stderr=subprocess.STDOUT)
 print(date,'OK' if result==0 else 'FAILED',flush=True)
 return result
with ThreadPoolExecutor(max_workers=3) as pool:
 futures=[pool.submit(build,snap) for snap in priority]
 failures=sum(f.result()!=0 for f in as_completed(futures))
raise SystemExit(1 if failures else 0)
