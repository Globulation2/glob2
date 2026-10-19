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
assert not (B/'CLASS-FULL-STATUS.json').exists(),'Never rerun full diagnostic automatically'
state=dict(status='WAITING-BUILD-GATES',plan_sha256=sha(B/'PLAN.json'),rows=[],qualification=[],started_utc=now())
def save():state['updated_utc']=now();write('CLASS-FULL-STATUS.json',state)
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

F=json.loads((B/'CLASS-FULL-PLAN.json').read_text());arms=json.loads((B/'BOUND-ARMS.json').read_text());original=json.loads((B/'STATUS.json').read_text());assert original['status']=='COMPLETE'
try:
 state['status']='CLASS-FULL';save()
 for cell in F['schedule']:
  row=run(cell,F['ticks'],F['warmup'],report=True)
  ref=next(r for r in original['rows'] if r['phase']=='cpu' and r['group']==row['group'] and r['arm']==row['arm'])
  assert (row['result']['initialChecksum'],row['result']['finalChecksum'])==(ref['result']['initialChecksum'],ref['result']['finalChecksum']),'Full diagnostic changed results'
 state.update(status='COMPLETE',diagnostic_vs_cpu_final_checksums_exact=True);save()
except Exception:
 state.update(status='FAILED',error=traceback.format_exc());save();raise
