import importlib.util,concurrent.futures,json,os,sys
s=importlib.util.spec_from_file_location('study','artifacts/portage-lakes/study.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
binary=os.path.abspath(sys.argv[1]);output=sys.argv[2];controls,defaults=m.controls(binary,'portage-lakes');jobs=[]
def add(w,h,teams,seed,settings,study):jobs.append(dict(binary=binary,generator='portage-lakes',study=study,control='',value=0,seed=seed,w=w,h=h,teams=teams,set=settings))
for w in [64,128,256,512]:
 for h in [64,128,256,512]:
  cap=min(12,max(2,w*h//4096))
  for teams in range(1,cap+1):
   for seed,workers in [(7,1),(31,8)]:add(w,h,teams,seed,{'workers':workers},'envelope')
  for high in [False,True]:add(w,h,cap,97,{k:v[-1 if high else 0] for k,v in controls.items()},'corners')
for w in [64,256]:
 for teams in [1,2]:
  for seed in range(1,9):
   for value in controls['extra-trails']:add(w,w,teams,seed,{'extra-trails':value},'small-trails')
with concurrent.futures.ThreadPoolExecutor(4) as pool,open(output,'w') as f:
 futures=[pool.submit(m.run,j) for j in jobs];count=bad=0
 for future in concurrent.futures.as_completed(futures):
  row=future.result();f.write(json.dumps(row)+'\n');f.flush();count+=1;bad+=not row['ok']
  if not row['ok']:print(row['w'],row['h'],row['teams'],row['seed'],row['set'],row['detail'],flush=True)
  if count%32==0:print(count,'/',len(jobs),'failed',bad,flush=True)
print('Done',count,'failed',bad,flush=True)
