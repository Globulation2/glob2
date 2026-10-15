#!/usr/bin/env python3
"""Linux correctness-only resumer. Root handoff required; never measures performance."""
from pathlib import Path
import concurrent.futures,datetime,fcntl,hashlib,json,mmap,os,signal,struct,subprocess,threading,time
from process_cleanup import kill_owned_tree,process_identity
P=Path(__file__).resolve().parent
MASKS=[[0,1,2,16,17,18],[3,4,5,19,20,21],[6,7,8,22,23,24],[9,10,11,25,26,27]]
LEGACY=[12,13,14,28,29,30]
BUDGET=18*3600

def sha(path):
 with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(path):return json.loads(Path(path).read_text())
def save(path,value):
 path=Path(path);temp=path.with_name(path.name+'.tmp-'+str(threading.get_ident()));temp.write_text(json.dumps(value,indent=2)+'\n');temp.replace(path)
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def identity(pid):
 found=process_identity(pid);return found[1] if found else None
def alive(record):
 if identity(record['pid'])!=str(record['starttime']):return False
 try:return (Path('/proc')/str(record['pid'])/'stat').read_text().split(') ',1)[1].split()[0]!='Z'
 except OSError:return False
def campaign_gate():
 gate=read(P/'timing-gate.json');assert gate['state']=='stable_boundary_ready_for_coordination_timing'
 pid=gate['old_campaign_runner_pid'];proc=Path('/proc')/str(pid)
 assert b'expanded-runner.py' in (proc/'cmdline').read_bytes()
 assert (proc/'stat').read_text().split(') ',1)[1].split()[0]=='T','Old campaign is not paused'
 return gate

def command(case,variant,out,allocation):
 return ['taskset','-c',','.join(map(str,allocation)),variant['binary'],'game','run',*case['args'],'--write-replay','--telemetry','checksums','--output-dir',str(out/'native')]
def overrides(case,variant,out):
 env=dict(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(out/'profile'),GLOB2_USER_DATA_DIR=str(out/'profile'));env.update(variant['env']);env.update(case['env']);return env
def fixtures(case):
 for path,digest in case['fixture_sha256'].items():assert sha(path)==digest,('fixture changed',path)
def sidecars(native,case):
 files=sorted(native.glob('*.checksums'));assert len(files)==2,('Expected exactly two sidecars',native)
 checks={};details={};start=case['initial_tick'];count=case['measured_ticks']
 for f in files:
  with f.open('rb') as stream:magic=stream.read(4)
  if magic==b'GCS1':
   with f.open('rb') as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as data:
    sig,teams,players,rows,flags=struct.unpack_from('<4sIIII',data);assert 1<=teams<=32 and 1<=players<=32 and rows==count and flags==0
    pos=20
    def u32():
     nonlocal pos
     assert pos+4<=len(data);n=struct.unpack_from('<I',data,pos)[0];pos+=4;return n
    for tick in range(start,start+count):
     assert u32()==tick,('GCS tick',f,tick);u32()
     for _ in range(teams):
      u32()
      for kind in range(2):
       entities=u32();assert entities<=65536
       for _ in range(entities):
        assert pos+10<=len(data);pos+=6;words=u32();assert words<1000000;pos+=words*4;assert pos<=len(data)
    assert pos==len(data),('GCS trailing/truncated bytes',f)
   fmt='GCS1';details[fmt]={'path':str(f),'rows':rows,'first_tick':start,'last_tick':start+count-1,'teams':teams,'players':players,'bytes':f.stat().st_size}
  else:
   rows=0
   with f.open() as stream:
    for expected,line in enumerate(stream,start):
     parts=line.split();assert len(parts)==2 and all(x.isdigit() for x in parts);assert int(parts[0])==expected;assert int(parts[1])<=0xffffffff;rows+=1
   assert rows==count;fmt='native';details[fmt]={'path':str(f),'rows':rows,'first_tick':start,'last_tick':start+count-1,'bytes':f.stat().st_size}
  assert fmt not in checks;checks[fmt]=sha(f)
 assert set(checks)=={'GCS1','native'}
 return checks,details

def validate(case,variant,out,allowed_mask=None):
 r=read(out/'receipt.json');assert r['role']==variant['role'] and r['case']==case and r['variant_env']==variant['env']
 assert r['binary_sha256']==variant['sha256']==sha(variant['binary']);fixtures(case)
 cmd=r['command'];assert len(cmd)>3;mask=[int(x) for x in cmd[2].split(',')];assert mask in [LEGACY,*MASKS]
 if allowed_mask is not None:assert mask==allowed_mask
 assert cmd==command(case,variant,out,mask)
 if 'cwd' in r:assert r['cwd']==variant['cwd']
 if 'env_overrides' in r:assert r['env_overrides']==overrides(case,variant,out)
 if 'returncode' in r:assert r['returncode']==0
 n=read(out/'native/result.json');assert n['status']=='completed' and n['ticks']==case['initial_tick']+case['measured_ticks']
 assert n['initialChecksum']==r['initialChecksum'] and n['finalChecksum']==r['finalChecksum']
 checks,details=sidecars(out/'native',case);assert checks==r['checksums']
 if 'result_sha256' in r:assert r['result_sha256']==sha(out/'native/result.json')
 if 'sidecar_details' in r:assert r['sidecar_details']==details
 return dict(role=variant['role'],initialChecksum=n['initialChecksum'],finalChecksum=n['finalChecksum'],checksums=checks,allocation=mask,receipt_sha256=sha(out/'receipt.json'),result_sha256=sha(out/'native/result.json'),sidecar_details=details)

def main():
 lockfile=(P/'parallel-parity.lock').open('a+');fcntl.flock(lockfile,fcntl.LOCK_EX|fcntl.LOCK_NB)
 review=read(P/'parallel-parity-review.json');assert review['approved'] is True and review['source_sha256']==sha(__file__)
 handoff=read(P/'parity-handoff.json');assert handoff['state']=='ready_for_parallel_recovery' and handoff['all_native_games_finished_naturally'] is True and handoff['no_measured_run_interrupted'] is True
 assert handoff['parallel_parity_sha256']==sha(__file__)
 for item in handoff['bound_files']:assert sha(item['path'])==item['sha256']
 bound={str(Path(item['path']).resolve()):item['sha256'] for item in handoff['bound_files']}
 for name in ['correctness-cases.json','native-pilot-manifest.json']:
  assert bound[str((P/name).resolve())]==spec_hashes[name],('Unbound parity input',name)
 for record in [handoff['controller'],handoff['pilot']]:assert not alive(record),('Previous wrapper still active',record)
 for record in handoff.get('native_children',[]):assert not alive(record),('Old native child still active',record)
 gate=campaign_gate();assert gate['old_campaign_runner_pid']==handoff['campaign']['pid'] and identity(handoff['campaign']['pid'])==str(handoff['campaign']['starttime']);casespec=read(P/'correctness-cases.json');native=read(P/'native-pilot-manifest.json');variants=casespec['variants'];cases=casespec['cases'];assert variants==native['variants'] and len(variants)==5 and len({v['role'] for v in variants})==5
 assert len(cases)==20 and len({c['id'] for c in cases})==20
 for v in variants:assert sha(v['binary'])==v['sha256']
 for c in cases:fixtures(c)
 # Independent handoff check: refuse live native binaries or old serial pilot.
 binaries={v['binary'] for v in variants}
 for proc in Path('/proc').glob('[0-9]*'):
  try:args=[x.decode() for x in (proc/'cmdline').read_bytes().split(b'\0') if x]
  except (OSError,UnicodeError):continue
  if int(proc.name)==os.getpid():continue
  assert not any(arg in binaries for arg in args),('Native process still present',proc.name,args)
  assert not any(Path(arg).name=='pilot.py' for arg in args),('Serial pilot still present',proc.name)
 claims=P/'parallel-parity-claims';claims.mkdir(exist_ok=True)
 controller={'pid':os.getpid(),'starttime':identity(os.getpid()),'script_sha256':sha(__file__),'started_utc':utc()}
 activefile=P/'parallel-parity-active.json'
 if activefile.exists():assert not alive(read(activefile)), 'Previous active controller identity remains live'
 save(activefile,dict(controller,state='running'))
 deadline=time.monotonic()+BUDGET;stop=threading.Event();mu=threading.Lock();children={};groups=[];allocations={};audits=[];pending=list(cases);gpu_busy=False
 def checkpoint():
  save(P/'parallel-parity-progress.json',dict(state='running',controller=controller,completed_cases=len(groups),completed_runs=len(audits),total_runs=len(cases)*len(variants),groups=groups,per_case_allocations=allocations,active_children=list(children.values()),updated_utc=utc(),correctness_only=True))
 def aborted(*_):stop.set()
 signal.signal(signal.SIGTERM,aborted);signal.signal(signal.SIGINT,aborted)
 def run_role(case,v,mask):
  assert not stop.is_set();assert time.monotonic()<deadline;campaign_gate();fixtures(case);assert sha(v['binary'])==v['sha256']
  out=P/'parity'/case['id']/v['role'];claim=claims/(case['id']+'--'+v['role']+'.json')
  if claim.exists():
   old=read(claim)
   for key in ['controller','child']:
    if old.get(key):assert not alive(old[key]),('Active role claim',claim,old[key])
  if (out/'receipt.json').exists():
   audit=validate(case,v,out)
   with mu:audits.append(dict(case=case['id'],reused=True,**audit));allocations.setdefault(case['id'],{})[v['role']]=audit['allocation'];checkpoint()
   return audit
  if out.exists():
   recovery=P/'parallel-parity-recovery'/case['id'];recovery.mkdir(parents=True,exist_ok=True);target=recovery/(v['role']+'-'+str(time.time_ns()));out.rename(target)
  out.mkdir(parents=True);cmd=command(case,v,out,mask);env={k:value for k,value in os.environ.items() if not k.startswith('GLOB2_')};env.update(overrides(case,v,out))
  state=dict(controller=controller,case=case['id'],role=v['role'],command=cmd,cwd=v['cwd'],allocation=mask,created_utc=utc());save(claim,state)
  child=None;finished=False
  try:
   with (out/'stdout.log').open('w') as log:
    child=subprocess.Popen(cmd,cwd=v['cwd'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    state['child']={'pid':child.pid,'starttime':identity(child.pid)};save(claim,state)
    with mu:children[child.pid]=state;checkpoint()
    started=time.monotonic()
    while child.poll() is None:
     if stop.is_set() or time.monotonic()>=deadline or time.monotonic()-started>600:raise TimeoutError(('Native stopped/timedout',case['id'],v['role']))
     time.sleep(.2)
    assert child.returncode==0,('Native failed',case['id'],v['role'],child.returncode)
   n=read(out/'native/result.json');assert n['status']=='completed' and n['ticks']==case['initial_tick']+case['measured_ticks'];checks,details=sidecars(out/'native',case);fixtures(case);assert sha(v['binary'])==v['sha256']
   r=dict(role=v['role'],case=case,command=cmd,cwd=v['cwd'],variant_env=v['env'],env_overrides=overrides(case,v,out),binary_sha256=v['sha256'],initialChecksum=n['initialChecksum'],finalChecksum=n['finalChecksum'],checksums=checks,sidecar_details=details,result_sha256=sha(out/'native/result.json'),returncode=child.returncode,allocation=mask,correctness_only=True,controller=controller,finished_utc=utc())
   assert not (out/'receipt.json').exists();save(out/'receipt.json',r);audit=validate(case,v,out,mask);finished=True
   with mu:audits.append(dict(case=case['id'],reused=False,**audit));allocations.setdefault(case['id'],{})[v['role']]=mask;checkpoint()
   return audit
  finally:
   if child is not None:
    if child.poll() is None:kill_owned_tree(child.pid,out/'cleanup.json');child.wait()
    with mu:children.pop(child.pid,None);checkpoint()
   state['state']='validated' if finished else 'incomplete';save(claim,state)
 def worker(index):
  nonlocal gpu_busy
  while not stop.is_set():
   with mu:
    selected=None
    # Admit one OpenCL CASE at a time; other workers continue CPU queue.
    for i,c in enumerate(pending):
     gpu='opencl' in c['args']
     if not gpu or not gpu_busy:selected=pending.pop(i);gpu_busy=gpu_busy or gpu;break
    if selected is None:return
   gpu='opencl' in selected['args']
   try:
    results=[run_role(selected,v,MASKS[index]) for v in variants]
    assert len({r['initialChecksum'] for r in results})==len({r['finalChecksum'] for r in results})==1,('Checksum endpoints differ',selected['id'])
    assert all(r['checksums']==results[0]['checksums'] for r in results),('Per-tick sidecars differ',selected['id'])
    with mu:groups.append({'case':selected['id'],'matched_roles':len(results)});checkpoint()
   except BaseException:stop.set();raise
   finally:
    if gpu:
     with mu:gpu_busy=False
 try:
  with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
   futures=[pool.submit(worker,i) for i in range(4)]
   for f in concurrent.futures.as_completed(futures):f.result()
  assert not stop.is_set() and len(groups)==len(cases) and not pending
  # Revalidate all persisted evidence before publishing the timing gate.
  for c in cases:
   for v in variants:validate(c,v,P/'parity'/c['id']/v['role'])
  assert sha(P/'correctness-cases.json')==spec_hashes['correctness-cases.json'] and sha(P/'native-pilot-manifest.json')==spec_hashes['native-pilot-manifest.json']
  save(P/'parallel-parity-validation.json',dict(controller=controller,inputs=spec_hashes,audits=audits,correctness_only=True))
  save(P/'parity-complete.json',dict(state='verified',groups=sorted(groups,key=lambda g:g['case']),runs=len(cases)*len(variants),binary_hashes={v['role']:v['sha256'] for v in variants},per_case_allocations=allocations,controller=controller,validation=str(P/'parallel-parity-validation.json'),correctness_only=True))
  save(activefile,dict(controller,state='complete',finished_utc=utc()))
 except BaseException:
  stop.set()
  # Worker finally blocks own and reap all independently-sessioned children.
  save(activefile,dict(controller,state='failed',finished_utc=utc()));raise

if __name__=='__main__':
 spec_hashes={name:sha(P/name) for name in ['correctness-cases.json','native-pilot-manifest.json','timing-gate.json','parity-handoff.json']}
 main()
