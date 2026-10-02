import subprocess,os,signal,shlex,json
from pathlib import Path
out=Path('artifacts/memory-optimization/peak-followup')
rows=[l.strip().split(None,2) for l in subprocess.check_output(['ps','-axo','pid,ppid,command'],text=True).splitlines()[1:]]
parents=[int(pid) for pid,ppid,cmd in rows if 'tools/memory_benchmark.py --baseline artifacts/memory-optimization/peak-followup/baseline/glob2' in cmd and '--output artifacts/memory-optimization/peak-followup/cpu-final' in cmd and Path(shlex.split(cmd)[0]).name.lower().startswith('python')]
if len(parents)>1:raise RuntimeError('ambiguous owned benchmark')
parent=parents[0] if parents else None
command=['python3','test/run_tests.py','--binary','engine']
for suite in ['AISavePortability','CastorContinuation','CustomGameSetup','TeamStatsSave','BuildingGradientInvalidation','AIDecisionCoverage']:command+=['--filter',suite+'/*']
command+=['--junit',str(out/'continuation-tests.xml')]
def stop(signum,_):raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,stop)
try:
 children=[]
 if parent:
  os.kill(parent,signal.SIGSTOP)
  rows=[l.strip().split(None,2) for l in subprocess.check_output(['ps','-axo','pid,ppid,command'],text=True).splitlines()[1:]]
  children=[int(pid) for pid,ppid,cmd in rows if int(ppid)==parent]
  for pid in children:os.kill(pid,signal.SIGSTOP)
 (out/'final-functional-recheck.json').write_text(json.dumps({'command':command,'paused_parent':parent,'paused_children':children},indent=2))
 with (out/'continuation-tests.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
finally:
 if parent:
  try:os.kill(parent,signal.SIGCONT)
  except ProcessLookupError:pass
 print('Final functional recheck finished; CPU runner resumed',flush=True)
