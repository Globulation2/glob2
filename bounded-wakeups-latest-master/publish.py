from pathlib import Path
import json,hashlib,shutil,subprocess
O=Path(__file__).resolve().parent;R=Path('/home/bradley/.codex/worktrees/engine-optimization/glob2');repo=O.parent/'october-regression/coordination-redesign/full-design-comparison/playground-stop/publish-checkout';D=repo/'bounded-wakeups-latest-master'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
v=json.loads((O/'validation-complete.json').read_text());assert v['state']=='passed'
base=v['builds']['results'][0]['revision'];candidate=v['builds']['results'][1]['revision']
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()==candidate
assert not subprocess.check_output(['git','status','--porcelain'],cwd=R,text=True).strip()
assert not D.exists();D.mkdir()
files=[];omitted=[]
def copy(src,rel):
 target=D/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target);files.append({'path':str(rel),'sha256':sha(target),'bytes':target.stat().st_size,'original':str(src)})
for name in ['build.py','validate.py','publish.py','environment.json','builds-complete.json','validation-complete.json','cases.json','publication-plan.json','runtime-dependencies.json']:
 copy(O/name,Path(name))
for role in ['baseline','candidate']:
 for p in sorted((O/role).rglob('*')):
  if not p.is_file() or p.is_symlink():continue
  rel=p.relative_to(O)
  if 'profile' in rel.parts or p.name.startswith('glob2') or p.suffix in ['.o','.so','.replay'] or (p.name.endswith('.game.gz') and 'native' in rel.parts):
   omitted.append({'path':str(rel),'sha256':sha(p),'bytes':p.stat().st_size});continue
  if (p.suffix=='.game' and p.name!='phase-0.game') or p.name.endswith('.game.txt') or (p.suffix=='.checksums' and p.open('rb').read(4)==b'GCS1'):
   omitted.append({'path':str(rel),'sha256':sha(p),'bytes':p.stat().st_size});continue
  if p.suffix in ['.json','.xml','.log','.txt','.trace','.checksums'] or p.name.endswith('.game.gz') or (p.suffix=='.game' and 'focused-engine' in rel.parts):copy(p,rel)
for name in ['src/common/ComputeExecutor.h','src/common/ComputeExecutorHarness.cpp']:
 copy(R/name,Path('source/candidate')/name)
 target=D/'source/baseline'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(subprocess.check_output(['git','show',base+':'+name],cwd=R));files.append({'path':str(target.relative_to(D)),'sha256':sha(target),'bytes':target.stat().st_size})
patch=D/'source/change.patch';patch.write_bytes(subprocess.check_output(['git','diff',base,candidate,'--','src/common/ComputeExecutor.h','src/common/ComputeExecutorHarness.cpp'],cwd=R));files.append({'path':str(patch.relative_to(D)),'sha256':sha(patch),'bytes':patch.stat().st_size})
for p in [R/'test/fixtures/multiplayer/FourSquares1.g2mr',R/'test/fixtures/multiplayer/FourSquares1.verify-trace.txt',R/'maps/FourSquares1.map.gz']:
 copy(p,Path('fixtures')/p.name)
fixtures={}
for c in json.loads((O/'cases.json').read_text()):fixtures.update(c['fixture_sha256'])
for p,digest in fixtures.items():
 assert sha(p)==digest;copy(Path(p),Path('fixtures')/(digest[:12]+'-'+Path(p).name))
for name in ['parallel-parity.py','process_cleanup.py']:
 copy(O.parent/'october-regression/coordination-redesign'/name,Path('provenance-helpers')/name)
(D/'README.md').write_text('''# Bounded wakeups: fresh latest-master validation

The candidate is one commit on master dd493d7e71d21e5ab6cbaee0fe168a37e076860a. The only production change bounds deferred submission notifications by the already computed runnable count; the existing held-callback test now checks partial wakeups with four workers.

See validation-complete.json for exact source/binary hashes, commands, environment, focused-case inventory, and 20 fresh baseline/candidate per-tick comparisons. Native plaintext traces are retained in full. GCS entity-level sidecars were framing-validated and compared byte-for-byte locally; their hashes and framing metadata are retained, but the large bodies are omitted. Fixed maps and legacy mature-save fixtures are included. Generated native executables, replay bodies and profiles are omitted; build logs retain exact release compiler/link commands. Historical performance data remains separately published at evidence commit 5903347157253813a6a328fd4a362d997f77c232, under playground-20261015; these correctness runs are not performance measurements.

Run `python3 verify.py` to verify the bytes and recompute all native per-tick pair comparisons from this bundle. Original commands and scripts preserve their original absolute paths and are provenance, not a portable unattended build controller. Source paths are relative to the repository root. Fresh platform coverage is Linux x86-64 CPU plus one NVIDIA OpenCL device; browser, Windows, macOS, Android and iOS runtime and manual interactive feel were not revalidated here. Prior browser evidence belongs to its recorded older revision.
''')
(D/'verify.py').write_text('''from pathlib import Path
import json,hashlib
R=Path(__file__).resolve().parent
m=json.loads((R/'manifest.json').read_text())
for f in m['files']:
 p=R/f['path']
 with p.open('rb') as s:digest=hashlib.file_digest(s,'sha256').hexdigest()
 assert digest==f['sha256'] and p.stat().st_size==f['bytes'],f['path']
v=json.loads((R/'validation-complete.json').read_text())
for pair in v['parity']:
 traces=[]
 for item in pair['results']:
  p=R/item['role']/'parity'/pair['case']/'native'/Path(item['details']['native']['path']).name
  rows=[tuple(map(int,l.split())) for l in p.read_text().splitlines()]
  d=item['details']['native'];assert len(rows)==d['rows']==pair['rows']
  assert [row[0] for row in rows]==list(range(d['first_tick'],d['last_tick']+1))
  assert all(len(row)==2 and 0<=row[1]<=0xffffffff for row in rows)
  assert hashlib.sha256(p.read_bytes()).hexdigest()==item['checksums']['native']
  traces.append(p.read_bytes())
 assert traces[0]==traces[1],pair['case']
for role in ['baseline','candidate']:
 assert (R/role/'golden/checksums.txt').read_bytes()==(R/'fixtures/FourSquares1.verify-trace.txt').read_bytes()
print('Verified',len(m['files']),'files and',len(v['parity']),'exact per-tick pairs plus both golden traces')
''')
for name in ['README.md','verify.py']:
 p=D/name;files.append({'path':name,'sha256':sha(p),'bytes':p.stat().st_size})
(D/'manifest.json').write_text(json.dumps({'schema':1,'files':files,'omitted':omitted,'note':'Manifest excludes itself; all other selected files have byte hashes.'},indent=2)+'\n')
subprocess.run(['python3',str(D/'verify.py')],check=True)
print('Selected',len(files),'files',sum(f['bytes'] for f in files),'bytes')
