import importlib.util, concurrent.futures, json, random, os, sys
spec=importlib.util.spec_from_file_location('study','artifacts/portage-lakes/study.py');study=importlib.util.module_from_spec(spec);spec.loader.exec_module(study)
binary=os.path.abspath(sys.argv[1]);out=sys.argv[2];count=int(sys.argv[3])
controls,_=study.controls(binary,'portage-lakes');rng=random.Random(20260919);jobs=[]
for n in range(count):
 w=rng.choice([64,128,256,512]);h=rng.choice([64,128,256,512]);teams=rng.randint(1,min(12,max(2,w*h//4096)))
 settings={k:rng.choice(v) for k,v in controls.items()};settings['workers']=rng.randint(1,8)
 jobs.append(dict(binary=binary,generator='portage-lakes',study='random',control='',value=0,seed=100000+n,w=w,h=h,teams=teams,set=settings))
with concurrent.futures.ThreadPoolExecutor(8) as pool,open(out,'w') as f:
 for start in range(0,count,64):
  futures=[pool.submit(study.run,job) for job in jobs[start:start+64]]
  rows=[]
  for future in concurrent.futures.as_completed(futures):
   row=future.result();rows.append(row);f.write(json.dumps(row)+'\n');f.flush()
  bad=[r for r in rows if not r['ok']];print(start+len(rows),'completed;',len(bad),'failed in block',flush=True)
  for r in bad:print(r['seed'],r['w'],r['h'],r['teams'],r['detail'],flush=True)
  if len(bad)>5 or os.path.exists(out+'.stop'):
   print('Pause study for repair or requested checkpoint.',flush=True);break
