import pathlib,subprocess,hashlib,json,os
r=pathlib.Path('artifacts/who-ate-the-map').resolve();env=dict(os.environ,GLOB2_USER_DIR=str(r/'profile'));rows=[]
shapes=[(128,128),(128,256),(256,128),(256,256),(256,512),(512,256),(512,512)]
for w,h in shapes:
 for a in range(3):
  for seed in [7,11]:
   teams=4 if min(w,h)==128 else 8;cmd=['--generate-map','who-ate-the-map','--width',str(w),'--height',str(h),'--teams',str(teams),'--seed',str(seed),'--set',f'appetite={a}','--output',str(r/'equivalence/performance.map')];hashes=[]
   for version in ['v10','final']:
    p=subprocess.run([str(r/version/'glob2')]+cmd,capture_output=True,env=env);assert p.returncode==0,p.stderr;hashes.append(hashlib.sha256((r/'equivalence/performance.map').read_bytes()).hexdigest())
   row=dict(w=w,h=h,teams=teams,appetite=a,seed=seed,hashes=hashes,equal=hashes[0]==hashes[1]);rows.append(row);assert row['equal'],row
   print(len(rows),'equal',flush=True)
(r/'equivalence/performance.map').unlink();(r/'equivalence/performance.json').write_text(json.dumps(rows,indent=2))
