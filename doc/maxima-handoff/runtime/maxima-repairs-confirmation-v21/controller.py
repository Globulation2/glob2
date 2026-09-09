"""Amended confirmation: complete batches, six predeclared adjusted looks."""
import sys,os,json,gzip,time,subprocess,shutil,concurrent.futures,hashlib,runpy
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'tools'))
import maxima_win_experiment as e
import maxima_win_report as report
import maxima_win_statistics as stats
import maxima_experiment_fleet as fleet
BASE=Path(__file__).resolve().parent
PRIOR=BASE.parent/'maxima-live-controls-v21'
ROOT=Path('/home/bradley/glob2-win-continuation-20260908')
REMOTE=ROOT/'output/repairs-confirmation-v21'
HOSTS=['pharaoh-dev-2.local','pharaoh-dev-3.local','therig.local','devlaptop.local']
runpy.run_path(str(PRIOR/'runtime-policy.py'))['install']()
CONFIG=str((BASE.parent/'maxima-live-controls-v21/ssh-config').resolve())
os.environ['MAXIMA_FLEET_SSH_CONFIG']=CONFIG
TOTAL=21000;SIZE=500;SWITCH='repairs.enabled'

def ssh(host,code,timeout=120):
 r=subprocess.run(['ssh','-F',CONFIG,'-o','ConnectTimeout=8',host,'python3 -'],input=code,text=True,capture_output=True,timeout=timeout)
 if r.returncode:raise RuntimeError(host+': '+r.stderr[-1800:])
 return json.loads(r.stdout) if r.stdout.strip() else None

def manifest(p,start,count):
 jobs={};rows=[]
 for i in range(start,start+count):
  s=e.scenario(p,'confirmation',i)
  row={'switch':SWITCH,'scenario_id':s['scenario_id'],'format':s['format'],'opponent':s['opponent'],'stage':'confirmation','index':i,'repeats':{}}
  for arm in s['order']:
   settings=e.arm_settings(p,s,SWITCH,arm=='on');job=report.execution(s,settings);jobs[job['execution_id']]=job;row[arm]=job['execution_id']
   if s['repeat']:
    rep=report.execution(s,settings,True);jobs[rep['execution_id']]=rep;row['repeats'][arm]=rep['execution_id']
  rows.append(row)
 return {'protocol_id':p['protocol_id'],'stage':'confirmation','fixed_pairs':{SWITCH:count},'jobs':list(jobs.values()),'comparisons':rows,'operational_batch_only':True,'full_fixed_pairs':TOTAL}

def prepare(p,allocation,start):
 local=BASE/('controls' if start==-200 else f'batch-{start:05d}');remote=REMOTE/local.name
 if (local/'manifest.json').exists():return local,remote,json.loads((local/'manifest.json').read_text())
 local.mkdir(exist_ok=True);q=json.loads((PRIOR/'qualification.json').read_text())
 for gate in q.values():
  if isinstance(gate,dict) and gate.get('path'):
   dest=local/gate['path'];dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(PRIOR/gate['path'],dest)
 m=report.comparisons(p,'controls',{'no_orders':200},[SWITCH],{e.NO_ORDERS:True}) if start==-200 else manifest(p,start,min(SIZE,TOTAL-start));assign=fleet.assign(m,allocation)
 # Cohort is a scheduling convenience, not an inferential stratum. Enqueue
 # both cohorts together to avoid idle slots behind a global barrier.
 for a in assign.values():a['weak']+=a['strong'];a['strong']=[]
 for name,value in [('protocol',p),('qualification',q),('manifest',m),('assignments',assign),('allocations',allocation),('allocation',{'protocol_id':p['protocol_id'],'approved':True,'authorization':'User authorized corrected-engine restart with fresh controls, independent confirmation, same maximum allocation and sequential policy','fixed_total_pairs':TOTAL,'batch_start':start,'external_purchases':False})]:e.atomic(local/(name+'.json'),value)
 e.require_gates(local,p,m['stage'])
 files=[str(x.relative_to(local)) for x in local.rglob('*.json')]
 for h in assign:
  ssh(h,f"from pathlib import Path;Path('{remote}').mkdir(parents=True,exist_ok=True)")
  subprocess.run(['rsync','-az','-e','ssh -F '+CONFIG,'--files-from=-',str(local)+'/',h+':'+str(remote)+'/'],input='\n'.join(files)+'\n',text=True,check=True)
 return local,remote,m

def recover(h,remote,key):
 # An existing successful engine can be recovered. Never rerun uncertain games.
 code=f'''import json,sqlite3,sys
from pathlib import Path
sys.path.insert(0,'{ROOT}/tools');import maxima_win_experiment as e
out=Path('{remote}');db=sqlite3.connect(out/'queue.sqlite');db.row_factory=sqlite3.Row;r=dict(db.execute('select * from jobs where id=?',('{key}',)).fetchone());assert r['status'] in ('running','needs_investigation')
assert not Path('/proc/'+str(r['pid'])).exists()
d=out/'runs/{key}';engine=json.loads((d/'engine.json').read_text());assert engine['exit_code']==0 and engine['status']=='exited';assert not Path('/proc/'+str(engine['pid'])).exists()
r['command']=json.loads((d/'200000.command.json').read_text());r['audit_sha256']=e.sha(d/'200000.audit.jsonl');r['exited_at']=(d/'engine.json').stat().st_mtime;r['checkpoint']=(d/'checkpoints/checkpoint-200000.game').exists();print(json.dumps(r))
'''
 row=ssh(h,code);d=BASE/'recoveries'/key;d.mkdir(parents=True,exist_ok=True);e.atomic(d/'source.json',row)
 audit=d/'audit.jsonl';subprocess.run(['scp','-F',CONFIG,h+':'+str(remote/'runs'/key/'200000.audit.jsonl'),str(audit)],check=True)
 assert e.sha(audit)==row['audit_sha256'];payload=json.loads(row['payload']);p=json.loads((BASE/'protocol.json').read_text());s=payload['scenario'];settings=payload['settings'];env=row['command']['identities']
 assert env['protocol']==p['protocol_id'] and env['binary']==p['binary_sha256'] and env['scenario']==s['scenario_id'] and env['configuration']==e.identity(settings)
 receipt=e.read_audit(audit,env,s,settings);assert receipt['terminal']['tick']<=200000
 if receipt['outcome'] is None:assert receipt['terminal']['tick']==200000 and row['checkpoint']
 result={'scenario_id':s['scenario_id'],'execution_id':key,'repeat':payload.get('repeat',False),'configuration_id':e.identity(settings),'execution_mode':p['execution_mode'],**e.adjudicate(p,s,receipt),'seconds':row['exited_at']-row['started'],'tick':receipt['terminal']['tick'],'receipts':[receipt],'recovery':{'engine_rerun':False,'audit_sha256':row['audit_sha256'],'seconds_method':'persisted engine exit minus queue start; outage excluded'}}
 e.atomic(d/'result.json',result)
 subprocess.run(['scp','-F',CONFIG,str(d/'result.json'),h+':'+str(remote/'runs'/key/'result.json')],check=True)
 ssh(h,f"import sys,json;from pathlib import Path;sys.path.insert(0,'{ROOT}/tools');import maxima_experiment_queue as q;out=Path('{remote}');db=q.Queue(out/'queue.sqlite');db.finish('{key}',json.loads((out/'runs/{key}/result.json').read_text()))")


def batch_hosts(local):
 return list(json.loads((local/'assignments.json').read_text()))

def advance_idle_hosts(p,allocation,start,local,probes):
 # Never prepare or dispatch beyond the next registered significance look.
 boundary=start+SIZE
 looks={row['pairs'] for row in json.loads((BASE/'SEQUENTIAL_AMENDMENT.json').read_text())['looks']}
 if start<0 or boundary>=TOTAL or boundary in looks:return {}
 assignments=json.loads((local/'assignments.json').read_text())
 eligible=[]
 for h,v in probes.items():
  expected={key for cohort in assignments[h].values() for key in cohort}
  complete={j['id'] for j in v['jobs'] if j['status']=='complete'}
  if not v['stop'] and expected<=complete and all(j['status']=='complete' for j in v['jobs']):eligible.append(h)
 if not eligible:return {}
 nxt,remote,_=prepare(p,allocation,boundary)
 snapshots={}
 for h in eligible:
  v=fleet.remote(h,ROOT,remote,'pulse')
  if v['stop'] or any(j['status']=='needs_investigation' for j in v['jobs']):raise RuntimeError('Prefetched batch requires investigation: '+h)
  if not v['jobs'] or v['summary'].get('pending',0):
   fleet.remote(h,ROOT,remote,'start','weak');v=fleet.remote(h,ROOT,remote,'pulse')
  snapshots[h]=v
 return snapshots

def run_batch(p,allocation,start):
 local,remote,m=prepare(p,allocation,start)
 hosts=batch_hosts(local)
 if (local/'COMPLETE.json').exists():
  archive(local,remote);return
 for h in hosts:
  if not fleet.remote(h,ROOT,remote,'pulse')['stop']:fleet.remote(h,ROOT,remote,'start','weak')
 results={}
 for path in (local/'results').glob('*/result.json.gz'):
  with gzip.open(path,'rt') as f:v=json.load(f)
  results[v['execution_id']]=v
 while len(results)<len(m['jobs']):
  def probe(h):return h,fleet.remote(h,ROOT,remote,'pulse')
  with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:probes=dict(pool.map(probe,hosts))
  for h,v in probes.items():
   if v['stop'] or any(j['status']=='needs_investigation' for j in v['jobs']):
    # Only attempt receipt recovery for a recorded dead worker / parse error.
    reason=v['stop'].get('reason') if v['stop'] else ''
    if not isinstance(reason,str) or not (reason.startswith('worker vanished') or reason.startswith('missing/malformed audit')):raise RuntimeError('Fleet stop requires investigation: '+h+' '+str(reason))
    keys=[j['id'] for j in v['jobs'] if j['status']=='needs_investigation']
    if not keys and v['stop'].get('job'):keys=[v['stop']['job']]
    if not keys:raise RuntimeError('No recoverable recorded job')
    for key in keys:recover(h,remote,key)
    ssh(h,f"from pathlib import Path;import time;p=Path('{remote}/STOP_DISPATCH.json');p.rename(p.with_name('RECOVERED_STOP_'+str(time.time_ns())+'.json'))")
    fleet.remote(h,ROOT,remote,'start','weak');continue
   keys=[j['id'] for j in v['jobs'] if j['status']=='complete' and j['id'] not in results][:64]
   if keys:
    dest=local/'results';dest.mkdir(exist_ok=True)
    subprocess.run(['rsync','-az','-e','ssh -F '+CONFIG,'--files-from=-',h+':'+str(remote/'runs')+'/',str(dest)+'/'],input=''.join(k+'/result.json\n' for k in keys),text=True,check=True)
    for key in keys:
     path=dest/key/'result.json';raw=path.read_bytes();value=json.loads(raw);assert value['execution_id']==key;results[key]=value
     compressed=path.with_suffix('.json.gz');pending=path.with_suffix('.json.gz.pending')
     with gzip.open(pending,'wb') as f:f.write(raw)
     pending.replace(compressed);path.unlink()
  report.assemble(m,results) # Deterministic repeats checked; no effect estimates.
  ahead=advance_idle_hosts(p,allocation,start,local,probes)
  completed=sum(row['on'] in results and row['off'] in results for row in m['comparisons'])
  e.atomic(BASE/'STATUS.json',{'time':time.time(),'stage':m['stage'],'maximum_pairs':TOTAL,'completed_pairs':completed if start==-200 else start+completed,'batch_start':start,'batch_executions':len(results),'batch_scheduled':len(m['jobs']),'running':sum(v['summary'].get('running',0) for v in [*probes.values(),*ahead.values()]),'prefetched_batch_start':start+SIZE if ahead else None,'prefetched_completed_executions':sum(v['summary'].get('complete',0) for v in ahead.values()),'inference':'scheduled adjusted looks only; see SEQUENTIAL_AMENDMENT.json'})
  if len(results)<len(m['jobs']):time.sleep(20)
 pairs=report.assemble(m,results)['no_orders' if start==-200 else SWITCH];assert len(pairs)==len(m['comparisons']);e.atomic(local/'validated-pairs.json',pairs)
 e.atomic(local/'COMPLETE.json',{'time':time.time(),'pairs':len(pairs),'executions':len(results),'repeat_checks':'passed','input_hash':e.identity(m)})
 archive(local,remote,lambda:advance_idle_hosts(p,allocation,start,local,probes))

def archive(local,remote,heartbeat=None):
 hosts=batch_hosts(local)
 if (local/'ARCHIVED.json').exists():return
 # Stop idle workers and compress finished evidence before scheduling next batch.
 for h in hosts:fleet.remote(h,ROOT,remote,'stop',value={'reason':'operational batch complete; evidence preserved','active_engines':'none; all jobs complete'})
 archive_code=f'''from pathlib import Path
import gzip,hashlib,os,json
out=Path('{remote}')
for path in (out/'runs').rglob('*'):
 if not path.is_file() or not (path.name.endswith('.audit.jsonl') or path.name.endswith('.engine.log') or path.suffix=='.game'):continue
 target=path.with_name(path.name+'.gz');pending=target.with_name(target.name+'.pending');digest=hashlib.sha256()
 with path.open('rb') as source,gzip.open(pending,'wb',compresslevel=1) as dest:
  while chunk:=source.read(1048576):digest.update(chunk);dest.write(chunk)
 check=hashlib.sha256()
 with gzip.open(pending,'rb') as source:
  while chunk:=source.read(1048576):check.update(chunk)
 assert digest.digest()==check.digest();pending.replace(target);target.with_name(target.name+'.sha256').write_text(digest.hexdigest());path.unlink()
print(json.dumps({{'archived':True}}))
'''
 with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
  pending={pool.submit(ssh,h,archive_code,3600) for h in hosts}
  while pending:
   done,pending=concurrent.futures.wait(pending,timeout=20,return_when=concurrent.futures.FIRST_COMPLETED)
   for result in done:result.result()
   if heartbeat:heartbeat()
 e.atomic(local/'ARCHIVED.json',{'verified_lossless':True,'time':time.time()})


def load_amendment():
 import importlib.util
 policy_path=BASE/'SEQUENTIAL_AMENDMENT.json'
 policy=json.loads(policy_path.read_text())
 qualification=json.loads((BASE/'SEQUENTIAL_QUALIFICATION.json').read_text())
 assert qualification['policy_sha256']==e.sha(policy_path) and qualification['two_point_power_passed']
 assert e.sha(BASE/'PLAN.json')==policy['original_plan_sha256']
 assert e.sha(Path(stats.__file__))==policy['statistics_sha256']
 assert e.sha(BASE/'sequential-analysis.py')==policy['analysis_sha256']
 spec=importlib.util.spec_from_file_location('frozen_sequential',BASE/'sequential-analysis.py')
 sequential=importlib.util.module_from_spec(spec);spec.loader.exec_module(sequential)
 sequential.validate_schedule(policy['looks'],policy['hypothesis_alpha'],TOTAL)
 return policy,sequential


def scheduled_look(p,policy,sequential,n):
 if n not in [r['pairs'] for r in policy['looks']]:return False
 pairs=[];input_hashes={};expected=[]
 for start in range(0,n,SIZE):
  local=BASE/f'batch-{start:05d}'
  complete=json.loads((local/'COMPLETE.json').read_text())
  assert complete['repeat_checks']=='passed'
  assert (local/'ARCHIVED.json').exists()
  manifest_value=json.loads((local/'manifest.json').read_text())
  assert complete['input_hash']==e.identity(manifest_value)
  rows=json.loads((local/'validated-pairs.json').read_text())
  expected_batch=[e.scenario(p,'confirmation',i)['scenario_id'] for i in range(start,start+SIZE)]
  assert [r['scenario_id'] for r in rows]==expected_batch
  assert len(rows)==complete['pairs']==SIZE
  pairs+=rows;expected+=expected_batch
  input_hashes[local.name]=e.sha(local/'validated-pairs.json')
 assert len(pairs)==n and len(set(expected))==n
 value=sequential.evaluate(pairs,policy['looks'],policy['hypothesis_alpha'],TOTAL)
 value.update(protocol_id=p['protocol_id'],switch=SWITCH,confirmatory=True,
              amendment_sha256=e.sha(BASE/'SEQUENTIAL_AMENDMENT.json'),
              validated_input_hashes=input_hashes)
 path=BASE/f'look-{n:05d}.json'
 if path.exists():assert json.loads(path.read_text())==value
 else:e.atomic(path,value)
 if value['stop']:
  e.atomic(BASE/'CONFIRMATION_RESULT.json',value)
  e.atomic(BASE/'STATUS.json',{'stage':'confirmation_complete','completed_pairs':n,
    'maximum_pairs':TOTAL,'time':time.time(),'running':0,'default_changes':False,
    'stop_reason':value['reason'],'amendment':'SEQUENTIAL_AMENDMENT.json'})
 return value['stop']


def main():
 if (BASE/"RETIRED.json").exists():
  raise RuntimeError("Retired by user; no further repairs dispatch")
 policy,sequential=load_amendment()
 p=json.loads((BASE/'protocol.json').read_text())
 assert p['protocol_id']==policy['protocol_id'] and p['hypothesis_alpha']==policy['hypothesis_alpha']
 assert json.loads((PRIOR/'QUALIFICATION_PASSED.json').read_text())['protocol_id']==p['protocol_id']
 retired=json.loads((BASE.parent/'maxima-repairs-confirmation-v19-resumed/RETIRED.json').read_text())
 assert not retired['restart_allowed']
 allocation=json.loads((PRIOR/'allocations.json').read_text())
 # New controls, never pooled with previous controls or confirmation.
 run_batch(p,allocation,-200)
 local=BASE/'controls';m=json.loads((local/'manifest.json').read_text());results={}
 for path in (local/'results').glob('*/result.json.gz'):
  with gzip.open(path,'rt') as stream:r=json.load(stream)
  results[r['execution_id']]=r
 control=report.controls_checkpoint(p,m,results,True)
 control.update(protocol_id=p['protocol_id'],next_stage='repairs_confirmation' if control['accepted'] else 'investigation; no further dispatch')
 e.atomic(BASE/'controls-acceptance.json',control)
 if not control['accepted']:
  e.atomic(BASE/'STOP_DISPATCH.json',{'reason':'fresh AI-off statistical control did not pass','protocol_id':p['protocol_id']})
  e.atomic(BASE/'STATUS.json',{'stage':'control_failed','running':0,'completed_pairs':200,'time':time.time()})
  return
 # Add fresh acceptance to the new protocol's gates before constructing any confirmation batch.
 gate=json.loads((PRIOR/'qualification.json').read_text());shutil.copy2(BASE/'controls-acceptance.json',PRIOR/'controls-acceptance.json')
 gate['positive_control']={'status':'passed','path':'controls-acceptance.json','sha256':e.sha(PRIOR/'controls-acceptance.json')}
 e.atomic(PRIOR/'qualification.json',gate)
 for start in range(0,TOTAL,SIZE):
  run_batch(p,allocation,start)
  if scheduled_look(p,policy,sequential,start+SIZE):return

if __name__=='__main__':
 try:main()
 except Exception as error:
  e.atomic(BASE/'CONTROLLER_ERROR.json',{'time':time.time(),'error':str(error),'games':'preserved; remote authority expires without pulses'});raise
