#!/usr/bin/env python3
"""Scoped operations using the frozen supervisor/archive; no policy edits."""
import argparse,fcntl,json,runpy,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=['status','drain','stop-idle']);a=p.parse_args();root=Path(__file__).resolve().parent.parent;helper=runpy.run_path(str(root/'scripts/supervise.py'))['HELPER']
def call(action):
 r=subprocess.run([sys.executable,'-c',helper,str(root/'worker.pyz'),str(root),action],capture_output=True,text=True,timeout=210)
 if r.returncode:raise RuntimeError(r.stderr)
 return json.loads(r.stdout)
if a.action=='status':print(json.dumps(call('status'),indent=2))
elif a.action=='drain':print(json.dumps(call('drain'),indent=2))
else:
 handles=[]
 for filename in ['supervisor.lock','coordinator.lock']:
  handle=(root/filename).open('a+b')
  try:fcntl.flock(handle,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:raise SystemExit(f'{filename} held; leave the active controller to drain')
  handles.append(handle)
 drained=call('drain');assert drained['coordinator_mode']=='draining'and drained['worker']['mode']=='draining'
 manifest=json.load((root/'experiment.json').open())
 for i in range(3):
  status=call('status');live=[x for x in status['worker']['attempts']if x['experiment']==manifest['id']and x['state']in['running','executed','packing','done']]
  if live:raise SystemExit('live/uncollected attempts remain; not stopping worker')
  if i<2:time.sleep(2)
 stopped=call('stop')
 if not stopped.get('daemon_lock_released'):raise RuntimeError('worker daemon lock remains held')
 print(json.dumps(stopped,indent=2))
