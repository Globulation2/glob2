import argparse, hashlib, json, os, platform, re, statistics, subprocess, time
from pathlib import Path
ROOT=Path.cwd(); BASE=ROOT/'artifacts/final-master-comparison'; OUT=BASE/'runs'
POLICIES=['master','branch']; SEEDS=list(range(42,74))
WORKLOADS=[{'name':'holiday','map':'Holiday_Island_2.map.gz','teams':4}, {'name':'oazis','map':'Oazis.map.gz','teams':11}]
parser=argparse.ArgumentParser();parser.add_argument('--pilot',action='store_true');args=parser.parse_args()
if args.pilot: OUT=BASE/'pilot';SEEDS=[42]
OUT.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
binaries={p:BASE/'binaries'/p for p in POLICIES}
identity={'platform':platform.platform(),'architecture':platform.machine(),'participants':4,'ticks':4096,'warmup_ticks':0,'whole_process_cpu':'/usr/bin/time -l user+system, including setup, final drain and exit','seeds':SEEDS,'build_command':'PYTHONPATH=. scons release=1 CXXFLAGS=-g0 client_profile=full server=0 -j4','instrumentation':'Stock built-in benchmark CPU and tick-duration counters only. No private probes or source patches.','policy':'Global four building-gradient admissions per tick in both revisions; PR1220 is not included.','run_order':'Serial paired games; alternate AB/BA by map and seed; reject both games if compiler/test/VM contention is detected.','limitations':['Short early-game comparison on one Mac; not long-game or slower-device coverage.','Simulation trajectories may differ, so this measures complete-game cost rather than isolated kernels.','Host guard cannot detect all OS/background activity.','Tick percentiles are per-game percentiles, not pooled percentiles.','Natural early game ends are retained; per-tick metrics use executed ticks.'],'binaries':{p:{'path':str(b),'sha256':sha(b),**json.loads((BASE/f'{p}-build.json').read_text())} for p,b in binaries.items()},'workloads':[]}
for w in WORKLOADS:
    w['players']=(['nicowar','econo','castor','warrush']*3)[:w['teams']]
    w['map_sha256']=sha(ROOT/'maps'/w['map']);identity['workloads'].append(w)
(OUT/'experiment.json').write_text(json.dumps(identity,indent=2)+'\n')

def activity(own_pid=None):
    rows=[];processes=[]
    for line in subprocess.check_output(['ps','-axo','pid=,ppid=,%cpu=,args='],text=True).splitlines():
        f=line.strip().split(None,3)
        if len(f)!=4:continue
        try:pid=int(f[0]);parent=int(f[1]);cpu=float(f[2])
        except ValueError:continue
        command=f[3]
        processes.append((pid,parent,cpu,Path(command.split(None,1)[0]).name,command))
    owned={own_pid} if own_pid is not None else set()
    while True:
        descendants={pid for pid,parent,*_ in processes if parent in owned}
        if descendants<=owned:break
        owned|=descendants
    for pid,parent,cpu,name,command in processes:
        if pid in owned:continue
        kind=None
        if name in {'clang','clang++','cc1plus','g++','c++','ld','ld-prime'}:kind='compiler/linker'
        elif name in {'glob2-engine-tests','glob2-unit-tests','glob2'}:kind='game/test'
        elif ('VirtualMachine' in name or name.startswith('qemu')) and cpu>=10:kind='VM'
        elif name in {'Python','python','python3','python3.14','python3.12'} and ('/scons ' in command or 'tools/dev_build.py' in command):kind='build-driver'
        if kind: rows.append({'pid':pid,'cpu':cpu,'kind':kind})
    return rows

def quiet():
    seen=False;since=None
    while True:
        busy=activity()
        if busy:
            if not seen: print('Waiting for host builds/tests: '+json.dumps(busy),flush=True)
            seen=True;since=None;time.sleep(5)
        elif seen:
            if since is None:since=time.monotonic()
            if time.monotonic()-since>=30:return
            time.sleep(5)
        else:return

def run(w,seed,policy):
    dest=OUT/w['name']/f'seed-{seed}-{policy}';dest.mkdir(parents=True,exist_ok=True)
    metadata=dest/'run.json'
    if metadata.exists():
        m=json.loads(metadata.read_text())
        if m.get('exit_code')==0 and not m.get('contention'):
            assert m['binary_sha256']==identity['binaries'][policy]['sha256']
            return json.loads((dest/'game/result.json').read_text()),False
    env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(dest/'profile'))
    cmd=[str(binaries[policy]),'game','run','--map-file',str(ROOT/'maps'/w['map']),'--game-seed',str(seed),'--ticks','4096','--compute-threads','4','--compute-telemetry','off','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--output-dir',str(dest/'game')]
    for player in w['players']:cmd+=['--player',player]
    m={'command':cmd,'timing_wrapper':['/usr/bin/time','-l'],'binary_sha256':identity['binaries'][policy]['sha256'],'load_before':os.getloadavg(),'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))}}
    metadata.write_text(json.dumps(m,indent=2)+'\n');start=time.monotonic();busy=[]
    with (dest/'stdout.log').open('w') as stdout,(dest/'stderr.log').open('w') as stderr:
        proc=subprocess.Popen(['/usr/bin/time','-l',*cmd],cwd=ROOT,env=env,stdout=stdout,stderr=stderr)
        while True:
            try: code=proc.wait(timeout=0.5);break
            except subprocess.TimeoutExpired: busy+=activity(proc.pid)
    busy+=activity()
    timing=(dest/'stderr.log').read_text(errors='replace')
    match=re.search(r'([\d.]+)\s+real\s+([\d.]+)\s+user\s+([\d.]+)\s+sys',timing)
    if code==0: assert match,timing[-1000:]
    rss=re.search(r'(\d+)\s+maximum resident set size',timing)
    m.update(exit_code=code,elapsed_seconds=time.monotonic()-start,contention=busy,load_after=os.getloadavg(),whole_cpu_seconds=float(match[2])+float(match[3]) if match else None,whole_wall_seconds=float(match[1]) if match else None,peak_rss_bytes=int(rss[1]) if rss else None)
    metadata.write_text(json.dumps(m,indent=2)+'\n')
    if code:raise RuntimeError(str(dest)+' failed: '+(dest/'stderr.log').read_text(errors='replace')[-2000:])
    result=json.loads((dest/'game/result.json').read_text());assert result['benchmark_measured_ticks']>0
    print(json.dumps({'map':w['name'],'seed':seed,'policy':policy,'ticks':result['ticks'],'cpu_s':result['benchmark_run_cpu_ns']/1e9,'p99_us':result['tick_p99_ns']/1e3,'contended':bool(busy)}),flush=True)
    return result,bool(busy)

for i,seed in enumerate(SEEDS):
    for j,w in enumerate(WORKLOADS):
        for attempt in range(100):
            quiet();pairs={};busy=False
            for policy in (POLICIES if (i+j)%2==0 else POLICIES[::-1]):
                result,contended=run(w,seed,policy);pairs[policy]=result;busy|=contended
                if busy:break
            if busy:
                archived=OUT/'excluded-contention'/w['name']/f'seed-{seed}-{time.time_ns()}';archived.mkdir(parents=True)
                for path in (OUT/w['name']).glob(f'seed-{seed}-*'):path.rename(archived/path.name)
                continue
            assert all(0<pairs[p]['ticks']<=4096 and pairs[p]['benchmark_measured_ticks']==pairs[p]['ticks'] for p in POLICIES),(w['name'],seed,{p:pairs[p]['ticks'] for p in POLICIES})
            break
        else:raise RuntimeError('Repeated contention')
print('COMPLETE',flush=True)
