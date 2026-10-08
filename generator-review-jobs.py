from pathlib import Path
import sys,json,subprocess,hashlib
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
from tools.tournaments.model import job
from tools.tournaments.jobs import EngineJob
base=root/'artifacts/generator-review-headless-final';base.mkdir(exist_ok=True)
package=base/'swamp-generator.json'
if not package.exists():
    package=base/'swamp-generator.json'
    subprocess.run([sys.executable,str(root/'tools/map-generators/package.py'),str(root/'data/generators/examples/swamp'),str(package)],check=True)
inputs={'generator-package':{'sha256':hashlib.sha256(package.read_bytes()).hexdigest()}}
bundle={'directory':str(root),'executable':'build/linux/client/release/src/glob2'}
for kind in ['generate_map','game']:
    adapter=EngineJob(kind)
    config={'generator':'examples:swamp','params':{'width':8,'height':8,'teams':2},'candidates':0}
    seeds={'map':91};outputs={'map':True} if kind=='generate_map' else {'saves':['initial','final'],'replay':True}
    if kind=='game': config.update(players=['cortex','maxima'],ticks=512);seeds['game']=713
    request=job(kind,'a'*64,inputs=inputs,config=config,seeds=seeds,outputs=outputs)
    adapter.validate(request);attempt=base/kind;attempt.mkdir(exist_ok=True)
    args=adapter.command(request,bundle,attempt,{'generator-package':str(package)})
    (attempt/'request.json').write_text(json.dumps(request,indent=2))
    (attempt/'command.json').write_text(json.dumps(args))
    with (attempt/'command.log').open('w') as log: subprocess.run(args,check=True,cwd=root,stdout=log,stderr=subprocess.STDOUT)
    result=adapter.collect(request,attempt);assert result['status']=='completed',result
    print(kind,'completed',flush=True)
args=[str(root/bundle['executable']),'--run-game','--load-game',str(base/'game/output/final.game.gz'),'--ticks','1024','--output-dir',str(base/'continuation'),'--profile','generator-review-continuation']
with (base/'continuation.log').open('w') as log: subprocess.run(args,check=True,cwd=root,stdout=log,stderr=subprocess.STDOUT)
assert json.loads((base/'continuation/result.json').read_text())['status']=='completed'
print('saved game resumed without package',flush=True)

args=[str(root/bundle['executable']),'--run-game','--load-game',str(base/'game/output/initial.game.gz'),'--ticks','1024','--output-dir',str(base/'continuous'),'--profile','generator-review-continuous']
with (base/'continuous.log').open('w') as log: subprocess.run(args,check=True,cwd=root,stdout=log,stderr=subprocess.STDOUT)
continued=json.loads((base/'continuation/result.json').read_text()); continuous=json.loads((base/'continuous/result.json').read_text())
assert continuous['status']=='completed'
assert continued['finalChecksum']==continuous['finalChecksum'], (continued,continuous)
report={'continuationChecksum':continued['finalChecksum'],'continuousChecksum':continuous['finalChecksum'],'throughTick':1024,'packageRequiredOnLoad':False}
(base/'checksum-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print('continuation and continuous checksums match',report,flush=True)
