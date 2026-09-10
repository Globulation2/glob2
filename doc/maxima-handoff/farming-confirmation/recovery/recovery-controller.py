"""Continue untouched pending jobs while retaining the known failed execution.
No retry, missing-result imputation, or final inference is permitted here.
"""
import sys,os,json,time,runpy,subprocess,gzip,concurrent.futures
from pathlib import Path
sys.path.insert(0,'/Users/bradley/glob2/output/maxima-farming-sub-switches-confirmation/runtime')
import maxima_win_experiment as e,maxima_experiment_fleet as fleet,maxima_win_report as report
BASE=Path(__file__).resolve().parent;ROOT=Path('/home/bradley/glob2-maxima-defense-fix');REMOTE=ROOT/'output/farming-sub-switches-confirmation';CONFIG='/Users/bradley/glob2/output/maxima-live-controls-v21/ssh-config'
KNOWN=['ca3cddca076e15fe90357c7959b9ef3fcff5c67d886170568c8bc2bd2c53d5af', '1be5400897b765f5a6e9dee48c4011f2fdcdee9a92b2b96ec0db4d27921f402e']
os.environ['MAXIMA_FLEET_SSH_CONFIG']=CONFIG
runpy.run_path('/Users/bradley/glob2/output/maxima-live-controls-v21/runtime-policy.py')['install']()
hosts=list(json.loads((BASE/'allocations.json').read_text()));m=json.loads((BASE/'manifest.json').read_text())
e.atomic(BASE/'recovery-process.json',{'pid':os.getpid(),'started':time.time(),'known_failed_job':KNOWN,'policy':'continue untouched pending jobs; quarantine known failure; no final inference'})
for h in hosts:fleet.remote(h,ROOT,REMOTE,'start','weak')
while True:
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:probes=dict(zip(hosts,pool.map(lambda h:fleet.remote(h,ROOT,REMOTE,'pulse'),hosts)))
 for h,v in probes.items():
  unexpected=[j for j in v['jobs'] if j['status']=='needs_investigation' and j['id'] not in KNOWN]
  if v['stop'] or unexpected:
   fleet.exp.HOSTS={h:0 for h in hosts};fleet.propagate_stop(BASE,ROOT,REMOTE,{'new_fault':h,'stop':v['stop'],'unexpected':unexpected});raise RuntimeError('New fault; inspect evidence immediately')
  keys=[j['id'] for j in v['jobs'] if j['status']=='complete' and not (BASE/'results'/j['id']/'result.json.gz').exists()][:64]
  if keys:
   subprocess.run(['rsync','-az','-e','ssh -F '+CONFIG,'--files-from=-',h+':'+str(REMOTE/'runs')+'/',str(BASE/'results')+'/'],input=''.join(k+'/result.json\n' for k in keys),text=True,check=True)
   for k in keys:
    p=BASE/'results'/k/'result.json';v=json.loads(p.read_text());assert v['execution_id']==k
    with gzip.open(p.with_suffix('.json.gz.pending'),'wb') as f:f.write(p.read_bytes())
    p.with_suffix('.json.gz.pending').replace(p.with_suffix('.json.gz'));p.unlink()
 complete={p.parent.name for p in (BASE/'results').glob('*/result.json.gz')};running=sum(v['summary'].get('running',0) for v in probes.values());pending=sum(v['summary'].get('pending',0) for v in probes.values())
 e.atomic(BASE/'STATUS.json',{'time':time.time(),'stage':'confirmation_recovery','completed_pairs':sum(r['on'] in complete and r['off'] in complete for r in m['comparisons']),'completed_executions':len(complete),'scheduled_pairs':8000,'scheduled_executions':len(m['jobs']),'running':running,'pending':pending,'quarantined_failed_execution':KNOWN,'final_inference_blocked':True,'hosts':{h:v['summary'] for h,v in probes.items()}})
 if not running and not pending:
  for h in hosts:fleet.remote(h,ROOT,REMOTE,'stop',value={'reason':'Remaining jobs collected; crash resolution required before inference'})
  break
 time.sleep(20)
