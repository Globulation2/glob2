from pathlib import Path
import subprocess,json
from concurrent.futures import ThreadPoolExecutor
ROOT=Path.cwd();OUT=ROOT/'artifacts/pending-direction-wide/map-inventory';BIN=ROOT/'artifacts/pending-direction/glob2-probe'
def inspect(p):
 out=OUT/(p.name+'.json');r=subprocess.run([str(BIN),'map','preview',str(p),'--report-file',str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 if r.returncode:return {'path':str(p),'error':r.stdout[-500:]}
 d=json.loads(out.read_text());m=d['map'];return {'name':p.name,'path':str(p),'width':m.get('width'),'height':m.get('height'),'teams':m.get('teams',len(d['start_position_euclidean_distances']))}
rows=list(ThreadPoolExecutor(max_workers=2).map(inspect,sorted((ROOT/'maps').glob('*.map.gz'))));(OUT/'index.json').write_text(json.dumps(rows,indent=2))
for r in rows:print({k:v for k,v in r.items() if k!='path'},flush=True)
