from pathlib import Path
import json,shutil,tarfile,hashlib,subprocess
p=Path(__file__).resolve().parent;dest=p/'publish';dest.mkdir(exist_ok=True)
for pattern in ['*.py','*.json','*-driver.log','build*.log','*.h','before.cpp','after.cpp','*-profile.cpp','verify.cpp','*Harness-*.log']:
 for f in p.glob(pattern):shutil.copy2(f,dest/f.name)
for f in p.glob('*.jsonl'):
 rows=[json.loads(s) for s in f.read_text().splitlines()]
 for r in rows:r.pop('process_snapshot',None) # Full local process inventory stays local.
 (dest/f.name).write_text(''.join(json.dumps(r)+'\n' for r in rows))
shutil.copy2(p/'report.md',dest/'README.md')
(dest/'change.patch').write_bytes(subprocess.check_output(['git','diff','7d2dcaf1','--','src/map/gradient/MapGradientBuilding.cpp','docs/development/performance-telemetry.md']))
for case in (p/'validation').iterdir():
 if not case.is_dir():continue
 with tarfile.open(dest/(case.name+'-validation.tar.gz'),'w:gz') as archive:
  for f in case.rglob('*'):
   if f.is_file() and (f.suffix=='.log' or f.name in ['game.replay.checksums','result.json'] or (f.parent.name=='lazy' and f.suffix=='.game')):archive.add(f,arcname=str(f.relative_to(p)))
with tarfile.open(dest/'logs-continuation.tar.gz','w:gz') as archive:
 for f in (p/'performance').glob('*.log'):archive.add(f,arcname=str(f.relative_to(p)))
 for f in (p/'continuation').iterdir():
  if f.is_file():archive.add(f,arcname=str(f.relative_to(p)))
 archive.add(p/'continuation.log',arcname='continuation.log')
manifest={str(f.relative_to(dest)):{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in dest.rglob('*') if f.is_file() and f.name!='manifest.json'}
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(len(manifest),'files; largest',max(v['bytes'] for v in manifest.values()))
