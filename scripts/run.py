import os,sys,json,subprocess,statistics,random,math,hashlib,platform
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'test'))
from benchmark_parallel_compute import execute,digest
root=Path.cwd();out=root/'artifacts/greedy-fetch/results';out.mkdir(parents=True,exist_ok=True)
binary=root/'build/darwin/client/release/src/glob2'
if len(sys.argv)>1:binary=Path(sys.argv[1]).resolve()
manifest={'source':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'binary_sha256':digest(binary),'platform':platform.platform(),'runs':[],'design':'Sequential balanced pairs. Same maps, seeds and AI settings. No impact/demand auditing. Existing cumulative gameplay counters exported only at termination. CPU measured after 16000 ticks; whole-match wall time includes warmup.'}
if (out/'manifest.json').exists():
 previous=json.loads((out/'manifest.json').read_text())
 assert previous['binary_sha256']==manifest['binary_sha256'],'do not mix different binaries in one manifest'
 manifest['runs']=previous['runs']
scenarios=[]
for seed in range(201,213):scenarios.append(('oazis',seed,root/'maps/Oazis.map.gz',['maxima']*11))
for family in ['coral','old-town']:
 for i in range(6):
  seed=1606+i*101 if family=='coral' else 2212+i*101
  fixture=root/'artifacts/greedy-fetch/maps'/f'{family}-256-{seed}.map.gz'
  scenarios.append((family,seed+50000,fixture,['numbi','castor','numbi','castor']))
for index,(family,seed,fixture,players) in enumerate(scenarios):
 if os.environ.get("GREEDY_PARTITION")=="linux" and not (6<=index<12 or index>=18):continue
 for variant in (['round-trip','greedy'] if index%2==0 else ['greedy','round-trip']):
  directory=out/f'{family}-{seed}-{variant}'
  args=['--map-file',str(fixture),'--game-seed',str(seed),*sum((['--player',p] for p in players),[]),'--ticks','26000','--benchmark-warmup','16000','--compute-experiments','hiring','--compute-threads','1','--gradient-workers','0']
  if variant=='greedy':args+=['--experiment','greedy-resource-fetching']
  if directory.exists():
   continue
  row=execute(binary,args,directory,cwd=root);row.update(family=family,seed=seed,variant=variant,input_sha256=digest(fixture));manifest['runs'].append(row)
  (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
  r=row['result'];metrics={k:sum(t['routing_comparison'][k] for t in r['teams']) for k in ['wheat_delivered','construction_completed','starvation_deaths']}
  print(family,seed,variant,round(row['wall_s'],2),metrics,flush=True)
