#!/usr/bin/env python3
"""Exactly three shared-state paired-horizon observer runs; never model fitting."""
import csv,datetime,fcntl,hashlib,json,os,pathlib,signal,subprocess,time,traceback
D=pathlib.Path(__file__).resolve().parent;P=json.loads((D/'INPUTS.json').read_text());A=json.loads((D/'ANALYSIS-LOCK-BINDING.json').read_text());R=pathlib.Path(P['runtime']);lock=(D/'collection.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def atomic(p,v):
 t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(v,sort_keys=True)+'\n');t.replace(p)
S=dict(status='PRELAUNCH',pid=os.getpid(),utc=stamp(),inputs_sha256=sha(D/'INPUTS.json'),rows=[],no_training_ingestion=True)
def save(**kw):S.update(kw,updated_utc=stamp());atomic(D/'STATUS.json',S)
try:
 assert not (D/'LAUNCH-INTENT.json').exists(),'Ambiguous prior intent; no automatic rerun'
 pilot=D.parents[1]/'prospective-study';ps=json.loads((pilot/'STATUS.json').read_text());assert ps['status']=='COMPLETE' and ps['observer_parity']=='PASS' and not ps.get('active')
 for k,h in A['files'].items():assert sha(k)==h,'Frozen analysis contract changed: '+k
 assert A['collection_inputs_sha256']==S['inputs_sha256']
 gatepath=pathlib.Path(P['runtime_gate']);g=json.loads(gatepath.read_text());assert g['status']=='PASS' and g['research_only'] and g['source_base']==P['source_base'] and g['model_sha256']==P['model_sha256'] and g['header_sha256']==P['header_sha256'] and g['threshold_milli']==128000 and g['sidecar_contract']==P['sidecar_contract'] and all(g['checks'].values())
 assert sha(R/'bin/glob2')==P['expected_binary_sha256']==g['binary_sha256']
 for line in subprocess.check_output(['ps','-eo','pid=,args='],text=True).splitlines():
  parts=line.strip().split(None,1)
  if len(parts)==2 and '/bin/glob2 ' in parts[1]:
   try:
    if os.sched_getaffinity(int(parts[0]))=={2}:raise RuntimeError('CPU2 still has exactsinglecore engine: '+line)
   except ProcessLookupError:pass
 save(status='RUNNING',pilot_status_sha256=sha(pilot/'STATUS.json'),gate_sha256=sha(gatepath),binary_sha256=g['binary_sha256'],analysis_binding_sha256=sha(D/'ANALYSIS-LOCK-BINDING.json'))
 atomic(D/'LAUNCH-INTENT.json',dict(utc=stamp(),pid=os.getpid(),inputs_sha256=S['inputs_sha256'],analysis_binding_sha256=S['analysis_binding_sha256'],gate_sha256=S['gate_sha256'],pilot_status_sha256=S['pilot_status_sha256'],one_attempt=True,no_automatic_retry=True))
 (D/'runs').mkdir(exist_ok=True)
 for m in P['maps']:
  warm=pathlib.Path(m['warm_save_path']);assert sha(warm)==m['warm_save_sha256'];out=D/'runs'/m['group'];out.mkdir(exist_ok=False)
  args=['taskset','-c','2','nice','-n','19',str(R/'bin/glob2'),'game','run','--load-game',str(warm),'--ticks','17408','--output-dir',str(out),'--profile',str(out/'profile'),'--benchmark-warmup','0','--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',str(R/'data'),'--resource-gradient-probe','--gradient-work-stats',str(out/'gradient-work.csv'),'--telemetry','checksums','--save','final']
  env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',LD_LIBRARY_PATH=str(R/'lib/glob2'))
  atomic(out/'COMMAND.json',dict(argv=args,cwd=str(R),environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','LD_LIBRARY_PATH']},warm_save_sha256=m['warm_save_sha256'],inputs_sha256=S['inputs_sha256'],gate_sha256=S['gate_sha256']))
  start=time.monotonic();capped=False
  with (out/'stdout.log').open('w') as so,(out/'stderr.log').open('w') as se:
   child=subprocess.Popen(args,cwd=R,env=env,stdout=so,stderr=se,start_new_session=True);save(active=dict(pid=child.pid,group=m['group'],started_utc=stamp()))
   while True:
    done,status,usage=os.wait4(child.pid,os.WNOHANG)
    if done:break
    if time.monotonic()-start>=1800:
     capped=True;os.killpg(child.pid,signal.SIGKILL);done,status,usage=os.wait4(child.pid,0);break
    time.sleep(.2)
   child.returncode=os.waitstatus_to_exitcode(status)
  result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else None;rows=[]
  if (out/'paired-horizons.csv').exists():
   with (out/'paired-horizons.csv').open() as f:rows=list(csv.DictReader(f))
  row=dict(group=m['group'],map_group=m['map_group'],returncode=child.returncode,capped=capped,wall_seconds=time.monotonic()-start,process_cpu_seconds=usage.ru_utime+usage.ru_stime,start_tick=16384,end_tick=result.get('ticks') if result else None,actual_ticks_advanced=(result['ticks']-16384) if result else None,threshold_milli=128000,paired_rows=len(rows),result=result,artifact_hashes={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name not in ['stdout.log','stderr.log','progress.jsonl']})
  if result:assert result['adaptive_gradient_policy']['threshold_milli_direction_cost']==128000 and result['benchmark_measured_ticks']==row['actual_ticks_advanced']
  reference=pilot/'runs'/f'on-r0-{m["group"]}-a128';names=['world.checksums','snapshot.checksums','game.replay.checksums','final.game.gz','gradient-quality.csv','gradient-planning-quality.csv','gradient-episodes.csv','gradient-contradictions.csv'];row['unchanged_H128_and_world']={n:(out/n).exists() and (reference/n).exists() and sha(out/n)==sha(reference/n) for n in names}
  atomic(out/'RECEIPT.json',row);S.pop('active',None);S['rows'].append(row);save(status='RUNNING',completed=len(S['rows']))
 save(status='COMPLETE',completed=3,finished_utc=stamp(),purpose=P['purpose'])
except Exception:
 save(status='FAILED-REQUIRES-REVIEW',error=traceback.format_exc());raise
