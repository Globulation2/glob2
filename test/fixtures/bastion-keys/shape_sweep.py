from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys,json,collections
sys.path.insert(0,str(Path.cwd()))
from tools.map_generation_study import generate_map
jobs=[(w,h,n,s) for w in (64,128,256,512) for h in (64,128,256,512) for n in range(1,13) for s in (401,402,403)]
def run(j):
 w,h,n,s=j
 r=generate_map(Path.cwd()/'build/src/glob2',68,s,w,h,n,{'workers':8 if s==402 else 1})
 r['request']=j;r['expected_supported']=w>=128 and h>=128 and n<=min(12,(w//128)*(h//128));return r
with ThreadPoolExecutor(4) as p,open('/tmp/bastion-swim-controls/shapes.jsonl','w') as f:
 for r in p.map(run,jobs):f.write(json.dumps(r)+'\n')
