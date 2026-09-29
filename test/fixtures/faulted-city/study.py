"""Reproducible supported-envelope study; run from repository root with frozen binary."""
import re
import argparse, concurrent.futures, json, os, pathlib, random, subprocess, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--out',required=True);p.add_argument('--mode',choices=['random','controls','shapes'],required=True);p.add_argument('--count',type=int,default=2000);p.add_argument('--jobs',type=int,default=8);a=p.parse_args()
controls,defaults={},{}
for line in subprocess.run([a.binary,'--list-map-generators','faulted-city'],capture_output=True,text=True).stdout.splitlines():
 match=re.match(r'\s+([\w-]+)=(-?\d+)\s+values: (.*)',line)
 if match and match[1] not in ['width','height','teams','workers']:
  controls[match[1]]=[int(re.match(r'-?\d+',v)[0]) for v in match[3].split()];defaults[match[1]]=int(match[2])
out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True);rng=random.Random(20260919);jobs=[]
def add(seed,w,h,teams,workers=4,options=None,control='',value=0):jobs.append(dict(seed=seed,w=w,h=h,teams=teams,workers=workers,options=options or {},control=control,value=value))
if a.mode=='random':
 for _ in range(a.count):
  w,h=rng.choice([(256,256),(256,512),(512,256),(512,512)]);add(rng.randrange(2**32),w,h,rng.randint(1,12),rng.randint(1,8),{k:rng.choice(v) for k,v in controls.items()})
elif a.mode=='controls':
 for seed in range(301,309):
  for k,values in controls.items():
   for v in values:add(seed,256,256,4,options={k:v},control=k,value=v)
 for w,h in [(256,256),(256,512),(512,256),(512,512)]:
  for teams in [1,12]:
   for workers in [1,8]:
    for k,values in controls.items():
     for v in [values[0],values[-1]]:add(901,w,h,teams,workers,{k:v},k,v)
else:
 for w,h in [(256,256),(256,512),(512,256),(512,512)]:
  for teams in range(1,13):
   for seed in [1,2,3]:
    for workers in [1,8]:add(seed,w,h,teams,workers)
def run(job):
 fd,path=tempfile.mkstemp(suffix='.json');os.close(fd)
 cmd=[a.binary,'--generate-map','faulted-city','--seed',str(job['seed']),'--width',str(job['w']),'--height',str(job['h']),'--teams',str(job['teams']),'--workers',str(job['workers']),'--json',path]
 for k,v in job['options'].items():cmd+=['--set',f'{k}={v}']
 start=time.monotonic();proc=subprocess.run(cmd,capture_output=True,text=True,env=dict(os.environ,SDL_VIDEODRIVER='dummy'),timeout=180)
 row=dict(job,seconds=time.monotonic()-start,returncode=proc.returncode)
 try:
  d=json.load(open(path));row['outcome']=d['generation']['outcome'];row['telemetry']=d['generation'].get('telemetry',{}).get('records',[])
  row['quality']=d['generation'].get('selection_quality')
  for k in ['terrain','resources','fertility','space']:row[k]=d.get(k)
  if not row['outcome']['success']:(out/f"failure-{job['seed']}-{job['w']}-{job['h']}-{job['teams']}.json").write_text(json.dumps(d))
 except Exception as e:row['parse_error']=str(e);row['output']=(proc.stdout+proc.stderr)[-1000:]
 os.unlink(path);return row
with (out/f'{a.mode}.jsonl').open('w') as f, concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
 failures=0
 for i,row in enumerate(pool.map(run,jobs),1):
  f.write(json.dumps(row)+'\n');f.flush();failures+=not row.get('outcome',{}).get('success',False)
  if i%25==0 or i==len(jobs):print(i,len(jobs),'failures',failures,flush=True)
