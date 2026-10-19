"""Isolated, frozen Mac CPU attribution. No training or production edits."""
import collections,datetime,fcntl,hashlib,json,os,pathlib,signal,subprocess,time,traceback
B=pathlib.Path(__file__).resolve().parent;D=B;P=json.loads((B/'PLAN.json').read_text())
sha=lambda p:hashlib.file_digest(pathlib.Path(p).open('rb'),'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(n,v):
 p=B/n;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(v,indent=2)+'\n');t.replace(p)
lock=(B/'experiment.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
exec((B/'mac-observation-helpers.py').read_text())
assert sha(B/'PLAN.json')==(B/'PLAN.sha256').read_text().split()[0]
assert not (B/'LAUNCH.json').exists(),'Never automatically rerun an interrupted attribution batch'
state=dict(status='WAITING-BUILD-GATES',plan_sha256=sha(B/'PLAN.json'),rows=[],qualification=[],started_utc=now())
def save():state['updated_utc']=now();write('STATUS.json',state)
def run(cell,ticks,warmup,probe=False,trace=False,report=False,final=False):
 m=next(m for m in P['maps'] if m['group']==cell['group']);a=arms[cell['arm']];phase=cell['phase'];name=f"{phase}-{cell['index']:02d}-{m['group']}-{a['id']}-r{cell.get('repeat',0)}";out=B/'runs'/name;out.mkdir(parents=True,exist_ok=False)
 assert sha(m['warm_path'])==m['warm_sha256'];assert sha(a['binary'])==a['binary_sha256']
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',GLOB2_ASSET_DIR=P['master_data_dir'],GLOB2_USER_DATA_DIR=str(out/'profile'));env.update(a['env'])
 if report:env['GLOB2_RESEARCH_CLASS_REPORT']=str(out/'class-diagnostic.json')
 argv=[a['binary'],'game','run','--load-game',m['warm_path'],'--ticks',str(m['start_tick']+warmup+ticks),'--output-dir',str(out),'--profile',str(out/'profile'),'--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',P['master_data_dir']]
 if phase!='qualification':argv+=['--benchmark-warmup',str(warmup)]
 if probe:argv+=['--resource-gradient-probe','--gradient-work-stats',str(out/'gradient-work.csv')]
 if trace:argv+=['--telemetry','checksums']
 if final:argv+=['--save','final']
 if phase=='cpu':assert not probe and not trace and not report and all(x not in argv for x in ['--resource-gradient-probe','--gradient-work-stats','--telemetry'])
 write(f'runs/{name}/COMMAND.json',dict(argv=argv,cwd=P['common_cwd'],environment={k:v for k,v in env.items() if k.startswith('GLOB2_') or k.startswith('SDL_')},phase=phase,context_before=context(),warm_sha256=m['warm_sha256'],binary_sha256=a['binary_sha256']))
 start=time.monotonic();capped=False
 with (out/'stdout.log').open('w') as so,(out/'stderr.log').open('w') as se:
  child=subprocess.Popen(argv,cwd=P['common_cwd'],env=env,stdout=so,stderr=se,start_new_session=True);state['active']=dict(name=name,pid=child.pid,phase=phase,started_utc=now());save()
  while True:
   done,status,usage=os.wait4(child.pid,os.WNOHANG)
   if done:break
   if time.monotonic()-start>P['wall_cap_seconds']:
    capped=True;os.killpg(child.pid,signal.SIGKILL);_,status,usage=os.wait4(child.pid,0);break
   time.sleep(.1)
  child.returncode=os.waitstatus_to_exitcode(status)
 wall=time.monotonic()-start;result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else None
 delta=result['ticks']-m['start_tick'] if result else None
 row=dict(**cell,name=name,returncode=child.returncode,capped=capped,output=str(out),format=m['format'],requested_measured_ticks=ticks,warmup=warmup,advanced_ticks=delta,measured_ticks=result.get('benchmark_measured_ticks') if result else None,result=result,user_cpu_seconds=usage.ru_utime,system_cpu_seconds=usage.ru_stime,total_process_cpu_seconds=usage.ru_utime+usage.ru_stime,wall_seconds=wall,context_after=context(),binary_sha256=a['binary_sha256'],warm_sha256=m['warm_sha256'])
 if result and phase!='qualification' and row['measured_ticks']:
  row.update(total_simulation_cpu_seconds=result['benchmark_run_cpu_ns']/1e9,simulation_cpu_ms_per_measured_tick=result['benchmark_run_cpu_ns']/1e6/row['measured_ticks'],wall_tps=delta/wall)
 if trace:row['trace_hashes']={p.name:sha(p) for p in out.glob('*.checksums')}
 if final:row['final_save_sha256']=sha(out/'final.game.gz') if (out/'final.game.gz').exists() else None
 write(f'runs/{name}/MEASUREMENT.json',row);state.pop('active',None)
 state['qualification' if phase=='qualification' else 'rows'].append(row);save()
 assert child.returncode==0 and not capped and result and delta==ticks+warmup,'Failed/incomplete run retained, no automatic retry'
 if phase!='qualification':assert row['measured_ticks']==ticks
 assert result['compute_telemetry']['mode']=='off'
 if a['id'] in ['class_off','class_on']:
  assert result['research_class_gate']==(a['id']=='class_on') and result['research_class_diagnostics']==report
 if a['id'] in ['adaptive16','fixed8','fixed32','fixed128']:assert result['adaptive_gradient_policy']['research_fixed_interval_ticks']==a['fixed_interval']
 return row
try:
 save();deadline=time.time()+7200
 while True:
  mf=B/'master-class-runtime/BUILD.json';mg=pathlib.Path(P['modern_qualification_gate']);fail=B/'master-class-runtime/BUILD-FAILURE.json'
  if fail.exists():raise RuntimeError('Master build failure: '+fail.read_text())
  if mf.exists() and mg.exists():
   master=json.loads(mf.read_text());modern=json.loads(mg.read_text())
   if master['status']=='PASS' and modern['status']=='PASS':break
  if time.time()>deadline:raise RuntimeError('Build gates absent after2h')
  time.sleep(5)
 assert sha(mg)==P['modern_qualification_sha256'];assert master['source']['patch_sha256']==P['master_patch_sha256'];assert modern['patch_sha256']==P['modern_patch_sha256']
 review=json.loads((B/'ROOT-REVIEW.json').read_text());assert review['master_patch']==P['master_patch_sha256'] and review['modern_patch']==P['modern_patch_sha256']
 arms={a['id']:dict(a) for a in P['arms']}
 for name in ['class_off','class_on']:arms[name]['binary_sha256']=master['binary_sha256'];assert arms[name]['binary']==master['binary']
 for a in arms.values():assert sha(a['binary'])==a['binary_sha256']
 assert sha(P['inputs_manifest'])==P['inputs_manifest_sha256']
 for m in P['maps']:assert sha(m['warm_path'])==m['warm_sha256'] and sha(m['map_path'])==m['map_sha256']
 processes();dependency_checks={a:dependencies(v['binary']) for a,v in arms.items()};write('DEPENDENCIES.json',dependency_checks);ref=dependency_checks['master_reference'];assert all(v==ref for v in dependency_checks.values()),'Nonmatching dependency content'
 assets=tree(pathlib.Path(P['master_data_dir']));write('ASSETS.json',assets);write('BOUND-ARMS.json',arms)
 write('LAUNCH.json',dict(utc=now(),pid=os.getpid(),plan_sha256=sha(B/'PLAN.json'),master_build_gate_sha256=sha(mf),modern_gate_sha256=sha(mg),initial_context=context(),hardware=subprocess.check_output(['sysctl','-n','hw.model','hw.ncpu','hw.physicalcpu','hw.logicalcpu'],text=True)))
 state['status']='QUALIFYING-OFF-CONTROLS';save();qi=0
 for m in P['maps']:
  for reference,control in [('master_reference','class_off'),('gen005_reference','adaptive16')]:
   pairs=[]
   for a in [reference,control]:pairs.append(run(dict(index=qi,phase='qualification',group=m['group'],arm=a,repeat=0),P['qualification_ticks'],0,trace=True,final=True));qi+=1
   assert pairs[0]['trace_hashes'] and pairs[0]['trace_hashes']==pairs[1]['trace_hashes'] and pairs[0]['final_save_sha256']==pairs[1]['final_save_sha256'],'Research off/default differs from immutable reference'
   assert pairs[0]['result']['initialChecksum']==pairs[1]['result']['initialChecksum'] and pairs[0]['result']['finalChecksum']==pairs[1]['result']['finalChecksum']
 write('OFF-QUALIFICATION.json',dict(status='PASS',pairs=4,per_tick_world_snapshot_and_available_sidecars_exact=True,final_save_bytes_exact=True))
 state['status']='CPU';save()
 for cell in P['cpu_schedule']:run(cell,P['cpu_measured_ticks'],P['benchmark_warmup'])
 cpu=[r for r in state['rows'] if r['phase']=='cpu']
 for m in P['maps']:
  for a in arms:
   rr=[r for r in cpu if r['group']==m['group'] and r['arm']==a];assert len(rr)==4
   values={(r['result']['initialChecksum'],r['result']['finalChecksum'],r['advanced_ticks'],r['measured_ticks']) for r in rr};assert len(values)==1,'Repeated arm nondeterminism'
  for ref,control in [('master_reference','class_off'),('gen005_reference','adaptive16')]:
   rr=[r for r in cpu if r['group']==m['group'] and r['arm'] in [ref,control]];assert len({(r['result']['initialChecksum'],r['result']['finalChecksum']) for r in rr})==1
 write('CPU-PARITY.json',dict(status='PASS',runs=64,within_arm_repeats_exact=True,research_default_vs_immutable_exact=True))
 subprocess.run(['python3',str(B/'summarize-attribution.py'),'--cpu-only'],cwd=B,check=True)
 state['status']='SEPARATE-QUALITY';save()
 for cell in P['quality_schedule']:
  row=run(cell,P['cpu_measured_ticks'],P['benchmark_warmup'],probe=True)
  ref=next(r for r in cpu if r['group']==row['group'] and r['arm']==row['arm']);assert (row['result']['initialChecksum'],row['result']['finalChecksum'])==(ref['result']['initialChecksum'],ref['result']['finalChecksum']),'ON observer changed final results'
 state['status']='CLASS-DIAGNOSTIC';save()
 for cell in P['class_diagnostic_schedule']:run(cell,P['class_diagnostic_measured_ticks'],P['benchmark_warmup'],report=True)
 assert tree(pathlib.Path(P['master_data_dir']))==assets,'Data changed during run';assert {a:dependencies(v['binary']) for a,v in arms.items()}==dependency_checks,'Dependency content changed'
 state.update(status='COMPLETE',finished_utc=now());save();subprocess.run(['python3',str(B/'summarize-attribution.py')],cwd=B,check=True)
except Exception:
 state.update(status='FAILED-REQUIRES-REVIEW',error=traceback.format_exc());save();write('FAILURE.json',dict(utc=now(),error=state['error']));raise
