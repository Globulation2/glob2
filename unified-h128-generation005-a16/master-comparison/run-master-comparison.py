"""Frozen CPU-only master versus gen005 comparison; no engine modifications."""
import collections,datetime,fcntl,hashlib,json,os,pathlib,shutil,signal,statistics,subprocess,time,traceback
B=pathlib.Path(__file__).resolve().parent;D=B;P=json.loads((B/'PLAN.json').read_text())
sha=lambda p:hashlib.file_digest(pathlib.Path(p).open('rb'),'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(n,v):
 p=B/n;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(v,indent=2)+'\n');t.replace(p)
lock=(B/'comparison.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
exec((B/'mac-observation-helpers.py').read_text())
assert sha(B/'PLAN.json')==(B/'PLAN.sha256').read_text().split()[0]
assert not (B/'LAUNCH.json').exists(),'Do not automatically repeat an interrupted experiment'
state=dict(status='PREPARING-COMMON-INPUTS',plan_sha256=sha(B/'PLAN.json'),rows=[],inputs=[],started_utc=now())
def save():state['updated_utc']=now();write('STATUS.json',state)
def execute(name,argv,cwd,mode,cell=None):
 out=B/'runs'/name;out.mkdir(parents=True,exist_ok=False);env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',GLOB2_ASSET_DIR=P['data_dir'],GLOB2_USER_DATA_DIR=str(out/'profile'))
 assert all(x not in argv for x in ['--telemetry','--resource-gradient-probe','--gradient-work-stats'])
 write(f'runs/{name}/COMMAND.json',dict(argv=argv,cwd=str(cwd),environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','GLOB2_ASSET_DIR','GLOB2_USER_DATA_DIR']},context_before=context(),phase=mode))
 start=time.monotonic();capped=False
 with (out/'stdout.log').open('w') as so,(out/'stderr.log').open('w') as se:
  child=subprocess.Popen(argv,cwd=cwd,env=env,stdout=so,stderr=se,start_new_session=True);state['active']=dict(name=name,pid=child.pid,phase=mode,utc=now());save()
  while True:
   done,status,usage=os.wait4(child.pid,os.WNOHANG)
   if done:break
   if time.monotonic()-start>P['wall_cap_seconds']:
    capped=True;os.killpg(child.pid,signal.SIGKILL);_,status,usage=os.wait4(child.pid,0);break
   time.sleep(.1)
  child.returncode=os.waitstatus_to_exitcode(status)
 wall=time.monotonic()-start;result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else None
 row=dict(name=name,phase=mode,returncode=child.returncode,capped=capped,user_cpu_seconds=usage.ru_utime,system_cpu_seconds=usage.ru_stime,total_process_cpu_seconds=usage.ru_utime+usage.ru_stime,wall_seconds=wall,result=result,output=str(out),context_after=context())
 if cell:row.update(cell)
 write(f'runs/{name}/MEASUREMENT.json',row);state.pop('active',None);save()
 assert child.returncode==0 and not capped,f'{mode} failed: {name}; no automatic retry'
 return row
try:
 build=json.loads((B/'BUILD.json').read_text());assert build['status']=='PASS' and build['source']==P['master_source'];assert sha(build['binary'])==build['binary_sha256'];assert sha(P['candidate_binary'])==P['candidate_binary_sha256']
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=P['master_working_directory'],text=True).strip()==P['master_source']
 assert not subprocess.check_output(['git','status','--porcelain'],cwd=P['master_working_directory'],text=True)
 processes();(B/'bin').mkdir(exist_ok=True);master=B/'bin/glob2-master-84edb385c'
 if not master.exists():shutil.copy2(build['binary'],master)
 assert sha(master)==build['binary_sha256']
 binaries={'master':str(master),'gen005':P['candidate_binary']};binary_hashes={a:sha(p) for a,p in binaries.items()}
 dependency_checks={a:dependencies(p) for a,p in binaries.items()};write('DEPENDENCIES.json',dependency_checks);assert dependency_checks['master']==dependency_checks['gen005'],'Different dependency load commands/content'
 assets=tree(pathlib.Path(P['data_dir']));write('ASSETS.json',assets)
 assert subprocess.check_output(['git','rev-parse',P['master_source']+':data'],cwd=P['master_working_directory'],text=True).strip()==P['data_tree_git_oid']
 subprocess.run(['git','diff','--exit-code',P['master_source'],'--','data'],cwd=P['master_working_directory'],check=True,stdout=subprocess.PIPE)
 write('LAUNCH.json',dict(utc=now(),supervisor_pid=os.getpid(),plan_sha256=sha(B/'PLAN.json'),binary_sha256=binary_hashes,initial_context=context(),hardware=subprocess.check_output(['sysctl','-n','hw.model','hw.ncpu','hw.physicalcpu','hw.logicalcpu'],text=True),sources={'master':P['master_source'],'gen005':P['candidate_source']}));save()
 assert sha(P['common_inputs_path'])==P['common_inputs_sha256']
 common=json.loads(pathlib.Path(P['common_inputs_path']).read_text());state['inputs']=common['inputs']
 for m in state['inputs']:
  assert sha(m['map_path'])==m['map_sha256'] and sha(m['warm_path'])==m['warm_sha256']
 write('COMMON-INPUTS.json',dict(original_path=P['common_inputs_path'],original_sha256=P['common_inputs_sha256'],inputs=state['inputs'],bound_before_measurements=True,utc=now()))
 state['status']='MEASURING-CPU';save()
 for cell in P['schedule']:
  m=next(m for m in state['inputs'] if m['group']==cell['group'])
  if not m['usable']:continue
  assert sha(m['warm_path'])==m['warm_sha256'];a=cell['arm'];name=f"{cell['index']:02d}-{m['group']}-{a}-r{cell['repeat']}";out=B/'runs'/name
  argv=[binaries[a],'game','run','--load-game',m['warm_path'],'--ticks',str(P['absolute_tick_limit']),'--output-dir',str(out),'--profile',str(out/'profile'),'--benchmark-warmup',str(P['benchmark_warmup_ticks']),'--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',P['data_dir']]
  row=execute(name,argv,P['master_working_directory'],'measurement',cell);r=row['result'];delta=r['ticks']-m['start_tick'] if r else None
  row.update(format=m['format'],advanced_ticks=delta,measured_ticks=r['benchmark_measured_ticks'] if r else None,warm_sha256=m['warm_sha256'],binary_sha256=binary_hashes[a]);state['rows'].append(row);save()
  assert r and delta==P['requested_advancing_ticks'] and r['benchmark_measured_ticks']==P['measured_ticks'],'Nonmatched timing horizon; retained for review'
  assert r['compute_telemetry']['mode']=='off'
  row.update(simulation_cpu_ms_per_measured_tick=r['benchmark_run_cpu_ns']/1e6/r['benchmark_measured_ticks'],total_simulation_cpu_seconds=r['benchmark_run_cpu_ns']/1e9,wall_tps=delta/row['wall_seconds']);write(f'runs/{name}/MEASUREMENT.json',row);save()
 for m in state['inputs']:
  if not m['usable']:continue
  for a in binaries:
   rr=[r for r in state['rows'] if r['group']==m['group'] and r['arm']==a];assert len(rr)==4
   values={(r['result']['initialChecksum'],r['result']['finalChecksum'],r['advanced_ticks'],r['measured_ticks']) for r in rr};assert len(values)==1,'Within-arm repeat nondeterminism'
 assert tree(pathlib.Path(P['data_dir']))==assets,'Data changed during comparison';assert {a:dependencies(p) for a,p in binaries.items()}==dependency_checks,'Dependencies changed during comparison'
 write('REPEAT-PARITY.json',dict(status='PASS',scope='Within-arm initial/final result checksums and actual/measured ticks; no cross-arm equality assertion',usable_states=sum(m['usable'] for m in state['inputs'])))
 state.update(status='COMPLETE',finished_utc=now());save()
 subprocess.run(['python3',str(B/'summarize-master.py')],cwd=B,check=True)
except Exception:
 state['status']='FAILED-REQUIRES-REVIEW';state['error']=traceback.format_exc();save();write('FAILURE.json',dict(utc=now(),error=state['error']));raise
