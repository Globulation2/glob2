import argparse,array,hashlib,json,os,platform,re,subprocess,sys,time,shutil
from pathlib import Path
ROOT=Path.cwd();OUT=ROOT/'artifacts/proactive-gradient/runs';BINARIES={m:ROOT/'artifacts/proactive-gradient/binaries'/m for m in ['baseline','proactive']}
MODES=['baseline','proactive'];PARTICIPANTS=[1,4];SEEDS=list(range(42,74))
WORKLOADS=[{'name':'holiday','map':'Holiday_Island_2.map.gz','players':['nicowar','econo','castor','warrush'],'ticks':32768}, {'name':'large-eleven','map':'Oazis.map.gz','players':(['nicowar','econo','castor','warrush']*3)[:11],'ticks':32768}]
p=argparse.ArgumentParser();p.add_argument('--pilot',action='store_true');p.add_argument('--smoke',action='store_true');p.add_argument('--workload',action='append');p.add_argument('--large-ticks',type=int,choices=[16384,32768],default=32768);a=p.parse_args()
WORKLOADS[1]['ticks']=a.large_ticks
if a.smoke:
 OUT=OUT/'smoke';WORKLOADS=[dict(w,ticks=2048) for w in WORKLOADS]
elif a.pilot:OUT=OUT/'pilot'
OUT.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
identity={'binaries':{m:{'path':str(b),'sha256':sha(b)} for m,b in BINARIES.items()},'tick_sample_format':'chronological native uint64 nanoseconds, '+sys.byteorder,'platform':platform.platform(),'architecture':platform.machine(),'policies':MODES,'participants':PARTICIPANTS,'seeds':SEEDS if not(a.smoke or a.pilot) else SEEDS[:1],'workloads':WORKLOADS,'building_admission':'Global four scheduled map refreshes per tick in both policies. Proactive initialization shares the one-tick hiring preparation deadline.','comparison':'Baseline: PR1197 c7f1cd299 primary-only hiring prewarm. Proactive: request shallow initialization after creation and construction footprint transitions; reuse those fields for primary hiring.','build_command':'PYTHONPATH=. scons release=1 CXXFLAGS=-g0 client_profile=full server=0; baseline initially -j8 and final incremental -j4; proactive -j4','instrumentation':'Identical private chronological tick writer and owner-only route-read counters; no prototype policy switches. Release O3; debug information disabled in both builds.','limitations':['Policies can change game evolution, so total CPU is not isolated kernel cost.','No graphics/frame pacing, other devices or cross-platform coverage.','Contention guard observes native compilers/tests, Docker execution and busy VMs; other host load is not fully controlled.'],'run_order':'Serial paired policies per seed/map, alternate policy and participant order; discard whole paired set upon observed build or VM contention.'}
for w in WORKLOADS:w['map_sha256']=sha(ROOT/'maps'/w['map'])
(OUT/'experiment.json').write_text(json.dumps(identity,indent=2))

def host_activity():
 activity=[]
 for line in subprocess.check_output(['ps','-axo','pid=,%cpu=,comm=,args='],text=True).splitlines():
  f=line.strip().split(None,3)
  if len(f)!=4:continue
  try:pid=int(f[0]);cpu=float(f[1])
  except ValueError:continue
  name=Path(f[2]).name;args=f[3];kind=None
  if name in {'clang','clang++','cc1plus','g++','c++','ld','ld-prime'}:kind='native-compiler-linker'
  elif name in {'Python','python','python3','python3.12','python3.14'} and '/scons ' in args:kind='native-build-driver'
  elif name in {'docker','com.docker.cli'} and (' exec ' in ' '+args+' ' or ' build ' in ' '+args+' '):kind='docker-exec-build-or-workload'
  elif ('VirtualMachine' in name or name.startswith('qemu')) and cpu>=10:kind='vm-cpu-at-least-10-percent'
  if name in {'glob2-engine-tests','glob2-unit-tests'}:kind='native-test-workload'
  if kind:activity.append({'pid':pid,'kind':kind,'cpu_percent':cpu})
 return activity

def wait_quiet():
 saw_busy=False;quiet_since=None;announced=False
 while True:
  activity=host_activity()
  if activity:
   if not announced:print('WAITING: host activity '+json.dumps(activity),flush=True);announced=True
   saw_busy=True;quiet_since=None;time.sleep(10)
  elif saw_busy:
   if quiet_since is None:quiet_since=time.monotonic()
   if time.monotonic()-quiet_since>=15:print('HOST QUIET',flush=True);return
   time.sleep(5)
  else:return

def run(w,mode,seed,n):
 BINARY=BINARIES[mode];binary_sha=identity['binaries'][mode]['sha256']
 path=OUT/w['name']/f'seed-{seed}-{mode}-p{n}';result=path/'game/result.json';path.mkdir(parents=True,exist_ok=True)
 if result.exists() and (path/'run.json').exists():
  old=json.loads((path/'run.json').read_text());assert old['binary_sha256']==binary_sha;assert old['command'][old['command'].index('--ticks')+1]==str(w['ticks'])
  if old.get('exit_code')==0:return json.loads(result.read_text()),old.get('host_contention_seen',False)
 wait_quiet()
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(path/'profile'),GLOB2_TEST_SEED=str(seed),GLOB2_BENCH_TICKS=str(path/'tick-ns.u64'))
 cmd=[str(BINARY),'game','run','--map-file',str(ROOT/'maps'/w['map']),'--game-seed',str(seed),'--ticks',str(w['ticks']),'--compute-threads',str(n),'--compute-telemetry','waits','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--output-dir',str(path/'game')]
 for player in w['players']:cmd+=['--player',player]
 if a.smoke:cmd+=['--telemetry','checksums']
 m={'command':cmd,'timing_wrapper':['/usr/bin/time','-l'],'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':binary_sha,'load_average_before':os.getloadavg()};(path/'run.json').write_text(json.dumps(m,indent=2));busy=False;start=time.monotonic()
 with (path/'stdout.log').open('w') as out,(path/'stderr.log').open('w') as err:
  proc=subprocess.Popen(['/usr/bin/time','-l',*cmd],cwd=ROOT,env=env,stdout=out,stderr=err)
  while True:
   try:code=proc.wait(timeout=1);break
   except subprocess.TimeoutExpired:busy|=bool(host_activity())
  busy|=bool(host_activity())
 m.update(exit_code=code,elapsed_seconds=time.monotonic()-start,host_contention_seen=busy,load_average_after=os.getloadavg())
 rss=re.search(r'(\d+)\s+maximum resident set size',(path/'stderr.log').read_text(errors='replace'));m['peak_rss_bytes']=int(rss.group(1)) if rss else None;(path/'run.json').write_text(json.dumps(m,indent=2))
 if code:raise RuntimeError(str(path)+' failed: '+(path/'stderr.log').read_text(errors='replace')[-2000:])
 r=json.loads(result.read_text());samples=array.array('Q');samples.frombytes((path/'tick-ns.u64').read_bytes());assert len(samples)==r['benchmark_measured_ticks']>0
 print(json.dumps({'workload':w['name'],'seed':seed,'mode':mode,'participants':n,'ticks':len(samples),'cpu_s':round(r['benchmark_run_cpu_ns']/1e9,3),'elapsed_s':round(m['elapsed_seconds'],2),'p99_ms':round(r['tick_p99_ns']/1e6,3),'host_contention_seen':busy}),flush=True)
 return r,busy

for i,seed in enumerate(SEEDS[:1] if a.pilot or a.smoke else SEEDS):
 for j,w in enumerate(WORKLOADS):
  if a.workload and w['name'] not in a.workload:continue
  for attempt in range(100):
   incomplete=any(json.loads(p.read_text()).get('exit_code')!=0 for p in (OUT/w['name']).glob(f'seed-{seed}-*/run.json'))
   if incomplete:
    dest=OUT/'excluded-host-contention'/w['name']/f'seed-{seed}-incomplete-{time.time_ns()}';dest.mkdir(parents=True)
    for old in (OUT/w['name']).glob(f'seed-{seed}-*'):old.rename(dest/old.name)
    (dest/'reason.json').write_text(json.dumps({'reason':'Replace full paired set containing an incomplete prior run.'}))
   modes=MODES if (i+j)%2==0 else list(reversed(MODES))
   results={};busy=False
   for n in ([1,4] if (i+j)%2==0 else [4,1]):
    for mode in modes:
     r,b=run(w,mode,seed,n);results[n,mode]=r;busy|=b
     if busy:break
    if busy:break
   if busy:
    dest=OUT/'excluded-host-contention'/w['name']/f'seed-{seed}-attempt-{attempt}-{time.time_ns()}';dest.mkdir(parents=True)
    for path in (OUT/w['name']).glob(f'seed-{seed}-*'):path.rename(dest/path.name)
    (dest/'reason.json').write_text(json.dumps({'reason':'Concurrent native compiler, Docker execution or busy VM observed; replace complete six-run paired set.'}));continue
   for mode in MODES:assert results[1,mode]['initialChecksum']==results[4,mode]['initialChecksum']
   for mode in MODES:
    assert results[1,mode]['finalChecksum']==results[4,mode]['finalChecksum'],(seed,w['name'],mode,results[1,mode]['finalChecksum'],results[4,mode]['finalChecksum'])
    if a.smoke:
     x=OUT/w['name']/f'seed-{seed}-{mode}-p1/game';y=OUT/w['name']/f'seed-{seed}-{mode}-p4/game'
     for file in ['world.checksums','game.replay.checksums']:assert (x/file).read_bytes()==(y/file).read_bytes(),(mode,file)
   print(f'SET COMPLETE {w["name"]} seed={seed}',flush=True);break
  else:raise RuntimeError('repeated contention')
print('ALL REQUESTED SETS COMPLETE',flush=True)
