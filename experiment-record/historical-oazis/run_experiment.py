#!/usr/bin/env python3
"""Resumable historical Playground/Nicowar benchmark; Linux process accounting."""
import argparse, csv, datetime as dt, difflib, hashlib, json, os
from pathlib import Path
import platform, random, re, signal, statistics, subprocess, time, threading, uuid, fcntl
from concurrent.futures import ThreadPoolExecutor
from zoneinfo import ZoneInfo
ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
TZ = ZoneInfo('America/Toronto')
SDL = Path('/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix')
LINK = '-L/usr/lib/gcc/x86_64-linux-gnu/15'
CPU_GROUPS=[list(range(i*3,i*3+3))+list(range(16+i*3,19+i*3)) for i in range(4)]
ACTIVE={}; PROGRESS_LOCK=threading.Lock(); RESULTS_LOCK=threading.Lock()

def sha(p):
    return hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()

def save(p, obj):
    tmp = p.with_suffix(p.suffix + '.'+uuid.uuid4().hex+'.tmp'); tmp.write_text(json.dumps(obj, indent=2)+'\n'); tmp.replace(p)

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()

def body(s, signature):
    a=s.index('{', s.index(signature)); depth=1; b=a+1
    while depth:
        depth += (s[b]=='{')-(s[b]=='}'); b+=1
    return a+1,b-1

def adapt(src, commit):
    patches=[]
    def edit(p, new):
        old=p.read_text()
        if old!=new:
            patches.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+str(p.relative_to(src)),tofile='b/'+str(p.relative_to(src))))
            p.write_text(new)
    # Fresh archives only: simulation source is otherwise left at its historical revision.
    p=src/'SConstruct'; s=p.read_text()
    s=s.replace('\t','    ').replace('--fwhole-program','-fwhole-program')
    # Boost.System is header-only in this host's Boost installation.
    s=s.replace('"boost_system", ', '').replace("'boost_system', ", '')
    if 'opts.Add("CXX"' not in s:
        s=s.replace('env = Environment()', 'env = Environment(CC="gcc-13", CXX="g++-13")')
    edit(p,s)
    p=next(p for p in (src/'src/Glob2.cpp',src/'src/app/Glob2.cpp') if p.exists())
    s=p.read_text();a,b=body(s,'int Glob2::runTestGames()');part=s[a:b]
    part=part.replace('globalContainer->automaticEndingSteps=90000;', 'globalContainer->automaticEndingSteps=atoi(getenv("GLOB2_TEST_MAX_TICKS"));')
    part=part.replace('long t = time(NULL);', 'long t = strtoul(getenv("GLOB2_TEST_SEED"), NULL, 10);')
    assert 'engine.run();' in part
    part=part.replace('engine.run();', '''const auto historyStart = std::chrono::steady_clock::now();
        engine.run();
        std::cout << "HISTORY_TIMING run_wall_seconds="
                  << std::chrono::duration<double>(std::chrono::steady_clock::now()-historyStart).count()
                  << std::endl;
        return 0;''',1)
    s=s[:a]+part+s[b:];s='#include <chrono>\n'+s;edit(p,s)
    # Fixed map, preserving each revision's own map loader and AI/game initialization.
    p=next(p for p in src.rglob('Engine*.cpp') if 'Engine::chooseRandomMap()' in p.read_text())
    s=p.read_text();a,b=body(s,'Engine::chooseRandomMap()');s=s[:a]+'\n    return loadMapHeader("maps/Playground.map");\n'+s[b:]
    a,b=body(s,'GameHeader Engine::createRandomGame(int numberOfTeams)');part=s[a:b]
    if 'setRandomSeed' not in part:
        part=part.replace('GameHeader gameHeader;', 'GameHeader gameHeader;\n    gameHeader.setRandomSeed(strtoul(getenv("GLOB2_TEST_SEED"), NULL, 10));')
    part=re.sub(r'static_cast<AI::(ImplementitionID|ImplementationID)>\(syncRand\(\) % 5 \+ 1\)', 'AI::NICOWAR',part)
    # Later runners get an explicit --matchup, so their evolving RNG handling is retained.
    s=s[:a]+part+s[b:];edit(p,s)
    p=next(p for p in src.rglob('Engine*.cpp') if 'int time = gui.game.stepCounter;' in p.read_text())
    s=p.read_text();needle='int time = gui.game.stepCounter;';assert needle in s
    report='''
        int historyWinner=-1;
        for(int t=0;t<gui.game.mapHeader.getNumberOfTeams();++t)
            if(gui.game.teams[t]->hasWon) historyWinner=t;
        std::cout << "HISTORY_RESULT ticks=" << time << " ended=" << gui.game.isGameEnded
                  << " winner=" << historyWinner << " width=" << gui.game.map.getW()
                  << " height=" << gui.game.map.getH()
                  << " teams=" << gui.game.mapHeader.getNumberOfTeams()
                  << " controllers=" << gui.game.gameHeader.getNumberOfPlayers() << std::endl;
'''
    s=s.replace(needle,needle+report,1);edit(p,s)
    p=src/'libgag/src/FileManager.cpp';s=p.read_text()
    if 'GLOB2_USER_DIR' not in s and 'GLOB2_USER_DATA_DIR' not in s:
        s=s.replace('const std::string homeDir = getenv("HOME");', 'const std::string homeDir = getenv("GLOB2_USER_DIR") ? getenv("GLOB2_USER_DIR") : getenv("HOME");')
    s=s.replace('char link[100];','char link[4096];')
    edit(p,s)
    (src.parent/'adapter.patch').write_text(''.join(patches))
    # Always load the identical raw map bytes, including after gzip-default transition.
    for p in (src/'maps/Playground.map',src/'maps/Playground.map.gz'):
        p.unlink(missing_ok=True)
    (src/'maps/Playground.map').write_bytes((OUT/'Oazis.map').read_bytes())

def plan():
    tip=git('rev-parse','origin/master')
    rows=[line.split(' ',2) for line in git('log','--first-parent','--format=%H %cI %s',tip).splitlines()]
    first=next(i for i,r in enumerate(rows) if r[1][:10]<'2026-09-03')
    # September 2 is the boundary before the September 3 activity burst.
    end=dt.datetime.fromisoformat(rows[0][1]).astimezone(TZ)
    dates=[];d=dt.date(2026,9,2)
    while d<=end.date(): dates.append(d);d+=dt.timedelta(days=3)
    snapshots=[]
    for date in dates:
        cutoff=dt.datetime.combine(date,dt.time.max,TZ)
        r=next(r for r in rows if dt.datetime.fromisoformat(r[1])<=cutoff)
        if not snapshots or snapshots[-1]['commit']!=r[0]:
            snapshots.append(dict(sample_date=str(date),commit=r[0],commit_date=r[1],subject=r[2]))
    if snapshots[-1]['commit']!=tip:
        snapshots.append(dict(sample_date=str(end.date()),commit=tip,commit_date=rows[0][1],subject=rows[0][2]))
    rng=random.Random(20261011)
    return dict(schema_version=1,tip=tip,snapshots=snapshots,seeds=rng.sample(range(1,2**31),32),
        map='Playground',map_sha256=sha(OUT/'Oazis.map'),width=128,height=128,teams=8,
        ai=['nicowar']*11,mode='FFA',timeout_seconds=3600,tick_limit=2147483647,
        concurrency=4,cpu_affinity_groups=CPU_GROUPS,compile_cpu_affinity=list(range(32)),build_jobs=32,compiler='gcc-13/g++-13',linkflags=LINK,
        execution='four simultaneous games, each on three physical cores plus SMT siblings; all binaries built first across CPUs 0-31; no compilation during measurements',
        accounting='os.wait4: user+system process CPU seconds, peak RSS KiB; wall clock includes startup and shutdown',
        observer='Legacy random-games driver: 8 AI controllers plus one idle local observer',
        metrics='ticks/sec uses Engine::run wall time; CPU/tick uses full-process CPU; completed means exit 0 and winner>=0',
        host=dict(platform=platform.platform(),cpu=subprocess.check_output(['lscpu'],text=True),
                  compiler=subprocess.check_output(['g++-13','--version'],text=True),
                  libraries=subprocess.check_output(['ldconfig','-p'],text=True),
                  initial_load=Path('/proc/loadavg').read_text()))

def prepare(snap):
    folder=OUT/'snapshots'/(snap['sample_date']+'-'+snap['commit'][:9]);src=folder/'source'
    folder.mkdir(parents=True,exist_ok=True)
    if not (folder/'prepared.json').exists():
        src.mkdir(exist_ok=True)
        archive=subprocess.Popen(['git','archive',snap['commit']],cwd=ROOT,stdout=subprocess.PIPE)
        subprocess.run(['tar','-x','-C',str(src)],stdin=archive.stdout,check=True)
        archive.stdout.close();assert archive.wait()==0
        adapt(src,snap['commit']);save(folder/'prepared.json',snap)
    return folder,src

def build(folder,src,jobs=2):
    date=folder.name[:10]
    # Legacy preparers started before the lock was added; wait for their publication.
    def legacy_builders():
        found=[]
        for proc in Path('/proc').glob('[0-9]*'):
            if int(proc.name)==os.getpid():continue
            try:argv=proc.joinpath('cmdline').read_bytes().decode().split('\0')
            except (FileNotFoundError,PermissionError,ProcessLookupError):continue
            if len(argv)>1 and argv[0].split('/')[-1].startswith('python') and argv[1].endswith('run_experiment.py') and '--build-only' in argv and '--only-date' in argv:
                if argv[argv.index('--only-date')+1]==date:found.append(int(proc.name))
        return found
    while legacy_builders():
        if '--build-only' in os.sys.argv:break
        time.sleep(2)
    with (folder/'.build.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        return _build(folder,src,jobs)

def _build(folder,src,jobs=2):
    stamp=folder/'build.json'
    if stamp.exists():
        record=json.loads(stamp.read_text());p=Path(record['binary'])
        if p.exists() and sha(p)==record['binary_sha256']:
            if record.get('adapter_sha256')==sha(folder/'adapter.patch'):return p
        else:raise RuntimeError('cached binary changed')
    env=os.environ.copy();s=(src/'SConstruct').read_text()
    if (src/'scons/ccache.py').exists():env.update(CCACHE='1',CCACHE_MAXSIZE='12G')
    if 'SDL3' in s:env['GLOB2_SDL3_PREFIX']=str(SDL)
    command=['taskset','-c','0-31','scons','release=1','CC=gcc-13','CXX=g++-13','LINKFLAGS='+LINK,'-j'+str(jobs)]
    start=time.monotonic()
    if (folder/'build.log').exists():
        (folder/'build.log').rename(folder/('build-previous-'+str(time.time_ns())+'.log'))
    with (folder/'build.log').open('w') as log:
        rc=subprocess.call(command,cwd=src,env=env,stdout=log,stderr=subprocess.STDOUT)
    if rc:raise RuntimeError(f'build failed: {folder}/build.log')
    binaries=[p for p in src.glob('build/**/glob2') if p.is_file() and os.access(p,os.X_OK)]
    assert len(binaries)==1,binaries
    binary=binaries[0]
    ldd=subprocess.check_output(['ldd',str(binary)],text=True)
    (folder/'libraries.txt').write_text(ldd)
    libraries={p:sha(Path(p)) for p in re.findall(r'(/[^\s]+)',ldd) if Path(p).is_file()}
    save(stamp,dict(command=command,binary=str(binary),binary_sha256=sha(binary),adapter_sha256=sha(folder/'adapter.patch'),shared_libraries=libraries,
                   build_wall_seconds=time.monotonic()-start,environment={k:v for k,v in env.items() if k.startswith(('GLOB2_','CCACHE'))}))
    return binary

def measure(binary,src,directory,seed,ticks,timeout,slot=0):
    directory.mkdir(parents=True,exist_ok=True)
    with PROGRESS_LOCK:
        ACTIVE[slot]=dict(run=str(directory),seed=seed,cpu_affinity=CPU_GROUPS[slot],elapsed_seconds=0)
        save(OUT/'progress.json',dict(state='running',active=dict(ACTIVE),concurrency=4))
    s=next(p for p in (src/'src/Glob2.cpp',src/'src/app/Glob2.cpp') if p.exists()).read_text()
    # Structured command names replaced the legacy flags, without replacing the driver.
    cli=next(src.rglob('CommandLine.cpp'),src/'missing')
    if cli.exists() and 'dev random-games' in cli.read_text():
        args=['dev','random-games','--runs','1','--ticks',str(ticks),'--map','Oazis','--matchup',','.join(['nicowar']*11)]
    else:
        args=['-test-games-nox']
        gc='\n'.join(p.read_text() for p in src.rglob('GlobalContainer*.cpp'))
        if '--matchup' in gc:args+=['--map','Oazis','--matchup',','.join(['nicowar']*11)]
    env=os.environ.copy();env.update(GLOB2_TEST_SEED=str(seed),GLOB2_TEST_MAX_TICKS=str(ticks),
        SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_ASSET_DIR=str(OUT/'snapshots'/src.parent.name/'runtime'))
    profile=directory/'profile';profile.mkdir(exist_ok=True)
    env.update(GLOB2_USER_DIR=str(profile),GLOB2_USER_DATA_DIR=str(profile))
    # Isolate profile writes without repurposing HOME: legacy FileManager stores in .glob2.
    # A cwd-local profile takes priority over user data in the supported CLI revisions.
    if cli.exists() and '--profile-dir' in cli.read_text():args+=['--profile-dir',str(directory/'profile')]
    command=['taskset','-c',','.join(map(str,CPU_GROUPS[slot])),str(binary),*args]
    timed_out=False;start=time.monotonic();last=start
    with (directory/'stdout.log').open('w') as log:
        proc=subprocess.Popen(command,cwd=OUT/'snapshots'/src.parent.name/'runtime',env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        while True:
            pid,status,usage=os.wait4(proc.pid,os.WNOHANG)
            if pid:break
            if time.monotonic()-start>timeout:
                timed_out=True;os.killpg(proc.pid,signal.SIGTERM)
                deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    pid,status,usage=os.wait4(proc.pid,os.WNOHANG)
                    if pid:break
                    time.sleep(.1)
                if not pid:
                    os.killpg(proc.pid,signal.SIGKILL);pid,status,usage=os.wait4(proc.pid,0)
                break
            if time.monotonic()-last>30:
                with PROGRESS_LOCK:
                    ACTIVE[slot]['elapsed_seconds']=time.monotonic()-start
                    save(OUT/'progress.json',dict(state='running',active=dict(ACTIVE),concurrency=4))
                last=time.monotonic()
            time.sleep(.02)
    wall=time.monotonic()-start;proc.returncode=os.waitstatus_to_exitcode(status)
    output=(directory/'stdout.log').read_text(errors='replace')
    match=re.search(r'HISTORY_RESULT (.*)',output)
    values=dict(re.findall(r'(\w+)=([^\s]+)',match[1])) if match else {}
    clock=re.search(r'HISTORY_TIMING run_wall_seconds=([0-9.e+-]+)',output)
    total_cpu=usage.ru_utime+usage.ru_stime
    nticks=int(values['ticks']) if values else None
    complete=not timed_out and proc.returncode==0 and int(values.get('winner',-1))>=0
    result=dict(seed=seed,cpu_slot=slot,cpu_affinity=CPU_GROUPS[slot],command=command,returncode=proc.returncode,status='timeout' if timed_out else 'completed' if complete else 'failed' if proc.returncode or not values else 'no_winner',
        ticks=nticks,wall_seconds=wall,user_cpu_seconds=usage.ru_utime,system_cpu_seconds=usage.ru_stime,
        total_cpu_seconds=total_cpu,peak_rss_kib=usage.ru_maxrss,
        run_wall_seconds=float(clock[1]) if clock else None,
        cpu_seconds_per_tick=total_cpu/nticks if nticks else None,
        e2e_ticks_per_second=nticks/wall if nticks else None,
        ticks_per_second=nticks/float(clock[1]) if nticks and clock else None,reported=values)
    if values:
        workload=json.loads((OUT/'experiment.json').read_text())
        if workload['width'] is None:
            workload.update(width=int(values['width']),height=int(values['height']));save(OUT/'experiment.json',workload)
        assert (int(values['width']),int(values['height']),int(values['teams']),int(values['controllers']))==(workload['width'],workload['height'],11,12),values
    if values:
        roster=re.findall(r'Player: (Nicowar[^\n]*) for team (\d+)',output)
        assert len(roster)==11 and sorted(int(t) for _,t in roster)==list(range(11)),roster
    save(directory/'result.json',result)
    with PROGRESS_LOCK:
        ACTIVE.pop(slot,None)
        save(OUT/'progress.json',dict(state='running',active=dict(ACTIVE),concurrency=4))
    return result

def summarize(config):
    all_rows=[];aggregates=[]
    for snap in config['snapshots']:
        folder=OUT/'snapshots'/(snap['sample_date']+'-'+snap['commit'][:9]);rows=[]
        for seed in config['seeds']:
            path=folder/'runs'/str(seed)/'result.json'
            if path.exists():
                row=json.loads(path.read_text());row.update(sample_date=snap['sample_date'],commit=snap['commit']);rows.append(row);all_rows.append(row)
        agg={**snap,'runs':len(rows),'completed':sum(r['status']=='completed' for r in rows),'timeouts':sum(r['status']=='timeout' for r in rows),'failed':sum(r['status']=='failed' for r in rows)}
        for key in ('ticks','wall_seconds','peak_rss_kib','total_cpu_seconds','cpu_seconds_per_tick','ticks_per_second','e2e_ticks_per_second'):
            vals=[r[key] for r in rows if r['status']=='completed' and r[key] is not None]
            agg['mean_'+key]=statistics.mean(vals) if vals else None
            agg['median_'+key]=statistics.median(vals) if vals else None
            agg['stdev_'+key]=statistics.stdev(vals) if len(vals)>1 else None
        aggregates.append(agg)
    save(OUT/'summary.json',aggregates)
    for name,rows in [('runs.csv',all_rows),('summary.csv',aggregates)]:
        if rows:
            keys=[k for k in rows[0] if k not in ('reported','command')]
            with (OUT/name).open('w',newline='') as stream:
                writer=csv.DictWriter(stream,keys,extrasaction='ignore');writer.writeheader();writer.writerows(rows)
    return aggregates

def prepare(snap):
    original=Path('/home/bradley/.codex/worktrees/a630/glob2/artifacts/engine-history')/'snapshots'/(snap['sample_date']+'-'+snap['commit'][:9])
    folder=OUT/'snapshots'/original.name;folder.mkdir(parents=True,exist_ok=True)
    src=original/'source';runtime=folder/'runtime';runtime.mkdir(exist_ok=True)
    for entry in src.iterdir():
        if entry.name=='maps':continue
        target=runtime/entry.name
        if not target.exists() and not target.is_symlink():target.symlink_to(entry,target_is_directory=entry.is_dir())
    maps=runtime/'maps';maps.mkdir(exist_ok=True)
    for name in ['Oazis.map','Playground.map']:(maps/name).write_bytes((OUT/'Oazis.map').read_bytes())
    for name in ['build.json','adapter.patch','libraries.txt']:
        (folder/name).write_bytes((original/name).read_bytes())
    save(folder/'map-override.json',dict(map='Oazis',sha256=sha(OUT/'Oazis.map'),working_directory=str(runtime),legacy_alias='maps/Playground.map',source_unchanged=str(src)))
    return folder,src

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--plan',action='store_true');parser.add_argument('--smoke-ticks',type=int,default=1024);parser.add_argument('--only-date');parser.add_argument('--build-only',action='store_true');parser.add_argument('--build-jobs',type=int,default=2);parser.add_argument('--smoke-only',action='store_true');parser.add_argument('--require-prebuilt',action='store_true');args=parser.parse_args()
    if not (OUT/'Oazis.map').exists():
        raise RuntimeError('Frozen Oazis.map is required; restore its recorded payload before resuming')
    path=OUT/'experiment.json'
    if not path.exists():raise RuntimeError('Frozen Oazis experiment.json is required; automatic plan regeneration is disabled')
    config=json.loads(path.read_text());assert sha(OUT/'Oazis.map')==config['map_sha256']
    if args.plan:
        print(json.dumps(config['snapshots'],indent=2));return
    for snap in config['snapshots']:
        if args.only_date and snap['sample_date']!=args.only_date:continue
        print('BUILD',snap['sample_date'],snap['commit'][:9],flush=True)
        if not args.build_only:save(OUT/'progress.json',dict(state='building',snapshot=snap))
        folder,src=prepare(snap)
        if args.require_prebuilt:
            record=json.loads((folder/'build.json').read_text());binary=Path(record['binary'])
            if not binary.exists() or sha(binary)!=record['binary_sha256'] or sha(folder/'adapter.patch')!=record['adapter_sha256']:
                raise RuntimeError(f'prebuilt binary validation failed: {folder}')
        else:binary=build(folder,src,args.build_jobs)
        if args.build_only:continue
        smoke=folder/'smoke'/'result.json'
        if not smoke.exists() or json.loads(smoke.read_text())['returncode']:
            result=measure(binary,src,smoke.parent,config['seeds'][0],args.smoke_ticks,120)
            if result['returncode'] or result['ticks']!=args.smoke_ticks:raise RuntimeError(f'smoke failed: {smoke.parent}')
        if args.smoke_only:continue
        def worker(slot):
            for index in range(slot,len(config['seeds']),4):
                seed=config['seeds'][index];directory=folder/'runs'/str(seed)
                if (directory/'result.json').exists():
                    prior=json.loads((directory/'result.json').read_text())
                    if prior['status']=='failed':raise RuntimeError(f'cached failure needs repair: {directory}')
                    continue
                print('RUN',snap['sample_date'],index+1,seed,'slot',slot,flush=True)
                result=measure(binary,src,directory,seed,config['tick_limit'],config['timeout_seconds'],slot)
                print('RESULT',result['status'],result['ticks'],round(result['wall_seconds'],2),'slot',slot,flush=True)
                with RESULTS_LOCK:summarize(config)
                if result['status']=='failed':raise RuntimeError(f'game failed: {directory}')
        with ThreadPoolExecutor(max_workers=4) as pool:
            futures=[pool.submit(worker,slot) for slot in range(4)]
            for future in futures:future.result()
        subprocess.run([str(OUT/'.venv/bin/python'),str(OUT/'plot_results.py')],check=True)
    if not args.build_only:summarize(config)
    if not args.build_only:
        subprocess.run([str(OUT/'.venv/bin/python'),str(OUT/'plot_results.py')],check=True)
    if args.build_only:return
    save(OUT/'progress.json',dict(state='builds_complete' if args.build_only else 'smokes_complete' if args.smoke_only else 'complete' if not args.only_date else 'subset_complete'))

if __name__=='__main__':
    try:main()
    except Exception as error:
        if '--build-only' not in os.sys.argv:save(OUT/'progress.json',dict(state='failed',error=str(error)))
        raise
