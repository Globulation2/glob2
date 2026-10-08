from pathlib import Path
import gzip,shutil,json,hashlib
root=Path.cwd();r=root/'artifacts/resource-growth';src=r/'remaining';dest=r/'published/remaining';dest.mkdir(parents=True,exist_ok=True)
def copy(p,relative=None):
 relative=relative or p.relative_to(src);to=dest/relative;to.parent.mkdir(parents=True,exist_ok=True)
 if p.suffix in {'.log','.jsonl','.checksums','.trace'} and p.stat().st_size>8192:
  with gzip.open(str(to)+'.gz','wb') as f:f.write(p.read_bytes())
 else:shutil.copy2(p,to)
# Explicit allowlist avoids archives, build trees, executable/object files, local profiles and preferences.
for p in src.iterdir():
 if p.is_file() and p.suffix in {'.json','.jsonl','.md','.txt','.log','.xml','.patch','.cpp','.h'}:copy(p)
folders=['player-free-tests','player-free-extended','player-free-final','player-free-golden','final-master-compat','final-retained-work','final-master-work','final-integration-verification','final-delivery-verification','final-integration-broad-first','final-integration-broad','final-integration-unit','final-integration-golden','final-delivery-golden','final-golden-update','final-header','final-integration-comparison','reproduction','components','timing','extended','stages','confirmation','retained-comparison','verification','production-verification','integration-baseline-verification','integration-production-verification','extra-verification','integration-direct-verification','master-work','latest-master-work','production-broad','production-golden','integration-broad','integration-golden','integration-executor','optimized-original-verified','retained-broad','retained-golden','retained-executor','retained-unit','retained-verification','current-master-compat','current-master-work','retained-work','layout-check','layout-cli','golden-update','retained-golden-final','pilot-components','pilot-timing','pilot-verification']
allowed={'.json','.jsonl','.md','.txt','.log','.xml','.checksums','.trace','.game','.g2mr','.gz','.patch','.cpp','.h'}
for folder in folders:
 base=src/folder
 if not base.exists():continue
 for p in base.rglob('*'):
  if p.is_file() and 'profile' not in p.relative_to(base).parts and p.suffix in allowed:copy(p)
# Input saves referenced by the manifest, with their original directory structure under resource-growth.
for s in json.load(open(src/'manifest.json'))['scenarios']:
 for name,h in s['fixture_sha256'].items():
  p=Path(name);assert hashlib.sha256(p.read_bytes()).hexdigest()==h
  copy(p,Path('inputs')/p.relative_to(r))
# Preserve reconstruction sources, probes and fixture harness without object files.
for name in ['ResourceGrowthFixtures.cpp','StageProbe.h']:
 copy(src/name,Path('reproduction')/name)
scripts=dest/'scripts';scripts.mkdir(exist_ok=True)
for p in (root/'docs/.work').iterdir():
 if p.is_file() and p.suffix in {'.py','.sh','.cpp'} and (any(key in p.name for key in ['growth-remaining','growth-final','growth-production','growth-integration','growth-optimized','growth-latest-master','growth-extra','growth-common-copy','growth-prototype','growth-retained','growth-current','growth-layout']) or p.name in ['run_with_shared_host_cpuset.py','check-shared-host-cpuset.py','remaining-component.cpp']):shutil.copy2(p,scripts/p.name)
for name in ['benchmark_parallel_compute.py','benchmark_resource_refactor.py','benchmark_resource_growth.py','run_with_benchmark_governor.py']:
 shutil.copy2(root/'test'/name,scripts/name)
# Record all staged evidence bytes; this is not a source/binary fingerprint replacement.
manifest={str(p.relative_to(dest)):hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.rglob('*') if p.is_file() and p.name!='evidence-sha256.json'}
(dest/'evidence-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Staged',len(manifest),'files,',sum(p.stat().st_size for p in dest.rglob('*') if p.is_file()),'bytes')
