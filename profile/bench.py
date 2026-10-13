import hashlib,json,os,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
bins={'control':ROOT/'artifacts/gradient-cpu-ab/control/build/linux/client/release/client-full-direct-1111/src/glob2','original':OUT/'after-baseline','clear-once':OUT/'clear-once','compact-lookup':OUT/'compact-lookup'}
env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_','SDL_'))}
env.update(GLOB2_GRADIENT_BACKEND='cpu',GLOB2_GRADIENT_ALGORITHM='buckets',GLOB2_GRADIENT_VERIFY='off',SDL_AUDIODRIVER='dummy')
protocol=dict(pairs=8,counts=[1,8],sizes=[128,512],warmup=256,measured_ticks=2048,order='forward/reverse alternating',cpu_affinity='0,2,4,6,8,10,12,14',metrics='all-thread process CPU including final drain; seed/solve cumulative counters include warmup',binary_sha256={k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in bins.items()})
(OUT/'ablation-protocol.json').write_text(json.dumps(protocol,indent=2)+'\n')
rows=[]
for pair in range(8):
 for size in [128,512]:
  for count in [1,8]:
   reference=None
   for label in list(bins) if pair%2==0 else list(reversed(bins)):
    name=f'{pair}-{size}-{count}-{label}'; directory=OUT/'ablation'/name;directory.mkdir(parents=True)
    command=['taskset','-c','0,2,4,6,8,10,12,14',str(bins[label]),'game','run','--load-game',str(ROOT/f'artifacts/gradient-cpu-ab/fixtures/{size}/initial.game.gz'),'--ticks','2304','--compute-threads',str(count),'--benchmark-warmup','256','--gpu','off','--data-dir',str(ROOT/'data'),'--output-dir',str(directory),'--profile','cpu-ablation']
    started=time.monotonic()
    with (directory/'process.log').open('w') as log: result=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError(f'{name}: {result.returncode}')
    data=json.loads((directory/'result.json').read_text())
    assert data['benchmark_measured_ticks']==2048 and not data.get('gpu_requested',0) and not data.get('gradient_gpu_fields',0)
    if reference is None:reference=data['finalChecksum']
    assert reference==data['finalChecksum'],name
    rows.append(dict(pair=pair,size=size,count=count,label=label,command=command,wall_seconds=time.monotonic()-started,result=data,load=os.getloadavg()))
    (OUT/'ablation-runs.json').write_text(json.dumps(rows,indent=2)+'\n')
    print(name,round(data['benchmark_run_cpu_ns']/2048/1e6,4),'CPU ms/tick',flush=True)
print('COMPLETE',flush=True)
