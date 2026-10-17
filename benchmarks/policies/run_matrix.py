import argparse,array,hashlib,json,os,platform,re,subprocess,sys,time
from pathlib import Path
ROOT=Path.cwd();OUT=ROOT/'artifacts/gradient-admission-policies-20261016'
BINARY=ROOT/'build/darwin/client/release/client-full-direct-1111/src/glob2'
MAP=ROOT/'maps/Holiday_Island_2.map.gz'
PLAYERS=['nicowar','econo','castor','warrush']
POLICIES=['capped','global8','global16','team2','team4','all']
parser=argparse.ArgumentParser();parser.add_argument('--smoke',action='store_true');args=parser.parse_args()
OUT.mkdir(parents=True,exist_ok=True)
identity={'binary':str(BINARY),'binary_sha256':hashlib.sha256(BINARY.read_bytes()).hexdigest(),'base_revision':'43b153739838207983c922ab4cbeed7773459de9','snapshot_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'map':str(MAP),'map_sha256':hashlib.sha256(MAP.read_bytes()).hexdigest(),'platform':platform.platform(),'architecture':platform.machine(),'players':PLAYERS,'ticks':32768,'participants':[1,4],'seeds':list(range(42,74)),'policies':POLICIES,'run_order':'Serial runs. Rotate six-policy order by seed index, reverse on odd index; alternate participant 1/4 order by seed. Same seeds across all policies/configurations.','build_command':'PYTHONPATH=. CCACHE=1 scons -j8 release=1 client_profile=full server=0','tick_sample_format':'native unsigned 64-bit nanoseconds, chronological; host byte order '+sys.byteorder}
(OUT/'experiment.json').write_text(json.dumps(identity,indent=2))
def run(mode,seed,ticks,participants=4,smoke=False):
    name=('smoke-' if smoke else '')+f'seed-{seed}-{mode}-p{participants}'
    path=OUT/name;path.mkdir(parents=True,exist_ok=True)
    result=path/'game/result.json'
    if result.exists() and (path/'run.json').exists():
        old=json.loads((path/'run.json').read_text())
        if old.get('exit_code')==0:
            if old['binary_sha256']!=identity['binary_sha256']:raise RuntimeError('Existing run uses another executable')
            return json.loads(result.read_text())
    while subprocess.run(['pgrep','-x','clang'],stdout=subprocess.DEVNULL).returncode==0:
        print('WAITING: unrelated compiler active; no benchmark started',flush=True)
        time.sleep(15)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(path/'profile'),GLOB2_TEST_SEED=str(seed),GLOB2_BUILDING_ADMISSION=mode,GLOB2_BENCH_TICKS=str(path/'tick-ns.u64'))
    for key in ['GLOB2_BUILDING_LATENCY_CSV','GLOB2_TEAM_TIMELINE','GLOB2_CHECKSUM_SIDECAR','GLOB2_TEAM_RESULTS','GLOB2_PERF_DISABLE']:env.pop(key,None)
    command=[str(BINARY),'game','run','--map-file',str(MAP),'--game-seed',str(seed),'--ticks',str(ticks),'--compute-threads',str(participants),'--compute-telemetry','waits','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--output-dir',str(path/'game')]
    for ai in PLAYERS:command+=['--player',ai]
    if smoke:command+=['--telemetry','checksums']
    manifest={'command':command,'timing_wrapper':['/usr/bin/time','-l'],'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':identity['binary_sha256'],'load_average_before':os.getloadavg()}
    (path/'run.json').write_text(json.dumps(manifest,indent=2));begin=time.monotonic()
    with (path/'stdout.log').open('w') as out,(path/'stderr.log').open('w') as err:
        process=subprocess.run(['/usr/bin/time','-l',*command],cwd=ROOT,env=env,stdout=out,stderr=err)
    manifest.update(exit_code=process.returncode,elapsed_seconds=time.monotonic()-begin,load_average_after=os.getloadavg())
    stderr=(path/'stderr.log').read_text(errors='replace');rss=re.search(r'(\d+)\s+maximum resident set size',stderr)
    manifest['peak_rss_bytes']=int(rss.group(1)) if rss else None
    (path/'run.json').write_text(json.dumps(manifest,indent=2))
    if process.returncode:raise RuntimeError(f'{name} failed with {process.returncode}: '+stderr[-2000:])
    value=json.loads(result.read_text())
    assert value['building_gradient_jobs']==value['building_gradient_worker_jobs_measured'],name
    assert value['benchmark_measured_ticks']>0,name
    samples=array.array('Q');samples.frombytes((path/'tick-ns.u64').read_bytes());assert len(samples)==value['benchmark_measured_ticks'],name
    if mode in ['capped','global8','global16']:assert value['building_gradient_max_batch']<={'capped':4,'global8':8,'global16':16}[mode],name
    if mode in ['team2','team4']:assert value['building_gradient_max_team_batch']<={'team2':2,'team4':4}[mode],name
    if mode=='all':assert value['building_gradient_max_queued_after_boundary']==0,name
    assert sum(value['building_gradient_queue_delay_histogram'])==value['building_gradient_queue_delay_samples'],name
    if mode=='all':assert value['building_gradient_queue_delay_max']==0,name
    print(json.dumps({'run':name,'ticks':value['benchmark_measured_ticks'],'process_cpu_s':round(value['benchmark_run_cpu_ns']/1e9,3),'gradient_cpu_s':round(value['building_gradient_worker_cpu_ns']/1e9,3),'gradient_jobs':value['building_gradient_jobs'],'max_batch':value['building_gradient_max_batch'],'max_pending':value['building_gradient_max_pending_observed'],'tick_p99_ms':round(value['tick_p99_ns']/1e6,3),'elapsed_s':round(manifest['elapsed_seconds'],2)}),flush=True)
    return value
if args.smoke:
    for mode in POLICIES:
        a=run(mode,42,4096,1,True);b=run(mode,42,4096,4,True)
        assert a['initialChecksum']==b['initialChecksum'] and a['finalChecksum']==b['finalChecksum'],mode
        for file in ['world.checksums','game.replay.checksums']:
            x=OUT/f'smoke-seed-42-{mode}-p1/game'/file;y=OUT/f'smoke-seed-42-{mode}-p4/game'/file
            assert x.read_bytes()==y.read_bytes(),(mode,file)
    print('SMOKE CHECKS PASSED: cap/all policy invariants and 1/4-participant checksum equivalence',flush=True)
else:
    for index,seed in enumerate(range(42,74)):
        modes=POLICIES[index%6:]+POLICIES[:index%6]
        if index%2:modes=list(reversed(modes))
        results={}
        for participants in ([1,4] if index%2==0 else [4,1]):
            pair={mode:run(mode,seed,32768,participants) for mode in modes}
            assert len({v['initialChecksum'] for v in pair.values()})==1,seed
            results[participants]=pair
        for mode in modes:
            assert results[1][mode]['finalChecksum']==results[4][mode]['finalChecksum'],(seed,mode)
        print(f'PAIR COMPLETE {index+1}/32 seed={seed}',flush=True)
    print('ALL 32 SEED PAIRS COMPLETE',flush=True)
