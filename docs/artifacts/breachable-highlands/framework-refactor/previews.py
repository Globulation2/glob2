"""Extract selected existing paired-job maps and invoke the native preview command."""
import json,subprocess,sys
from pathlib import Path
from tools.tournaments.results import Results
s=Results(sys.argv[1]);out=Path(sys.argv[2]).resolve();out.mkdir(exist_ok=True)
summary=json.loads(Path(sys.argv[3]).read_text())
wanted={summary['metrics'][metric][end]:name for metric,end,name in [
 ('min_local_fit4','minimum_job','least-room'),('fairness','minimum_job','least-fair'),
 ('worst_nearest_rival_walk_cost','maximum_job','longest-rival-route')]}
index=[]
for r in s:
 labels=r['job']['labels'];original=labels['original_job']
 if labels['variant']!='shared' or original not in wanted:continue
 name=wanted[original];target=out/(name+'.map')
 with s.open_artifact(r,'map-r0.map','rb') as f:target.write_bytes(f.read())
 subprocess.run([str(Path('build/src/glob2').resolve()),'--preview-map',str(target),'--output',str(out/(name+'.png')),'--json',str(out/(name+'.json'))],check=True,stdout=subprocess.DEVNULL)
 index.append({'name':name,'original_job':original,'job':r['job'],'map_report':r['result']['map_report']})
assert len(index)==3,len(index)
(out/'index.json').write_text(json.dumps(index,indent=2)+'\n')
