import hashlib,json,os,subprocess
from pathlib import Path
root=Path.cwd();base=root/'artifacts/final-pr-review';binary=root/'build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
results=[]
for name,teams in [('Holiday_Island_2',4),('Oazis',11)]:
    dest=base/'gpu'/name;dest.mkdir(parents=True,exist_ok=True)
    cmd=[str(binary),'game','run','--map-file',str(root/f'maps/{name}.map.gz'),'--game-seed','42','--ticks','2048','--compute-threads','4','--gpu','on','--gradient-backend','opencl','--gradient-algorithm','frozen8','--telemetry','checksums','--output-dir',str(dest/'game')]
    for t in range(teams):cmd+=['--player',('nicowar','econo')[t%2]]
    item={'map':name,'command':cmd};env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    with (dest/'run.log').open('w') as log:
        try: item['exit_code']=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180).returncode
        except subprocess.TimeoutExpired:item['timeout_seconds']=180;item['exit_code']=124
    if item['exit_code']==0:
        data=json.loads((dest/'game/result.json').read_text());item['device']=data.get('gradient_device');item['gpu_fields']=data.get('gradient_gpu_fields');item['gpu_error']=data.get('gpu_error');item['fallbacks']=data.get('gradient_cpu_fallbacks');item['matched']={}
        for file in ('world.checksums','game.replay.checksums'):
            a=(dest/'game'/file).read_bytes();b=(base/'matches'/f'{name}-p4'/file).read_bytes();item['matched'][file]=a==b
            item.setdefault('trace_sha256',{})[file]=hashlib.sha256(a).hexdigest()
        item['qualified']=bool(item['gpu_fields']) and all(item['matched'].values())
    else:item['qualified']=False
    results.append(item);print(json.dumps(item),flush=True)
(base/'gpu-comparison.json').write_text(json.dumps(results,indent=2)+'\n')
