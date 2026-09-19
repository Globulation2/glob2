"""Held-out native CLI matrix; uses the skill's existing sample runner and serializer."""
import importlib.util,json,random,sys,os
from concurrent.futures import ThreadPoolExecutor
spec=importlib.util.spec_from_file_location('study','.agents/skills/glob2-map-design/scripts/control_study.py')
study=importlib.util.module_from_spec(spec);spec.loader.exec_module(study)
binary,out=sys.argv[1:3]
count=int(sys.argv[3]) if len(sys.argv)>3 else 2000
controls,defaults=study.controls(binary,'gauntlet');rng=random.Random(90428)
jobs=[]
for n in range(count):
 w,h=rng.choice([(256,256),(256,512),(512,256),(512,512)])
 settings={k:rng.choice(v) for k,v in controls.items()};settings['workers']=rng.randint(1,8)
 jobs.append(dict(binary=binary,generator='gauntlet',study='held-out',control='',value=0,seed=200000+n,w=w,h=h,
  teams=rng.randint(2,12 if w==h==512 else 8),set=settings))
bad=0
with ThreadPoolExecutor(6) as pool,open(out,'w') as f:
 for row in pool.map(study.run,jobs):
  f.write(json.dumps(row)+'\n');f.flush();bad+=not row['ok']
print(count,'supported requests;',bad,'failures')
sys.exit(bool(bad))
