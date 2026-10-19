"""Package exact benchmark inputs/results as deduplicated SHA256 objects.

Artifact-only evidence helper. No game execution, source changes or model fitting.
ELFs, dependency trees, compiler outputs and source/build snapshots are excluded.
"""
from pathlib import Path
import hashlib,io,json,tarfile,gzip
ROOT=Path(__file__).resolve().parent
CAMPAIGN=ROOT.parent.parent
F=CAMPAIGN/'final1280'
STUDIES={'selected12-cpu':F/'candidate12-research/cpu','selected12-quality':F/'candidate12-research','rejected7-panel':F/'prospective-macos'}
objects={};entries=[]
def add(path,logical,kind):
 data=path.read_bytes();digest=hashlib.sha256(data).hexdigest();objects.setdefault(digest,data);entries.append(dict(path=logical,sha256=digest,bytes=len(data),kind=kind,original_path=str(path)))
selected=('COMMAND.json','MEASUREMENT.json','result.json','artifacts.json','progress.jsonl','stdout.log','stderr.log','gradient-quality.csv','gradient-planning-quality.csv','gradient-episodes.csv','gradient-work.csv','gradient-contradictions.csv')
for name,base in STUDIES.items():
 state=json.loads((base/'STATUS.json').read_text())
 assert state['status']=='COMPLETE'
 for row in state['rows']:
  if name=='selected12-quality' and row['phase']!='quality':continue
  folder=Path(row['output'])
  for file in selected:
   p=folder/file
   if p.exists():add(p,f'{name}/runs/{folder.name}/{file}','raw-run')
  for p in (folder/'profile').glob('*.txt'):add(p,f'{name}/runs/{folder.name}/profile/{p.name}','profile')
 for file in ['PLAN.json','PLAN.sha256','STATUS.json','START.json','ASSETS-BEFORE.json','DEPENDENCIES.json','run.py','run-quality.py','mac-observation-helpers.py','SUMMARY.json','REGRESSION-TRIAGE.json']:
  p=base/file
  if p.exists():add(p,f'{name}/{file}','study-provenance')
plan=json.loads((F/'prospective-macos/PLAN.json').read_text())
for m in plan['maps']:
 p=Path(m['warm_path']);assert hashlib.sha256(p.read_bytes()).hexdigest()==m['warm_sha256'];add(p,f'inputs/{m["group"]}/warm175.game.gz','exact-shared-input')
 p=Path(m['map_path']);assert hashlib.sha256(p.read_bytes()).hexdigest()==m['map_sha256'];add(p,f'inputs/{m["group"]}/initial175.map.gz','initial-map-provenance')
index=dict(schema=1,algorithm='sha256',objects='benchmark-cas.tar.gz/objects/<sha256>',entries=entries,logical_files=len(entries),unique_objects=len(objects),logical_bytes=sum(e['bytes'] for e in entries),unique_bytes=sum(map(len,objects.values())),scope='24 selected12 OFF +2 selected12 ON +24 rejected7-panel OFF +4 historical ON (including2 acceptedgen005 ON reused as selected12qualityreference). No ELF/dependency/buildobjects.',reconstruction='Read each objects/<sha256> byte payload, verify SHA256 and byte length, and write to indexed relative path; original_path records historical command locations, not prerequisites for verification.')
(ROOT/'BENCHMARK-CAS.json').write_text(json.dumps(index,sort_keys=True,indent=2)+'\n')
with (ROOT/'benchmark-cas.tar.gz').open('wb') as raw:
 with gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as gz:
  with tarfile.open(fileobj=gz,mode='w') as tar:
   for digest,data in sorted(objects.items()):
    item=tarfile.TarInfo('objects/'+digest);item.size=len(data);item.mode=0o644;item.mtime=0;tar.addfile(item,io.BytesIO(data))
with tarfile.open(ROOT/'benchmark-cas.tar.gz') as tar:
 actual={m.name.removeprefix('objects/'):tar.extractfile(m).read() for m in tar.getmembers()}
 assert set(actual)==set(objects)
 assert all(hashlib.sha256(data).hexdigest()==digest for digest,data in actual.items())
receipt=dict(status='PASS',archive_sha256=hashlib.sha256((ROOT/'benchmark-cas.tar.gz').read_bytes()).hexdigest(),archive_bytes=(ROOT/'benchmark-cas.tar.gz').stat().st_size,index_sha256=hashlib.sha256((ROOT/'BENCHMARK-CAS.json').read_bytes()).hexdigest(),logical_files=len(entries),unique_objects=len(objects),decoded_objects_verified=True)
(ROOT/'BENCHMARK-CAS-VERIFIED.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
