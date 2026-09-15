from pathlib import Path
import sys,json,subprocess,hashlib
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root))
from tools.tournaments.common import store_artifact
from tools.tournaments.model import job
from tools.tournaments.coordinator import Coordinator
base=Path(__file__).resolve().parent;name=sys.argv[1];seeds=[int(x) for x in sys.argv[2].split(',')];base=base/name;base.mkdir(exist_ok=True)
bundle=next((root/'artifacts/forts-tournament/linux-run/builds').iterdir());build=json.loads((bundle/'bundle.json').read_text())['id'];jobs=[]
for seed in seeds:
 directory=base/f'maps/g33-s{seed}';directory.mkdir(parents=True,exist_ok=True);out=directory/'native'
 cmd=[str(root/'build/src/glob2'),'--generate-map','--generator','34','--map-seed',str(seed),'--param','width=8','--param','height=8','--param','teams=4','--write-map','true','--rotations','4','--output-dir',str(out)]
 if not (out/'result.json').exists():
  with (directory/'generate.log').open('w') as f:subprocess.run(cmd,check=True,cwd=root,stdout=f,stderr=f)
 m=json.loads((out/'result.json').read_text());assert m['status']=='completed';assert m['rotations_verified']
 for r in range(4):
  input=store_artifact(out/f'map-r{r}.map',base/'experiment/artifacts')
  jobs.append(job('game',build,inputs={'map':input},seeds={'game':101+r},config={'players':['nicowar']*4,'ticks':60000},outputs={'telemetry':['team-timeline'],'saves':['final'] if r==0 and seed in seeds[:2] else []},limits={'timeout_seconds':1800},labels={'map_seed':seed,'rotation':r,'variant':name,'generator_revision':m['revision']}))
manifest={'schema_version':1,'id':'forts-tuning-'+name,'jobs':jobs,'settings':{'heartbeat_seconds':3,'lease_seconds':300,'prefetch':0},'labels':{'generator_source_sha256':hashlib.sha256((root/'src/map/generator/generators/FortsGenerator.cpp').read_bytes()).hexdigest(),'simulation_bundle':'same as revision 3 Linux baseline'}}
(base/'plan.json').write_text(json.dumps(manifest,indent=2)+'\n');c=Coordinator.submit(base/'experiment',manifest,[bundle]);c.close();print(base)
