import argparse,array,gzip,hashlib,json,os,platform,re,struct,subprocess,sys,time
from pathlib import Path
ROOT=Path.cwd();OUT=ROOT/'artifacts/gradient-admission-variety-20261016'
BINARY=ROOT/'build/darwin/client/release/client-full-direct-1111/src/glob2'
POLICIES=['capped','team4'];PARTICIPANTS=[1,4];SEEDS=list(range(42,74))
WORKLOADS=[{'name':'small-duel','map':'SmallForTwo.map.gz','players':['nicowar','warrush'],'ticks':32768},
 {'name':'archipelago','map':'Archipelago.map.gz','players':['nicowar','econo','castor','warrush','nicowar'],'ticks':32768},
 {'name':'large-eleven','map':'Oazis.map.gz','players':(['nicowar','econo','castor','warrush']*3)[:11],'ticks':32768}]
parser=argparse.ArgumentParser();parser.add_argument('--pilot',action='store_true');parser.add_argument('--workload',action='append',choices=[w['name'] for w in WORKLOADS]);args=parser.parse_args()
OUT.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
identity={'binary':str(BINARY),'binary_sha256':sha(BINARY),'base_revision':'43b153739838207983c922ab4cbeed7773459de9','snapshot_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'platform':platform.platform(),'architecture':platform.machine(),'policies':POLICIES,'participants':PARTICIPANTS,'seeds':SEEDS,'workloads':WORKLOADS,'run_order':'Serial runs, seed outer loop. Rotate/reverse workload order by seed. Alternate participant and policy order by seed and workload. Full four-run seed/workload set replaced if compiler activity occurs during any run.','build_command':'PYTHONPATH=. CCACHE=1 scons -j8 release=1 client_profile=full server=0','tick_sample_format':'chronological native unsigned 64-bit nanoseconds; '+sys.byteorder,'host_contention_guard':'Wait for no clang before run; poll once/second during run. Preserve/retry whole seed/workload set if clang appears. Other host activity not controlled.'}
for w in WORKLOADS:
 p=ROOT/'maps'/w['map'];w['map_path']=str(p);w['map_sha256']=sha(p)
 b=gzip.open(p,'rb').read();n=struct.unpack_from('>I',b)[0];major,minor,teams,offset=struct.unpack_from('>4I',b,4+n);assert b[offset:offset+4]==b'MapB'
 lw,lh=struct.unpack_from('>2I',b,offset+4);w.update(width=1<<lw,height=1<<lh,teams=teams,map_format=[major,minor]);assert len(w['players'])==teams
(OUT/'experiment.json').write_text(json.dumps(identity,indent=2))
def compiler_active():return subprocess.run(['pgrep','-x','clang'],stdout=subprocess.DEVNULL).returncode==0
def wait_quiet():
 while compiler_active():
  print('WAITING: unrelated compiler active',flush=True);time.sleep(15)
def run(w,mode,seed,participants):
 path=OUT/w['name']/f'seed-{seed}-{mode}-p{participants}';result=path/'game/result.json';path.mkdir(parents=True,exist_ok=True)
 if result.exists() and (path/'run.json').exists():
  old=json.loads((path/'run.json').read_text())
  if old.get('exit_code')==0:
   assert old['binary_sha256']==identity['binary_sha256'];return json.loads(result.read_text()),bool(old.get('compiler_seen'))
 wait_quiet()
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
 env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(path/'profile'),GLOB2_TEST_SEED=str(seed),GLOB2_BUILDING_ADMISSION=mode,GLOB2_BENCH_TICKS=str(path/'tick-ns.u64'))
 cmd=[str(BINARY),'game','run','--map-file',w['map_path'],'--game-seed',str(seed),'--ticks',str(w['ticks']),'--compute-threads',str(participants),'--compute-telemetry','waits','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--output-dir',str(path/'game')]
 for ai in w['players']:cmd+=['--player',ai]
 manifest={'command':cmd,'timing_wrapper':['/usr/bin/time','-l'],'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':identity['binary_sha256'],'load_average_before':os.getloadavg()};(path/'run.json').write_text(json.dumps(manifest,indent=2));begin=time.monotonic();contended=False
 with (path/'stdout.log').open('w') as stdout,(path/'stderr.log').open('w') as stderr:
  proc=subprocess.Popen(['/usr/bin/time','-l',*cmd],cwd=ROOT,env=env,stdout=stdout,stderr=stderr)
  while True:
   try:code=proc.wait(timeout=1);break
   except subprocess.TimeoutExpired:contended|=compiler_active()
  contended|=compiler_active()
 manifest.update(exit_code=code,elapsed_seconds=time.monotonic()-begin,load_average_after=os.getloadavg(),compiler_seen=contended)
 rss=re.search(r'(\d+)\s+maximum resident set size',(path/'stderr.log').read_text(errors='replace'));manifest['peak_rss_bytes']=int(rss.group(1)) if rss else None;(path/'run.json').write_text(json.dumps(manifest,indent=2))
 if code:raise RuntimeError(str(path)+' failed: '+(path/'stderr.log').read_text(errors='replace')[-1500:])
 value=json.loads(result.read_text());assert value['building_gradient_jobs']==value['building_gradient_worker_jobs_measured']
 samples=array.array('Q');samples.frombytes((path/'tick-ns.u64').read_bytes());assert len(samples)==value['benchmark_measured_ticks']>0
 assert sum(value['building_gradient_queue_delay_histogram'])==value['building_gradient_queue_delay_samples']
 if mode=='capped':assert value['building_gradient_max_batch']<=4
 else:assert value['building_gradient_max_team_batch']<=4
 print(json.dumps({'workload':w['name'],'seed':seed,'mode':mode,'participants':participants,'ticks':len(samples),'cpu_s':round(value['benchmark_run_cpu_ns']/1e9,3),'elapsed_s':round(manifest['elapsed_seconds'],2),'p99_ms':round(value['tick_p99_ns']/1e6,3),'max_batch':value['building_gradient_max_batch'],'queue_max':value['building_gradient_queue_delay_max'],'compiler_seen':contended}),flush=True)
 return value,contended
def run_set(w,seed,index):
 for attempt in range(100):
  incomplete=False
  for existing in (OUT/w['name']).glob(f'seed-{seed}-*/run.json'):
   old=json.loads(existing.read_text());incomplete|=old.get('exit_code')!=0 or bool(old.get('compiler_seen'))
  if incomplete:
   dest=OUT/'excluded-host-contention'/w['name']/f'seed-{seed}-recovered-{time.time_ns()}';dest.mkdir(parents=True)
   for path in (OUT/w['name']).glob(f'seed-{seed}-*'):path.rename(dest/path.name)
   (dest/'reason.json').write_text(json.dumps({'reason':'Replace whole set with incomplete/contended prior run when resuming.'},indent=2))
  results={};contended=False
  for participants in ([1,4] if index%2==0 else [4,1]):
   for mode in (POLICIES if index%2==0 else list(reversed(POLICIES))):
    v,busy=run(w,mode,seed,participants);results[participants,mode]=v;contended|=busy
    if busy:break
   if contended:break
  if contended:
   dest=OUT/'excluded-host-contention'/w['name']/f'seed-{seed}-attempt-{attempt}-{time.time_ns()}';dest.mkdir(parents=True,exist_ok=False)
   for path in (OUT/w['name']).glob(f'seed-{seed}-*'):path.rename(dest/path.name)
   (dest/'reason.json').write_text(json.dumps({'reason':'Compiler observed during at least one run; replace all four policy/participant runs in this seed/workload set.','seed':seed,'workload':w['name']},indent=2));print('RETRY WHOLE SET: '+str(dest),flush=True);wait_quiet();continue
  assert len({v['initialChecksum'] for v in results.values()})==1
  for mode in POLICIES:assert results[1,mode]['finalChecksum']==results[4,mode]['finalChecksum'],(w['name'],seed,mode)
  print(f"SET COMPLETE workload={w['name']} seed={seed}",flush=True);return
 raise RuntimeError('Repeated host contention prevented valid runs')
seeds=SEEDS[:1] if args.pilot else SEEDS
for i,seed in enumerate(seeds):
 active=[w for w in WORKLOADS if not args.workload or w['name'] in args.workload]
 workloads=active[i%len(active):]+active[:i%len(active)]
 if i%2:workloads=list(reversed(workloads))
 for j,w in enumerate(workloads):run_set(w,seed,i+j)
 print(f'SEED COMPLETE {i+1}/{len(seeds)} seed={seed}',flush=True)
print('PILOT COMPLETE' if args.pilot else 'ALL REQUESTED WORKLOAD-SEED SETS COMPLETE',flush=True)
