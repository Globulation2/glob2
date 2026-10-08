from pathlib import Path
import gzip,shutil,json,os,subprocess
root=Path.cwd();r=root/'artifacts/resource-growth';src=r/'attribution-v2';dest=r/'published/attribution-v2';dest.mkdir(exist_ok=True)
for p in src.iterdir():
 if p.is_file() and p.suffix in {'.json','.md','.txt','.log','.cpp','.h','.patch'}:
  if p.suffix=='.log':
   with gzip.open(dest/(p.name+'.gz'),'wb') as f:f.write(p.read_bytes())
  else:shutil.copy2(p,dest/p.name)
for directory in ['statistics','batched','stage-runs','fair','owner-control','direct-control','eager-determinism','direct-determinism']:
 for p in (src/directory).rglob('*'):
  if not p.is_file() or 'profile' in p.relative_to(src/directory).parts:continue
  if p.name not in {'metadata.json','measurements.jsonl','summary.json','result.json','stage-cpu.json','engine.log','world.checksums','game.replay.checksums'}:continue
  to=dest/p.relative_to(src);to.parent.mkdir(parents=True,exist_ok=True)
  if p.suffix in {'.log','.jsonl'}:
   with gzip.open(str(to)+'.gz','wb') as f:f.write(p.read_bytes())
  else:shutil.copy2(p,to)
scripts=dest/'scripts';scripts.mkdir(exist_ok=True)
for p in (root/'docs/.work').glob('*'):
 if p.is_file() and p.suffix in {'.py','.sh'} and (('attribution-v2' in p.name) or p.name in ['build-growth-attribution-controls.py','build-growth-final-controls.py','build-growth-statistics-candidate.py','build-growth-copy-candidate.py','build-growth-legacy-copy.py','build-growth-eager-owner.py','build-growth-direct-owner.py','run-growth-eager-owner.py','run-growth-direct-owner.py','run_with_shared_host_cpuset.py','check-shared-host-cpuset.py','run-growth-attribution-controls.sh']):shutil.copy2(p,scripts/p.name)
env=dict(os.environ,GIT_INDEX_FILE=str(r/'evidence.index'))
def git(args,**kw):return subprocess.check_output(['git',*args],env=env,**kw).decode().strip()
old=(r/'evidence-commit.txt').read_text().strip();git(['read-tree','--empty']);entries=[]
for p in sorted((r/'published').rglob('*')):
 if p.is_file():entries.append(f'100644 {git(["hash-object","-w",str(p)])}\t{p.relative_to(r/"published")}\n')
git(['update-index','--index-info'],input=''.join(entries).encode());tree=git(['write-tree']);commit=git(['commit-tree',tree,'-p',old,'-m','Attribute remaining growth costs and test snapshot copy and serial placement controls']);git(['update-ref','refs/heads/evidence/resource-growth-897',commit,old]);(r/'evidence-commit.txt').write_text(commit+'\n');print(commit,flush=True);subprocess.run(['git','push','origin','evidence/resource-growth-897'],check=True)
