"""v21 operational CPU cap. Frozen games and inference are unchanged."""
import json,shlex,subprocess
import maxima_experiment_fleet as fleet

def command(host,root,out,mode,cohort=None):
 if host=='pharaoh-dev-1.local':raise RuntimeError('Host remains quarantined')
 args=(['taskset','-c','0-11'] if host=='devlaptop.local' else [])+['python3',str(root/'tools/maxima_experiment_fleet.py'),mode,'--output',str(out),'--host',host]
 if cohort:args+=['--cohort',cohort]
 return args

def install():
 def remote(host,root,out,mode,cohort=None,value=None):
  r=subprocess.run([*fleet.ssh(),'-o','BatchMode=yes','-o','ConnectTimeout=8',host,shlex.join(command(host,root,out,mode,cohort))],input=json.dumps(value) if value is not None else None,capture_output=True,text=True,timeout=40)
  if r.returncode:raise RuntimeError(host+': '+r.stderr[-2000:])
  parsed=json.loads(r.stdout)
  if not isinstance(parsed,dict):raise RuntimeError(host+': non-object response')
  if mode=='pulse' and not all(k in parsed for k in ('jobs','stop','summary')):raise RuntimeError(host+': incomplete pulse')
  return parsed
 fleet.remote=remote
