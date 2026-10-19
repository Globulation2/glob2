#!/usr/bin/env python3
"""Frozen prospective threshold pilot. No fitting, collection ingestion or promotion."""
import csv,datetime,fcntl,hashlib,json,os,pathlib,signal,subprocess,time,traceback
D=pathlib.Path(__file__).resolve().parent;P=json.loads((D/'PROSPECTIVE-PLAN.json').read_text());R=pathlib.Path(P['runtime']);LOCK=(D/'pilot.lock').open('a');fcntl.flock(LOCK,fcntl.LOCK_EX|fcntl.LOCK_NB)
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def atomic(p,v):
 t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(v,sort_keys=True)+'\n');t.replace(p)
state=dict(status='WAITING-GATE',pid=os.getpid(),started_utc=stamp(),plan_sha256=sha(D/'PROSPECTIVE-PLAN.json'),rows=[],no_training_ingestion=True,no_promotion=True)
def save(**kw):state.update(kw,updated_utc=stamp());atomic(D/'STATUS.json',state)
def context():
 v=dict(utc=stamp(),loadavg=pathlib.Path('/proc/loadavg').read_text().strip())
 for n in ['topology/thread_siblings_list','cpufreq/scaling_cur_freq','cpufreq/scaling_governor']:
  p=pathlib.Path('/sys/devices/system/cpu/cpu2')/n;v[n]=p.read_text().strip() if p.exists() else None
 lines=subprocess.check_output(['ps','-eo','pid=,psr=,pcpu=,args='],text=True).splitlines();v['glob2_processes']=[l for l in lines if '/bin/glob2 ' in l and 'python' not in l]
 return v
def run(m,arm,kind,repeat=0):
 name=f'{kind}-r{repeat}-{m["group"]}-a{arm}';out=D/'runs'/name;out.mkdir(exist_ok=False)
 probe=kind=='on';parity=probe or kind=='short-off';target=P['on_absolute_tick_limit'] if parity else P['off_absolute_tick_limit'];warm=pathlib.Path(m['warm_save_path']);assert sha(warm)==m['warm_save_sha256']
 args=['taskset','-c','2','nice','-n','19',str(R/'bin/glob2'),'game','run','--load-game',str(warm),'--ticks',str(target),'--output-dir',str(out),'--profile',str(out/'profile'),'--benchmark-warmup','0','--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',str(R/'data')]
 if probe:args+=['--resource-gradient-probe','--gradient-work-stats',str(out/'gradient-work.csv')]
 if parity:args+=['--telemetry','checksums','--save','final']
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',LD_LIBRARY_PATH=str(R/'lib/glob2'));env[P['threshold_environment']]=str(P['threshold_milli'][str(arm)])
 before=context();atomic(out/'COMMAND.json',dict(argv=args,cwd=str(R),environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','LD_LIBRARY_PATH',P['threshold_environment']]},warm_sha256=m['warm_save_sha256'],plan_sha256=state['plan_sha256'],gate_sha256=state['gate_sha256'],context_before=before,wall_cap_seconds=1800))
 start=time.monotonic();capped=False
 with (out/'stdout.log').open('w') as so,(out/'stderr.log').open('w') as se:
  child=subprocess.Popen(args,cwd=R,env=env,stdout=so,stderr=se,start_new_session=True);save(active=dict(name=name,pid=child.pid,started_utc=stamp(),wall_cap_seconds=1800))
  while True:
   done,status,usage=os.wait4(child.pid,os.WNOHANG)
   if done:break
   if time.monotonic()-start>=1800:
    capped=True;os.killpg(child.pid,signal.SIGKILL);done,status,usage=os.wait4(child.pid,0);break
   time.sleep(.2)
  child.returncode=os.waitstatus_to_exitcode(status)
 wall=time.monotonic()-start;result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else None;cpu=usage.ru_utime+usage.ru_stime;end=result.get('ticks') if result else None;delta=end-m['warm_tick'] if end is not None else None
 row=dict(name=name,group=m['group'],map_group=m['map_group'],format=m['format'],arm=arm,threshold_milli=P['threshold_milli'][str(arm)],kind=kind,repeat=repeat,attempt=1,returncode=child.returncode,capped=capped,wall_seconds=wall,process_user_seconds=usage.ru_utime,process_system_seconds=usage.ru_stime,process_total_cpu_seconds=cpu,maximum_rss_kib=usage.ru_maxrss,start_tick=m['warm_tick'],end_tick=end,actual_ticks_advanced=delta,requested_absolute_limit=target,requested_additional_ticks=target-m['warm_tick'],result=result,context_after=context(),output=str(out),warm_save_sha256=m['warm_save_sha256'])
 if delta and delta>0:row.update(process_cpu_ms_per_tick=cpu*1000/delta,wall_tps=delta/wall)
 if result:
  policy=result['adaptive_gradient_policy'];assert policy['threshold_milli_direction_cost']==row['threshold_milli'];side=out/'research-gradient-threshold-input.json';assert policy['research_input_sha256']==sha(side)
  assert result['benchmark_measured_ticks']==delta
  row['benchmark_run_cpu_ms_per_tick']=result['benchmark_run_cpu_ns']/1e6/delta if delta else None;row['research_input_sha256']=sha(side)
  row['full_requested_horizon']=delta==row['requested_additional_ticks'] and result['termination']=='tick_cap'
 row['artifact_hashes']={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name not in ['stdout.log','stderr.log','progress.jsonl']};atomic(out/'MEASUREMENT.json',row);state.pop('active',None);state['rows'].append(row);save(status='RUNNING',finished_runs=len(state['rows']));return row
try:
 save()
 gatepath=pathlib.Path(P['required_gate']);deadline=time.time()+1800
 while not gatepath.exists():
  if time.time()>deadline:raise RuntimeError('Qualified runtime gate absent after bounded30min wait')
  time.sleep(30)
 gate=json.loads(gatepath.read_text());assert gate['status']=='PASS' and gate['research_only'] and gate['source_base']==P['source_base'] and gate['model_sha256']==P['model_sha256'] and gate['header_sha256']==P['header_sha256']
 assert gate['binary_sha256']==sha(R/'bin/glob2') and gate['environment_key']==P['threshold_environment'];assert all(gate['checks'].values())
 assert not (D/'LAUNCH-INTENT.json').exists(),'Ambiguous/duplicate prior launch; no automatic retry'
 for m in P['maps']:
  if m['warm_usable']:assert sha(m['warm_save_path'])==m['warm_save_sha256'] and m['warm_tick']==16384 and m['warm_save_minor']==178
 atomic(D/'GATE-SNAPSHOT.json',gate);save(status='RUNNING',gate_sha256=sha(gatepath),binary_sha256=gate['binary_sha256'])
 atomic(D/'LAUNCH-INTENT.json',dict(utc=stamp(),pid=os.getpid(),plan_sha256=state['plan_sha256'],gate_sha256=state['gate_sha256'],one_attempt=True,no_automatic_retry=True))
 skipped=[]
 for m in P['maps']:
  if not m['warm_usable']:
   skipped.append(dict(group=m['group'],reason='predeclared warmup ended/unusable',long_off_cells=6,on_cells=3));continue
  for rep,order in enumerate(m['off_repeat_orders']):
   for arm in order:run(m,arm,'off',rep)
 for m in P['maps']:
  if m['warm_usable']:
   for arm in m['diagnostic_order']:run(m,arm,'on')
 first=next(m for m in P['maps'] if m['warm_usable']);parity=[]
 for arm in P['thresholds']:
  off=run(first,arm,'short-off');on=next(r for r in state['rows'] if r['kind']=='on' and r['group']==first['group'] and r['arm']==arm)
  names=['world.checksums','snapshot.checksums','game.replay.checksums','final.game.gz'];available={n:(n in off['artifact_hashes'],n in on['artifact_hashes']) for n in names};matched={n:off['artifact_hashes'].get(n)==on['artifact_hashes'].get(n) and available[n]==(True,True) for n in names}
  parity.append(dict(arm=arm,group=first['group'],artifacts=matched,available=available,initial_equal=off['result']['initialChecksum']==on['result']['initialChecksum'],final_equal=off['result']['finalChecksum']==on['result']['finalChecksum']))
  assert all(matched.values()) and parity[-1]['initial_equal'] and parity[-1]['final_equal'],'Mature observer parity failed'
 atomic(D/'OBSERVER-PARITY.json',dict(status='PASS',checks=parity))
 save(status='COMPLETE',finished_utc=stamp(),skipped=skipped,executed_runs=len(state['rows']),observer_parity='PASS',interpretation='No production claim; final analysis pending; diagnostic1024jobs not extrapolated to8192timings')
except Exception:
 save(status='FAILED-REQUIRES-REVIEW',error=traceback.format_exc());raise
