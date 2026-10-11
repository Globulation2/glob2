import os, sys, json, time, subprocess, pathlib, hashlib, re, struct, xml.etree.ElementTree as ET
import sprite_probe
from cpu_trial import partition, competitors, snapshot, activity
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/memory-wins'
def run(label, command, overrides=None):
    directory=OUT/label
    directory.mkdir(parents=True, exist_ok=True)
    digest=hashlib.sha256(pathlib.Path(command[0]).read_bytes()).hexdigest()
    if (directory/'result.json').exists():
        previous=json.loads((directory/'command.json').read_text())
        if previous['binary_sha256']!=digest or previous['command']!=list(map(str,command)) or previous['overrides']!=(overrides or {}):
            raise RuntimeError(f'{label}: stored results have different inputs')
        result=json.loads((directory/'result.json').read_text())
        if result['exit_code']:raise RuntimeError(f'{label}: stored run failed')
        return result
    profile=directory/'profile';profile.mkdir(exist_ok=True)
    (profile/'preferences.txt').write_text('gameSpeed=10\ntargetRenderFps=60\nhighResolutionArtwork=1\nautosaveGames=0\nmusicVolume=190\nvoiceVolume=190\nmute=0\n')
    maps=profile/'maps';maps.mkdir(exist_ok=True)
    for seed in [42,713,2026]:
        target=maps/f'lava-{seed}.map.gz'
        if not target.exists():target.symlink_to(ROOT/f'artifacts/memory-lava/map-seed{seed}.map.gz')
    env=dict(os.environ,GLOB2_USER_DATA_DIR=str(profile),SDL_VIDEODRIVER='x11',SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0')
    for key in ['MALLOC_ARENA_MAX','GLOB2_MALLOC_LOG','GLOB2_PERF_DISABLE','GLOB2_ASSET_THREADS','GLOB2_ASSET_IO_THREADS','GLOB2_ASSET_MEMORY_MB','GLOB2_EXPERIMENT_TEXTURE_DIR','GLOB2_ASSET_DIR','GLOB2_ASSET_BENCHMARK_HD','GLOB2_ASSET_BENCHMARK_RELOAD','GLOB2_ASSET_BENCHMARK_GPU','GLOB2_TEAM_TIMELINE','GLOB2_TEAM_RESULTS','GLOB2_CHECKSUM_SIDECAR','GLOB2_TEST_SEED','GLOB2_SIM_THREAD']:
        env.pop(key,None)
    for key in list(env):
        if key.startswith('GLOB2_BENCH_'):env.pop(key)
    env.pop('GLOB2_CHECKSUM_SIDECAR_MAX_TICKS',None)
    env['LD_LIBRARY_PATH']=str(OUT/'lib/glob2')
    env.update(overrides or {})
    reservation=partition()
    record={'cpu_reservation':reservation,'command':list(map(str,command)),'overrides':overrides or {},'binary_sha256':digest}
    (directory/'command.json').write_text(json.dumps(record,indent=2))
    symbols=subprocess.check_output(['nm','-C',str(command[0])],text=True)
    match=re.search(r'^([0-9a-f]+) [BbDd] GAGCore::glState$',symbols,re.M)
    gl_symbol=int(match[1],16) if match else None
    registry_match=re.search(r'^([0-9a-f]+) [BbDd] GAGCore::loadedSprites$',symbols,re.M)
    registry_symbol=int(registry_match[1],16) if registry_match else None
    is_pie=struct.unpack_from('<H',pathlib.Path(command[0]).read_bytes(),16)[0]==3
    gl_address=None;other_gpu_samples=[];last_gpu=0;last_sprite=0;gpu_samples=[];sprite_samples=[]
    isolated=env.get('GLOB2_ISOLATED_TRIAL')=='1'
    if isolated:
        announced=False
        while competitors():
            if not announced:print(f'{label}: waiting for builds or games to finish',flush=True);announced=True
            time.sleep(1)
    # Warm only the files consumed by this workload; every process still starts cold.
    # Apply the same cache condition without flushing any machine-wide caches.
    if isolated:
        assets=ROOT/'build/linux/client/release/client-full-direct-1111/runtime-assets'
        for file in assets.rglob('*'):
            if file.is_file():
                with file.open('rb') as cached:
                    while cached.read(1024*1024):pass
    contamination=[];last_check=0
    samples=[];peak=0;cpu_before=snapshot();start=time.monotonic()
    with (directory/'stdout.log').open('w') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        while True:
            pid, status, usage=os.wait4(process.pid,os.WNOHANG)
            if pid:
                process.returncode=os.waitstatus_to_exitcode(status);break
            if isolated and time.monotonic()-last_check>=1:
                contamination.extend((time.monotonic()-start,pid,name) for pid,name in competitors() if pid!=process.pid)
                last_check=time.monotonic()
                if contamination:
                    process.terminate()
            try:
                data=pathlib.Path(f'/proc/{process.pid}/status').read_text()
                row={key:int(value) for key,value in re.findall(r'^(VmRSS|VmHWM|VmSwap|Threads):\s+(\d+)',data,re.M)}
                row['seconds']=time.monotonic()-start
                if env.get('GLOB2_PROFILE_GPU') and gl_symbol:
                    if gl_address is None:
                        base=0
                        if is_pie:
                            for line in pathlib.Path(f'/proc/{process.pid}/maps').read_text().splitlines():
                                cols=line.split()
                                if len(cols)>5 and cols[-1]==str(command[0]) and int(cols[2],16)==0:
                                    base=int(cols[0].split('-')[0],16);break
                        if base or not is_pie:gl_address=base+gl_symbol
                    if gl_address:
                        try:
                            with open(f'/proc/{process.pid}/mem','rb',buffering=0) as memory:
                                memory.seek(gl_address);gl=memory.read(32)
                            row['texture_bytes']=struct.unpack_from('<Q',gl,24)[0]
                            row['texture_count']=struct.unpack_from('<I',gl,20)[0]
                        except (OSError,struct.error):pass
                    if gl_address and registry_symbol and row['seconds']-last_sprite>=30:
                        try:
                            sprites=sprite_probe.capture(process.pid,gl_address-gl_symbol+registry_symbol)
                            sprites['seconds']=row['seconds'];sprite_samples.append(sprites)
                        except (OSError,ValueError,struct.error):pass
                        last_sprite=row['seconds']
                    if row['seconds']-last_gpu>=float(env.get('GLOB2_PROFILE_GPU_INTERVAL','5')):
                        gpu=subprocess.run(['nvidia-smi','-q','-x'],capture_output=True,text=True)
                        try:
                            entries=[];others=[]
                            for info in ET.fromstring(gpu.stdout).findall('.//process_info'):
                                if info.findtext('pid')==str(process.pid):entries.append({child.tag:child.text for child in info})
                                else:others.append({child.tag:child.text for child in info})
                            other_gpu_samples.append({'seconds':row['seconds'],'processes':others})
                            gpu_samples.append({'seconds':row['seconds'],'processes':entries})
                        except ET.ParseError:pass
                        last_gpu=row['seconds']
                samples.append(row)
                if row.get('VmRSS',0)>peak:
                    peak=row['VmRSS'];(directory/'peak-status.txt').write_text(data)
            except (OSError,ValueError):pass
            time.sleep(.1)
    cpu_after=snapshot()
    raw=(directory/'stdout.log').read_text()
    result={'cpu_reservation':reservation,'cpu_before':cpu_before,'cpu_after':cpu_after,'cpu_activity':activity(cpu_before,cpu_after,usage.ru_utime+usage.ru_stime),'exit_code':process.returncode,'wall_seconds':time.monotonic()-start,'process_cpu_seconds':usage.ru_utime+usage.ru_stime,'kernel_peak_rss_kib':usage.ru_maxrss,'competing_builds_or_games':contamination,'samples':samples,'gpu_samples':gpu_samples,'other_gpu_samples':other_gpu_samples,'sprite_samples':sprite_samples,'asset':[]}
    for line in raw.splitlines():
        if line.startswith('ASSET_BENCHMARK '):
            result['asset'].append(dict(part.split('=',1) for part in line.split()[1:]))
    (directory/'result.json').write_text(json.dumps(result,indent=2))
    if contamination:
        archived=directory.with_name(directory.name+'-interrupted-'+str(time.time_ns()))
        directory.rename(archived)
        print(f'{label}: interrupted by a build or game; retrying in a fresh process/profile',flush=True)
        return run(label,command,overrides)
    if process.returncode:raise RuntimeError(f'{label}: exit {process.returncode}')
    return result
if __name__=='__main__':
    spec=json.loads(pathlib.Path(sys.argv[1]).read_text())
    for case in spec:
        data=run(case['label'],case['command'],case.get('env'))
        print(json.dumps({'label':case['label'],'rss_mib':data['kernel_peak_rss_kib']/1024,'cpu_seconds':data['process_cpu_seconds'],'asset':data['asset']}),flush=True)
