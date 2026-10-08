from pathlib import Path
import shutil,gzip,json,hashlib
r=Path.cwd();b=r/'artifacts/resource-growth/final-master';out=r/'artifacts/resource-growth/published/final-master';out.mkdir(exist_ok=True)
def put(p,rel=None):
 q=out/(rel or p.relative_to(b));q.parent.mkdir(parents=True,exist_ok=True)
 if p.suffix=='.log' and p.stat().st_size>250000:
  q=Path(str(q)+'.gz');q.write_bytes(gzip.compress(p.read_bytes(),mtime=0))
 else:shutil.copy2(p,q)
for p in b.iterdir():
 if p.is_file() and p.suffix in {'.json','.jsonl','.xml','.md','.patch','.log','.txt','.cpp'}:put(p)
for sub in ['paired','charts','correctness','binaries']:
 for p in (b/sub).rglob('*'):
  if p.is_file() and p.suffix in {'.json','.jsonl','.log','.gz','.checksums','.svg','.png','.pdf'}:put(p)
for n in ['prepare-rebased-growth-comparison.py','run-rebased-growth-correctness.py','verify-rebased-growth.py','build-rebased-growth-inspector.py','inspect-rebased-growth-saves.py','freeze-rebased-growth.py','run-rebased-growth-pairs.py','run-rebased-growth-reserved.sh','run-rebased-growth-confirmation.sh','summarize-rebased-growth-pairs.py','run_with_shared_host_cpuset.py','plot-rebased-growth.py','report-rebased-growth.py','package-rebased-growth-evidence.py']:
 p=r/'docs/.work'/n
 if p.exists():put(p,Path('scripts')/n)
for p in [r/'test/benchmark_parallel_compute.py',r/'test/benchmark_resource_refactor.py',r/'test/run_with_benchmark_governor.py']:put(p,Path('scripts')/p.name)
for s in json.loads((b/'manifest.json').read_text())['scenarios']:
 for path in s['fixture_sha256']:
  p=Path(path)
  if not p.exists():p=Path(path+'.gz')
  put(p,Path('fixtures')/s['id']/p.name)
manifest={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob('*')) if p.is_file() and p.name!='SHA256.json'}
(out/'SHA256.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(len(manifest),'files',sum(p.stat().st_size for p in out.rglob('*') if p.is_file()),'bytes')
