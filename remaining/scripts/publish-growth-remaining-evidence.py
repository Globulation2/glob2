from pathlib import Path
import os,subprocess
root=Path.cwd();r=root/'artifacts/resource-growth';env=dict(os.environ,GIT_INDEX_FILE=str(r/'evidence.index'))
def git(args,**kw):return subprocess.check_output(['git',*args],env=env,**kw).decode().strip()
old=(r/'evidence-commit.txt').read_text().strip();assert git(['rev-parse','refs/heads/evidence/resource-growth-897'])==old
git(['read-tree','--empty']);entries=[]
files=sorted(p for p in (r/'published').rglob('*') if p.is_file())
for p in files:assert p.stat().st_size<90*1024*1024,(p,p.stat().st_size)
hashes=git(['hash-object','-w','--stdin-paths'],input=('\n'.join(str(p) for p in files)+'\n').encode()).splitlines()
assert len(hashes)==len(files)
for p,h in zip(files,hashes):entries.append(f'100644 {h}\t{p.relative_to(r/"published")}\n')
git(['update-index','--index-info'],input=''.join(entries).encode());tree=git(['write-tree']);commit=git(['commit-tree',tree,'-p',old,'-m','Record four growth optimization experiments and current-master comparison']);git(['update-ref','refs/heads/evidence/resource-growth-897',commit,old]);(r/'evidence-commit.txt').write_text(commit+'\n');print(commit,flush=True);subprocess.run(['git','push','origin','evidence/resource-growth-897'],check=True)
