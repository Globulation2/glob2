#!/usr/bin/env python3
"""Artifact-only frozen generation comparison. Explicit qualified launch required."""
import collections,csv,datetime,fcntl,hashlib,json,os,pathlib,signal,subprocess,time,traceback
D=pathlib.Path(__file__).resolve().parent
P=json.loads((D/'PLAN.json').read_text())
sha=lambda p:hashlib.file_digest(pathlib.Path(p).open('rb'),'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):
 t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(v,indent=2)+'\n');t.replace(p)
lock=(D/'comparison.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
assert sha(D/'PLAN.json')==(D/'PLAN.sha256').read_text().split()[0]
assert not (D/'LAUNCH.json').exists(),'One attempt only; review previous artifacts before any retry'
gate=json.loads((D/'LAUNCH-GATE.json').read_text());assert gate['status']=='PASS' and gate['builds_and_proofs_terminal']
assert gate['plan_sha256']==sha(D/'PLAN.json')

def processes():
 lines=subprocess.check_output(['ps','-axo','pid=,pcpu=,comm=,args='],text=True).splitlines();found=[]
 for line in lines:
  parts=line.strip().split(None,3)
  if len(parts)<3:continue
  pid,cpu,command=parts[:3];exe=pathlib.Path(command).name
  assert exe not in {'glob2','clang','clang++','cc1','cc1plus','ld','ld64','scons'},'Competing game/build: '+line
  if exe.startswith('python') and len(parts)>3:
   assert not any('qualify' in x and x.endswith('.py') for x in parts[3].split()),'Competing qualification: '+line
  found.append(dict(pid=int(pid),cpu_percent=cpu,command=command))
 return found
def context():
 return dict(utc=now(),loadavg=os.getloadavg(),processes=processes(),thermal=subprocess.run(['pmset','-g','therm'],text=True,capture_output=True).stdout,power=subprocess.run(['pmset','-g','batt'],text=True,capture_output=True).stdout)
def dependencies(binary):
 direct=subprocess.check_output(['otool','-L',str(binary)],text=True).splitlines()[1:];queue=[(line.strip().split(' (')[0],pathlib.Path(binary)) for line in direct];files={};system=[]
 while queue:
  name,loader=queue.pop()
  if name.startswith('@loader_path/'):
   name=str(loader.parent/name.removeprefix('@loader_path/'))
  elif name.startswith('@rpath/'):
   commands=subprocess.check_output(['otool','-l',str(loader)],text=True).splitlines();rpaths=[]
   for i,line in enumerate(commands):
    if line.strip()=='cmd LC_RPATH':
     rpath=commands[i+2].strip().split(' (offset')[0].removeprefix('path ').replace('@loader_path',str(loader.parent)).replace('@executable_path',str(pathlib.Path(binary).parent));rpaths.append(pathlib.Path(rpath)/name.removeprefix('@rpath/'))
   matches=[p for p in rpaths if p.is_file()];assert matches,'Unresolved rpath dependency '+name+' from '+str(loader);name=str(matches[0])
  if name in files or name in system:continue
  path=pathlib.Path(name)
  if name.startswith('/usr/lib/') or name.startswith('/System/'):
   system.append(name);continue
  assert path.is_file(),'Unresolved non-system dependency: '+name
  files[name]=dict(resolved_path=str(path.resolve()),sha256=sha(path))
  queue += [(line.strip().split(' (')[0],path) for line in subprocess.check_output(['otool','-L',name],text=True).splitlines()[1:] if line.strip().split(' (')[0]!=name]
 return dict(direct_commands=direct,non_system_dylibs=files,dyld_shared_cache_paths=sorted(system))
def tree(root):return {str(p.relative_to(root)):sha(p) for p in root.rglob('*') if p.is_file()}
try:
 for arm,a in P['arms'].items():assert sha(a['binary'])==gate['binary_sha256'][arm]
 dependency_checks={arm:dependencies(a['binary']) for arm,a in P['arms'].items()}
 write(D/'DEPENDENCIES.json',dependency_checks)
 assert dependency_checks['gen004']==dependency_checks['gen005'],'Different dylib load commands/content'
 asset_root=pathlib.Path(P['data_dir']);asset_manifest=tree(asset_root);assert asset_manifest
 write(D/'ASSETS.json',asset_manifest)
 assert subprocess.check_output(['git','rev-parse','e1cac289914889cb7f49a6b9fa1268aae55eaf7d:data'],cwd=P['working_directory'],text=True).strip()=='23def6961cd5dbffdc49b1f3f0a4f1c9d4bcdef0'
 subprocess.run(['git','diff','--exit-code','e1cac289914889cb7f49a6b9fa1268aae55eaf7d','--','data'],cwd=P['working_directory'],check=True,stdout=subprocess.PIPE)
 for m in P['maps']:assert sha(m['local_path'])==m['sha256']
 write(D/'LAUNCH.json',dict(utc=now(),pid=os.getpid(),plan_sha256=sha(D/'PLAN.json'),gate_sha256=sha(D/'LAUNCH-GATE.json'),initial_context=context(),host=subprocess.check_output(['uname','-a'],text=True).strip(),hardware=subprocess.check_output(['sysctl','-n','hw.model','hw.ncpu','hw.physicalcpu','hw.logicalcpu'],text=True)))
 state=dict(status='RUNNING',rows=[],plan_sha256=sha(D/'PLAN.json'))
 for cell in P['schedule']:
  m=next(m for m in P['maps'] if m['group']==cell['group']);a=P['arms'][cell['arm']];runtime=pathlib.Path(P['working_directory'])
  name=f"{cell['index']:02d}-{cell['kind']}-{cell['group']}-{cell['arm']}-r{cell['repeat']}";out=D/'runs'/name;out.mkdir(parents=True,exist_ok=False)
  argv=[a['binary'],'game','run','--load-game',m['local_path'],'--ticks','24576','--output-dir',str(out),'--profile',str(out/'profile'),'--benchmark-warmup','0','--gpu','off','--compute-threads','1','--compute-telemetry','off','--data-dir',P['data_dir']]
  if cell['kind']=='on':argv+=['--telemetry','checksums','--resource-gradient-probe','--gradient-work-stats',str(out/'gradient-work.csv')]
  env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1')
  before=context();write(out/'COMMAND.json',dict(argv=argv,cwd=str(runtime),environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER']},context_before=before,checkpoint_sha256=sha(m['local_path']),binary_sha256=sha(a['binary'])))
  start=time.monotonic();capped=False
  with (out/'stdout.log').open('w') as so,(out/'stderr.log').open('w') as se:
   child=subprocess.Popen(argv,cwd=runtime,env=env,stdout=so,stderr=se,start_new_session=True);state['active']=dict(name=name,pid=child.pid,utc=now());write(D/'STATUS.json',state)
   while True:
    done,status,usage=os.wait4(child.pid,os.WNOHANG)
    if done:break
    if time.monotonic()-start>=P['wall_cap_seconds']:
     capped=True;os.killpg(child.pid,signal.SIGKILL);_,status,usage=os.wait4(child.pid,0);break
    time.sleep(.2)
   child.returncode=os.waitstatus_to_exitcode(status)
  wall=time.monotonic()-start;result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else None
  delta=result['ticks']-m['start_tick'] if result else None;cpu=usage.ru_utime+usage.ru_stime
  row=dict(**cell,name=name,format=m['format'],output=str(out),returncode=child.returncode,capped=capped,wall_seconds=wall,user_cpu_seconds=usage.ru_utime,system_cpu_seconds=usage.ru_stime,total_cpu_seconds=cpu,ticks=delta,result=result,context_after=context(),checkpoint_sha256=m['sha256'],binary_sha256=sha(a['binary']))
  if delta and delta>0:row.update(process_cpu_ms_per_tick=cpu*1000/delta,wall_tps=delta/wall,benchmark_cpu_ms_per_tick=result['benchmark_run_cpu_ns']/1e6/delta)
  row['checksums']={p.name:sha(p) for p in out.glob('*.checksums')};row['artifact_hashes']={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name not in {'stdout.log','stderr.log','progress.jsonl'}}
  write(out/'MEASUREMENT.json',row);state['rows'].append(row);state.pop('active',None);write(D/'STATUS.json',state)
  assert child.returncode==0 and not capped and result and delta==8192 and result['benchmark_measured_ticks']==8192,'Incomplete/failing cell retained; no automatic retry'
  assert result['compute_telemetry']['mode']=='off'
  assert result['adaptive_gradient_policy']['threshold_milli_direction_cost']==16000
  assert cell['kind']=='off' or row['checksums'],'ON deterministic sidecars absent'
 parity=[]
 for m in P['maps']:
  for arm in P['arms']:
   rows=[r for r in state['rows'] if r['group']==m['group'] and r['arm']==arm];assert len(rows)==5
   assert all((r['result']['initialChecksum'],r['result']['finalChecksum'],r['ticks'])==(rows[0]['result']['initialChecksum'],rows[0]['result']['finalChecksum'],rows[0]['ticks']) for r in rows),'Within-arm repeat/observer mismatch'
   parity.append(dict(group=m['group'],arm=arm,repeats=4,observer_on=1,initialChecksum=rows[0]['result']['initialChecksum'],finalChecksum=rows[0]['result']['finalChecksum'],ticks=rows[0]['ticks'],gate='final results only; full per-tick parity qualified separately'))
 write(D/'PARITY.json',dict(status='PASS',checks=parity,cross_arm_equality_required=False))
 assert tree(pathlib.Path(P['data_dir']))==asset_manifest,'Shared data changed during study'
 assert {arm:dependencies(a['binary']) for arm,a in P['arms'].items()}==dependency_checks,'Dependency content changed during study'
 state.update(status='COMPLETE',finished_utc=now(),runs=len(state['rows']));write(D/'STATUS.json',state)
except Exception:
 write(D/'FAILURE.json',dict(status='FAILED-REQUIRES-REVIEW',utc=now(),error=traceback.format_exc()));raise
