from pathlib import Path
import sys,json,shutil,gzip,hashlib
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root))
from tools.tournaments.results import Results
out=root/'artifacts/forts-review';out.mkdir(exist_ok=True)
def copy(src,dst):
 dst=out/dst;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
variants={'baseline-r3':root/'artifacts/forts-tournament/linux-run','r5':root/'artifacts/forts-tuning/r5/experiment','r5-holdout':root/'artifacts/forts-tuning/r5-holdout/experiment','r5-numbi':root/'artifacts/forts-tuning/r5-numbi/experiment'}
for variant,source in variants.items():
 store=Results(source);copy(source/'experiment.json',f'{variant}/experiment.json')
 for a in store:
  if a['category']!='success':continue
  j=a['job'];seed=j['labels']['map_seed'];rot=j['labels']['rotation'];base=Path(variant)/f'seed-{seed}/rotation-{rot}'
  copy(source/'results'/f"{j['id']}.json",base/'accepted.json')
  for meta in a['artifacts']:
   if meta['path'] not in ['stdout.log','stderr.log','result.json','final.game']:continue
   if meta['path']=='final.game' and not(rot==0 and seed in [17,29,101,211]):continue
   blob=source/'artifacts'/meta['sha256'];assert hashlib.sha256(blob.read_bytes()).hexdigest()==meta['sha256']
   copy(blob,base/(meta['path']+('.gz' if meta['encoding']=='gzip' else '')))
  meta=j['inputs']['map'];copy(source/'artifacts'/meta['sha256'],base/'initial.map.gz')
for name in ['r4','r5','r5-holdout','r5-numbi']:
 for f in ['summary.json','games.json','colonies.csv']:
  src=root/f'artifacts/forts-tuning/{name}/{f}'
  if src.exists():copy(src,f'{name}/{f}')
for src in (root/'artifacts/forts-tuning/previews').iterdir():
 if src.suffix in ['.png','.json']:copy(src,Path('previews')/src.name)
for name in ['contracts-r5.log','r5-checks.log','golden-final.log','sweep-final.log','telemetry-final.log','linux-final-validation.log','linux-golden-final.log','golden-r6-update.log','linux-r6.log','r6-equivalence.json','contracts-r6.log','golden-r6-final.log','linux-r6-final.log']:
 src=root/'artifacts/forts-tuning'/name
 if src.exists():copy(src,Path('validation')/name)
for src in (root/'artifacts/forts-tuning/r5-checks').glob('*.json'):copy(src,Path('validation/parameter-matrix')/src.name)
for name in ['check_r5.py','plan.py','analyze.py','package_review.py']:
 copy(root/'artifacts/forts-tuning'/name,Path('scripts')/name)
for name in ['FortsGenerator-r3.cpp','FortsGenerator-r4.cpp']:
 copy(root/'artifacts/forts-tuning'/name,Path('source-snapshots')/name)
copy(root/'src/map/generator/generators/FortsGenerator.cpp','source-snapshots/FortsGenerator-r6.cpp')
copy(root/'test/map-generator-golden.txt','validation/map-generator-golden.txt')
copy(root/'artifacts/forts/forts-1.png','previews/prototype-r1-seed-1.png')
copy(root/'docs/map-generators/FORTS.md','DESIGN.md')
copy(root/'artifacts/forts-tournament/cross-platform-check.json','validation/cross-platform-check.json')
for platform in ['mac','linux']:
 src=root/f'artifacts/forts-tournament/{platform}-probe/export/game.replay.checksums'
 dest=out/f'validation/{platform}-4096.checksums.gz'
 with src.open('rb') as f,gzip.open(dest,'wb') as g:shutil.copyfileobj(f,g)
bundle=next((root/'artifacts/forts-tournament/linux-run/builds').iterdir())
copy(bundle/'bundle.json','runtime-bundle.json')
files=sorted(p for p in out.rglob('*') if p.is_file() and p.name!='SHA256SUMS')
(out/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(out)}\n' for p in files))
print(len(files),'files',sum(p.stat().st_size for p in files)//1048576,'MiB')
