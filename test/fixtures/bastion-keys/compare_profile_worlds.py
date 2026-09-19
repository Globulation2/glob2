# Compare frozen ID-67 binaries, not current ID-67 (Drowned Forest).
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json,subprocess,tempfile,hashlib,shutil
root=Path.cwd();base=root/'artifacts/bastion-keys';before=base/'profile-before/glob2';after=base/'profile-after/glob2'
jobs=[(seed,w,h,n,level) for seed in range(1,9) for w,h,n in [(7,7,1),(8,7,2),(8,8,4),(9,9,12)] for level in range(3)]
def run(job):
 seed,w,h,n,level=job;params={'teams':n,'width':w,'height':h,'workers':[1,4,8][level],'home-size':[13,14,15][level],'plantation-size':[14,16,18][level],'outer-islands':[1,3,5][level]}
 params.update({r+'-amount':[0,100,300][level] for r in ['wheat','wood','stone','algae','fruit']});hashes=[]
 with tempfile.TemporaryDirectory(prefix='bastion-profile-compare-') as tmp:
  for label,binary,method in [('before',before,67),('after',after,67)]:
   out=Path(tmp)/label;cmd=[str(binary),'--generate-map','--generator',str(method),'--map-seed',str(seed),'--write-map','true','--output-dir',str(out)]
   for k,v in params.items():cmd+=['--param',f'{k}={v}']
   r=subprocess.run(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
   assert r.returncode==0,(job,label,r.stderr)
   hashes.append(hashlib.sha256((out/'map-r0.map').read_bytes()).hexdigest())
  if hashes[0]!=hashes[1]:shutil.copytree(tmp,base/f'profile-mismatch-{seed}-{w}-{h}-{level}',dirs_exist_ok=True)
 return dict(seed=seed,params=params,before=hashes[0],after=hashes[1],identical=hashes[0]==hashes[1])
with ThreadPoolExecutor(4) as pool,(base/'profile-world-comparison.jsonl').open('w') as f:
 for row in pool.map(run,jobs):f.write(json.dumps(row)+'\n');f.flush()
print('Compared',len(jobs),'native maps')
