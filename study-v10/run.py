import json,gzip,subprocess,os,random,hashlib,time,sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
root=Path('artifacts/who-ate-the-map').resolve();out=root/'study-v10';binary=root/'v10/glob2'
controls=['wheat-amount','wood-amount','stone-amount','algae-amount','fruit-amount']
def run(job):
 idx=job['id']; p=out/f'{idx}.json';cmd=[str(binary),'--generate-map','who-ate-the-map','--width',str(job['w']),'--height',str(job['h']),'--teams',str(job['teams']),'--workers',str(job['workers']),'--seed',str(job['seed']),'--json',str(p)]
 for k,v in job['params'].items():cmd+=['--set',f'{k}={v}']
 start=time.monotonic();r=subprocess.run(cmd,capture_output=True,text=True,env=dict(os.environ,GLOB2_USER_DIR=str(root/'profile')))
 d=json.loads(p.read_text()) if p.exists() else {};p.with_suffix('.json.gz').write_bytes(gzip.compress(json.dumps(d).encode()));p.unlink(missing_ok=True)
 row=dict(job,rc=r.returncode,seconds=time.monotonic()-start)
 row['error']=d.get('generation',{}).get('outcome',{}).get('detail',r.stderr[-400:]) if r.returncode else ''
 row['metrics']={}
 if not r.returncode:
  row['metrics']['grass']=d['terrain']['grass']['percent']
  for k,v in d['resources']['types'].items():row['metrics'][k]=v['coverage']['tiles']
  row['metrics']['build_sites']=d['space']['build_sites_4x4']
 for rec in d.get('generation',{}).get('telemetry',{}).get('records',[]):
  if rec['key'].startswith('eaten.') and rec.get('subject') is None:row['metrics'][rec['key']]=rec['value']
 return row
shapes=[(128,128),(128,256),(256,128),(256,256),(256,512),(512,256),(512,512)]
mode=sys.argv[1] if len(sys.argv)>1 else 'pilot';jobs=[]
def add(w,h,teams,workers,seed,params,study,control='',value=0):
 jobs.append(dict(id=f'{mode}-{len(jobs):05d}',w=w,h=h,teams=teams,workers=workers,seed=seed,params=params,study=study,control=control,value=value))
if mode=='pilot':
 for w,h in shapes:
  for teams in [1,4 if min(w,h)==128 else 8]:
   for a in range(3):
    for amount in [0,100,300]:
     add(w,h,teams,8,71,dict(appetite=a,**{k:amount for k in controls}),'extremes')
elif mode=='oat':
 for side in [128,256,512]:
  for seed in range(1,5):
   for k in ['appetite']+controls:
    for v in (range(3) if k=='appetite' else range(0,301,25)):
     add(side,side,4,4,seed,{k:v},'oat',k,v)
elif mode=='random':
 rng=random.Random(719205)
 for i in range(2000):
  w,h=rng.choice(shapes); params={k:rng.randrange(13)*25 for k in controls};params['appetite']=rng.randrange(3)
  add(w,h,rng.randint(1,4 if min(w,h)==128 else 8),rng.randint(1,8),rng.randrange(100000,100000000),params,'random')
elif mode=='envelope':
 for w in [64,128,256,512]:
  for h in [64,128,256,512]:
   for teams in range(1,13):
    add(w,h,teams,4,19,{},'envelope')
(out/f'{mode}-manifest.json').write_text(json.dumps(dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),jobs=jobs),indent=2))
with open(out/f'{mode}.jsonl','w') as f:
 for n,row in enumerate(ThreadPoolExecutor(4).map(run,jobs),1):
  f.write(json.dumps(row)+'\n');f.flush()
  if row['rc'] or n%100==0:print(n,'/',len(jobs),row['id'],row['rc'],row['error'],flush=True)
