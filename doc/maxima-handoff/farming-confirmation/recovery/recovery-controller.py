"""Continue untouched pending jobs while retaining the known failed execution.
No retry, missing-result imputation, or final inference is permitted here.
"""
import sys,os,json,time,runpy,subprocess,gzip,concurrent.futures
from pathlib import Path
sys.path.insert(0,'/Users/bradley/glob2/output/maxima-farming-sub-switches-confirmation/runtime')
import maxima_win_experiment as e,maxima_experiment_fleet as fleet,maxima_win_report as report
BASE=Path(__file__).resolve().parent;ROOT=Path('/home/bradley/glob2-maxima-defense-fix');REMOTE=ROOT/'output/farming-sub-switches-confirmation';CONFIG='/Users/bradley/glob2/output/maxima-live-controls-v21/ssh-config'
KNOWN=['ca3cddca076e15fe90357c7959b9ef3fcff5c67d886170568c8bc2bd2c53d5af', '1be5400897b765f5a6e9dee48c4011f2fdcdee9a92b2b96ec0db4d27921f402e', '4de46a225ea0e95ea6e6461fad399a6c97caa7f1a38363bc1568f90f42250f4d']
os.environ['MAXIMA_FLEET_SSH_CONFIG']=CONFIG
runpy.run_path('/Users/bradley/glob2/output/maxima-live-controls-v21/runtime-policy.py')['install']()
hosts=list(json.loads((BASE/'allocations.json').read_text()));m=json.loads((BASE/'manifest.json').read_text())
e.atomic(BASE/'recovery-process.json',{'pid':os.getpid(),'started':time.time(),'known_failed_job':KNOWN,'policy':'continue untouched pending jobs; quarantine known failure; no final inference'})
registry=BASE/'known-conversion-failures.json'
if registry.exists():KNOWN=list(set(KNOWN+json.loads(registry.read_text())))
e.atomic(registry,KNOWN)

def quarantine_same_assertion(host, value):
 stop=value['stop']
 unknown=[j for j in value['jobs'] if j['status']=='needs_investigation' and j['id'] not in KNOWN]
 if not unknown:return False
 ids=[j['id'] for j in unknown]
 if any(j['error']!='engine exited -6; inspect before retry' for j in unknown):return False
 if stop and (stop.get('job') not in ids or stop.get('reason')!='engine exited -6; inspect before retry'):return False
 code="""import json,hashlib
from pathlib import Path
b=Path(%r);ids=%r;expected=%r;evidence=[]
for key in ids:
 p=b/'runs'/key/'100000.engine.log'
 with p.open('rb') as f:
  f.seek(max(0,p.stat().st_size-4096));tail=f.read().decode(errors='replace')
 if tail.strip().splitlines()[-1]!=expected:raise RuntimeError('different assertion')
 evidence.append({'execution_id':key,'assertion':expected,'log_bytes':p.stat().st_size})
stop=b/'STOP_DISPATCH.json'
if stop.exists():
 v=json.loads(stop.read_text());assert v.get('job') in ids and v.get('reason')=='engine exited -6; inspect before retry'
 stop.rename(b/('quarantined-'+v['job']+'-STOP.json'))
print(json.dumps(evidence))
""" % (str(REMOTE),ids,"glob2: src/unit/UnitActivity.cpp:141: void Unit::handleActivity(): Assertion `owner->map->getGroundUnit(posX, posY)==gid' failed.")
 r=subprocess.run(['ssh','-F',CONFIG,host,'python3 -'],input=code,text=True,capture_output=True,timeout=40)
 if r.returncode:return False
 evidence=json.loads(r.stdout)
 for item in evidence:
  e.atomic(BASE/'crash-investigation'/(item['execution_id']+'.json'),{**item,'host':host,'time':time.time(),'policy':'same verified assertion; quarantine only, never retry or substitute','fix_pr':239})
 KNOWN.extend(ids);e.atomic(registry,KNOWN)
 # Existing queue entries remain immutable; start replaces only exited workers.
 started_hosts.discard(host)
 value['stop']=None
 return True

started_hosts=set()
def probe(host):
 try:
  if host not in started_hosts:
   fleet.remote(host,ROOT,REMOTE,'start','weak');started_hosts.add(host)
  return host,fleet.remote(host,ROOT,REMOTE,'pulse'),None
 except (subprocess.SubprocessError,OSError,RuntimeError) as error:
  return host,None,str(error)

while True:
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:values=list(pool.map(probe,hosts))
 probes={h:v for h,v,error in values if v is not None}
 failures={h:error for h,v,error in values if error}
 e.atomic(BASE/'CONNECTIVITY.json',{'time':time.time(),'failures':failures})
 for h,v in probes.items():
  quarantine_same_assertion(h,v)
  unexpected=[j for j in v['jobs'] if j['status']=='needs_investigation' and j['id'] not in KNOWN]
  if v['stop'] or unexpected:
   fleet.exp.HOSTS={h:0 for h in hosts};fleet.propagate_stop(BASE,ROOT,REMOTE,{'new_fault':h,'stop':v['stop'],'unexpected':unexpected});raise RuntimeError('New fault; inspect evidence immediately')
  keys=[j['id'] for j in v['jobs'] if j['status']=='complete' and not (BASE/'results'/j['id']/'result.json.gz').exists()][:64]
  if keys:
   try:
    transfer=subprocess.run(['rsync','-az','-e','ssh -o ConnectTimeout=5 -F '+CONFIG,'--files-from=-',h+':'+str(REMOTE/'runs')+'/',str(BASE/'results')+'/'],input=''.join(k+'/result.json\n' for k in keys),text=True,capture_output=True,timeout=12)
    if transfer.returncode:
     e.atomic(BASE/('transfer-'+h+'.json'),{'time':time.time(),'retry_next_cycle':True,'error':transfer.stderr[-2000:]});continue
   except (subprocess.TimeoutExpired,OSError) as error:
    e.atomic(BASE/('transfer-'+h+'.json'),{'time':time.time(),'retry_next_cycle':True,'error':str(error)});continue
   for k in keys:
    p=BASE/'results'/k/'result.json';v=json.loads(p.read_text());assert v['execution_id']==k
    with gzip.open(p.with_suffix('.json.gz.pending'),'wb') as f:f.write(p.read_bytes())
    p.with_suffix('.json.gz.pending').replace(p.with_suffix('.json.gz'));p.unlink()
 complete={p.parent.name for p in (BASE/'results').glob('*/result.json.gz')};running=sum(v['summary'].get('running',0) for v in probes.values());pending=sum(v['summary'].get('pending',0) for v in probes.values())
 e.atomic(BASE/'STATUS.json',{'time':time.time(),'stage':'confirmation_recovery','completed_pairs':sum(r['on'] in complete and r['off'] in complete for r in m['comparisons']),'completed_executions':len(complete),'scheduled_pairs':8000,'scheduled_executions':len(m['jobs']),'running':running,'pending':pending,'quarantined_failed_execution':KNOWN,'final_inference_blocked':True,'hosts':{h:v['summary'] for h,v in probes.items()}})
 if len(probes)==len(hosts) and not running and not pending:
  for h in hosts:fleet.remote(h,ROOT,REMOTE,'stop',value={'reason':'Remaining jobs collected; crash resolution required before inference'})
  break
 time.sleep(20)
