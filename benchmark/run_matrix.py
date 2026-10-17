import argparse,array,hashlib,json,os,platform,re,subprocess,sys,time,shutil
from pathlib import Path
ROOT=Path.cwd();OUT=ROOT/'artifacts/hiring-prewarm-top-candidates';BINARY=ROOT/'build/darwin/client/release/client-full-direct-1111/src/glob2'
MODES=['baseline','shortlist','top'];PARTICIPANTS=[1,4];SEEDS=list(range(42,74))
WORKLOADS=[{'name':'holiday','map':'Holiday_Island_2.map.gz','players':['nicowar','econo','castor','warrush'],'ticks':32768}, {'name':'large-eleven','map':'Oazis.map.gz','players':(['nicowar','econo','castor','warrush']*3)[:11],'ticks':32768}]
p=argparse.ArgumentParser();p.add_argument('--pilot',action='store_true');p.add_argument('--smoke',action='store_true');p.add_argument('--workload',action='append');p.add_argument('--large-ticks',type=int,choices=[16384,32768],default=32768);a=p.parse_args()
WORKLOADS[1]['ticks']=a.large_ticks
if a.smoke:
 OUT=OUT/'smoke';WORKLOADS=[dict(WORKLOADS[0],ticks=2048)]
elif a.pilot:OUT=OUT/'pilot'
OUT.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
identity={'binary':str(BINARY),'binary_sha256':sha(BINARY),'source_manifest_sha256':sha(ROOT/'artifacts/hiring-prewarm-top-candidates/source-sha256.json'),'tick_sample_format':'chronological native uint64 nanoseconds, '+sys.byteorder,'base_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'platform':platform.platform(),'architecture':platform.machine(),'policies':MODES,'participants':PARTICIPANTS,'seeds':SEEDS if not(a.smoke or a.pilot) else SEEDS[:1],'workloads':WORKLOADS,'building_admission':'Four per team in all modes. Eight-tick scheduled refresh publication; experimental cold hiring fields publish with shortlist at its existing one-tick deadline.','comparison':'Baseline: original missing/stale heuristic ranking, no prewarming. Full shortlist: original ranking, then prepare every final ground candidate including fallback entries. Top only: identical ranking, prepare only the primary prefix selected by team coordination; uncoordinated lists use up to actual vacancies. Both retain every fallback entry, reuse usable/stale serving fields and preserve scheduled refresh policy. The previous exact-ranking policy is excluded because it already computes candidate distances during ranking.','build_command':'PYTHONPATH=. CCACHE=1 scons -j8 release=1 client_profile=full server=0','instrumentation':'Same binary and counters in all modes. Thread CPU timers on hiring jobs, private seeds, proposal extensions and ordinary resolve misses. Owner misses include all ordinary building readers, not only hires. Capture counters before final checksum/save work. Total process CPU includes final draining of private jobs.','limitations':['Experimental prewarming path is not save/load qualified. No production PR is modified.','Policies change game evolution and membership/ranking; total CPU is not isolated kernel cost.','No graphics/frame pacing, other devices or cross-platform coverage.'],'run_order':'Serial, paired per seed/map, rotate/reverse mode order and alternate participant order; replace full six-run set on native build, docker exec/build, or VM CPU >=10% overlap. VM CPU comes from host ps. After activity clears, require 15 quiet seconds. Other host load is not fully controlled.'}
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
 path=OUT/w['name']/f'seed-{seed}-{mode}-p{n}';result=path/'game/result.json';path.mkdir(parents=True,exist_ok=True)
 if result.exists() and (path/'run.json').exists():
  old=json.loads((path/'run.json').read_text());assert old['binary_sha256']==identity['binary_sha256'];assert old['command'][old['command'].index('--ticks')+1]==str(w['ticks'])
  if old.get('exit_code')==0:return json.loads(result.read_text()),old.get('host_contention_seen',False)
 wait_quiet()
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(path/'profile'),GLOB2_TEST_SEED=str(seed),GLOB2_HIRING_PREWARM=mode,GLOB2_BENCH_TICKS=str(path/'tick-ns.u64'))
 cmd=[str(BINARY),'game','run','--map-file',str(ROOT/'maps'/w['map']),'--game-seed',str(seed),'--ticks',str(w['ticks']),'--compute-threads',str(n),'--compute-telemetry','waits','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--output-dir',str(path/'game')]
 for player in w['players']:cmd+=['--player',player]
 if a.smoke:cmd+=['--telemetry','checksums']
 m={'command':cmd,'timing_wrapper':['/usr/bin/time','-l'],'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':identity['binary_sha256'],'load_average_before':os.getloadavg()};(path/'run.json').write_text(json.dumps(m,indent=2));busy=False;start=time.monotonic()
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
 probe=r['hiring_gradient_probe'];assert probe['mode']=={'baseline':0,'shortlist':1,'top':3}[mode]
 if mode=='baseline':assert probe['private_builds']==0 and probe['applied']==0
 print(json.dumps({'workload':w['name'],'seed':seed,'mode':mode,'participants':n,'ticks':len(samples),'cpu_s':round(r['benchmark_run_cpu_ns']/1e9,3),'elapsed_s':round(m['elapsed_seconds'],2),'p99_ms':round(r['tick_p99_ns']/1e6,3),'probe':probe,'host_contention_seen':busy}),flush=True)
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
   modes=MODES[(i+j)%3:]+MODES[:(i+j)%3]
   if i%2:modes=list(reversed(modes))
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
   assert len({v['initialChecksum'] for v in results.values()})==1
   for mode in MODES:
    assert results[1,mode]['finalChecksum']==results[4,mode]['finalChecksum'],(seed,w['name'],mode,results[1,mode]['finalChecksum'],results[4,mode]['finalChecksum'])
    if a.smoke:
     x=OUT/w['name']/f'seed-{seed}-{mode}-p1/game';y=OUT/w['name']/f'seed-{seed}-{mode}-p4/game'
     for file in ['world.checksums','game.replay.checksums']:assert (x/file).read_bytes()==(y/file).read_bytes(),(mode,file)
   print(f'SET COMPLETE {w["name"]} seed={seed}',flush=True);break
  else:raise RuntimeError('repeated contention')
print('ALL REQUESTED SETS COMPLETE',flush=True)
