import importlib.util, concurrent.futures, json, random, os, sys
spec=importlib.util.spec_from_file_location('study','artifacts/portage-lakes/study.py');study=importlib.util.module_from_spec(spec);spec.loader.exec_module(study)
binary=os.path.abspath(sys.argv[1]);out=sys.argv[2];count=int(sys.argv[3])
controls,_=study.controls(binary,'portage-lakes');rng=random.Random(20260919);jobs=[]
for n in range(count):
 w=rng.choice([64,128,256,512]);h=rng.choice([64,128,256,512]);teams=rng.randint(1,min(12,max(2,w*h//4096)))
 settings={k:rng.choice(v) for k,v in controls.items()};settings['workers']=rng.randint(1,8)
 jobs.append(dict(binary=binary,generator='portage-lakes',study='random',control='',value=0,seed=100000+n,w=w,h=h,teams=teams,set=settings))
existing=[json.loads(line) for line in open(out)] if os.path.exists(out) else []
seen={r['seed'] for r in existing}
assert len(seen)==len(existing)
for r in existing:
 j=jobs[r['seed']-100000]
 assert all(r[k]==j[k] for k in ('seed','w','h','teams','set'))
remaining=[j for j in jobs if j['seed'] not in seen]
print('Resuming',len(existing),'completed;',len(remaining),'remaining',flush=True)
with concurrent.futures.ThreadPoolExecutor(8) as pool,open(out,'a') as f:
 futures=[pool.submit(study.run,job) for job in remaining]
 done=len(existing);bad=sum(not r['ok'] for r in existing)
 for future in concurrent.futures.as_completed(futures):
  row=future.result();f.write(json.dumps(row)+'\n');f.flush();done+=1;bad+=not row['ok']
  if not row['ok']:print('FAIL',row['seed'],row['w'],row['h'],row['teams'],row['detail'],flush=True)
  if done%64==0 or done==count:print(done,'completed;',bad,'total failed',flush=True)
