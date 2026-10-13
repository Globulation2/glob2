import hashlib,json,os,resource,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];OUT=Path(__file__).resolve().parent
bins={'before':ROOT/'artifacts/gradient-cpu-ab/before/build/linux/client/release/client-full-direct-1111/src/glob2','master':ROOT/'artifacts/gradient-cpu-profile/master-baseline','fixed':ROOT/'artifacts/gradient-cpu-audit/candidate-release'}
fixtures=json.loads((ROOT/'artifacts/gradient-cpu-ab/fixtures.json').read_text())
env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_','SDL_'))}
env.update(GLOB2_GRADIENT_BACKEND='cpu',GLOB2_GRADIENT_ALGORITHM='buckets',GLOB2_GRADIENT_VERIFY='off',SDL_AUDIODRIVER='dummy')
protocol=dict(base='3e2ba6c07',source_hashes=json.loads((ROOT/'artifacts/gradient-cpu-audit/candidate-source-hashes.json').read_text()),pairs=8,counts=[1,2,4,8],fixtures=fixtures,warmup=256,measured_ticks=2048,weights='equal fixture/count weights; pair-level cohort averages',ci='Student t paired 95%, eight run pairs',order='before/master/fixed on even pairs, reverse on odd pairs',cpu_affinity='0,2,4,6,8,10,12,14',cpu_window='all-thread process CPU, excluding setup/warmup, including final drain',phase_counters='full simulation including warmup, normalized by completed periodic fields',binary_sha256={k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in bins.items()})
(OUT/'final-protocol.json').write_text(json.dumps(protocol,indent=2)+'\n')
rows=[]
def run(label,variant,fixture,count,timed=False,pair=None,save=False):
 directory=OUT/'final'/label;directory.mkdir(parents=True)
 command=['taskset','-c','0,2,4,6,8,10,12,14',str(bins[variant]),'game','run','--load-game',fixture['save'],'--ticks',str(fixture['start']+2304),'--compute-threads',str(count),'--gpu','off','--data-dir',str(ROOT/'data'),'--output-dir',str(directory),'--profile','cpu-final']
 command+=['--benchmark-warmup','256'] if timed else ['--telemetry','checksums']
 if save:command+=['--save','final','--write-replay']
 prior=resource.getrusage(resource.RUSAGE_CHILDREN);started=time.monotonic()
 with (directory/'process.log').open('w') as log:r=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
 cpu=resource.getrusage(resource.RUSAGE_CHILDREN)
 if r.returncode:raise RuntimeError(f'{label}: exit {r.returncode}')
 d=json.loads((directory/'result.json').read_text());assert not d.get('gpu_requested',0) and not d.get('gradient_gpu_fields',0)
 if timed:assert d['benchmark_measured_ticks']==2048
 row=dict(label=label,variant=variant,fixture=fixture['name'],count=count,timed=timed,pair=pair,command=command,result=d,wall_seconds=time.monotonic()-started,total_process_cpu_seconds=cpu.ru_utime+cpu.ru_stime-prior.ru_utime-prior.ru_stime,load=os.getloadavg())
 rows.append(row);(OUT/'final-runs.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(label,round(d['benchmark_run_cpu_ns']/2048/1e6,4) if timed else 'checksums',flush=True)
 return directory,d
for fixture in fixtures:
 reference=None
 for count in [1,2,4,8]:
  for variant in bins:
   directory,data=run(f'validate-{fixture["name"]}-{count}-{variant}',variant,fixture,count,save=True)
   checksums=(directory/'world.checksums').read_bytes()
   if reference is None:reference=checksums
   assert checksums==reference,(fixture,count,variant)
for pair in range(8):
 for fixture in fixtures:
  for count in [1,2,4,8]:
   checksum=None
   for variant in bins if pair%2==0 else reversed(bins):
    directory,d=run(f'pair-{pair}-{fixture["name"]}-{count}-{variant}',variant,fixture,count,True,pair)
    if checksum is None:checksum=d['finalChecksum']
    assert checksum==d['finalChecksum'],directory
print('COMPLETE',flush=True)
