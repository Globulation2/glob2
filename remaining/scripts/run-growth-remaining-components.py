from pathlib import Path
import subprocess,os,json,sys
sys.path.insert(0,"test")
from benchmark_parallel_compute import digest
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/'components';out.mkdir(exist_ok=True)
tags=['control','A','B','C','D','AB','ABCD']
hashes={tag:digest(base/(tag+'-components')) for tag in tags}
(out/'metadata.json').write_text(json.dumps({'binaries':hashes,'affinity':sorted(os.sched_getaffinity(0)),'repeats':10,'warmups':1},indent=2))
for rep in range(-1,10):
 shift=(rep+1)%len(tags);order=tags[shift:]+tags[:shift]
 for tag in order:
  dest=out/tag/(str(rep)+'.json');dest.parent.mkdir(exist_ok=True)
  if dest.exists():continue
  with dest.with_suffix('.log').open('w') as log:
   subprocess.run([str(base/(tag+'-components')),'--test-case=remaining growth component experiments*','--no-skip'],env=dict(os.environ,GLOB2_REMAINING_COMPONENT_OUTPUT=str(dest),GLOB2_REMAINING_COMPONENT_REPEAT=str(rep)),stdout=log,stderr=subprocess.STDOUT,check=True)
 print('components repetition',rep,'complete',flush=True)
for tag in tags:
 rows=sum((json.load(open(out/tag/(str(rep)+'.json')))['samples'] for rep in range(-1,10)),[])
 (out/(tag+'.json')).write_text(json.dumps({'samples':rows},indent=2))
# All variants process exactly the same stocks/proposals. Capture bytes deliberately differ.
reference=json.load(open(out/'control.json'))['samples']
for tag in ['A','B','C','D','AB','ABCD']:
 rows=json.load(open(out/(tag+'.json')))['samples'];assert len(rows)==len(reference)
 for a,b in zip(reference,rows):
  for k in ('stage','repeat','multi','size','pattern','retained','food','proposals','accepted','rejected','clamped','tiles','stock'):
   if k in a:assert a[k]==b[k],(tag,k,a,b)
(out/'verification.json').write_text(json.dumps({'identical_work':True,'variants':7,'samples_per_variant':len(reference)},indent=2))

for tag,h in hashes.items():assert digest(base/(tag+"-components"))==h
