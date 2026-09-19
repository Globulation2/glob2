#!/usr/bin/env python3
"""Compare final map-only wall time to Forts on the same large request."""
import json, statistics, subprocess, sys, time
from pathlib import Path
binary=sys.argv[1];root=Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=True)
rows=[]
for seed in range(1,4):
 for generator in (['gauntlet','forts'] if seed%2 else ['forts','gauntlet']):
  command=[binary,'--generate-map',generator,'--seed',str(seed),'--width','512','--height','512','--teams','12','--output',str(root/f'{generator}-{seed}.map')]
  start=time.perf_counter();p=subprocess.run(command,capture_output=True,text=True)
  rows.append(dict(generator=generator,seed=seed,seconds=time.perf_counter()-start,exit=p.returncode,stdout=p.stdout,stderr=p.stderr,command=command))
result={'runs':rows,'medians':{name:statistics.median(r['seconds'] for r in rows if r['generator']==name) for name in ['gauntlet','forts']}}
(root/'timings.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result['medians']))
assert all(r['exit']==0 for r in rows)
