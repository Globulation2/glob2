"""Retain complete accepted reports without duplicating the engine's JSON object.

The full content-addressed object store stays in the original results directory.
The review ZIP contains the immutable experiment, each accepted record (including
its full native report), the pinned runner, and the exact build manifest. Before
packing, verify every referenced object and compare native result.json to the
embedded report. This makes omission of the duplicate object store explicit.
"""
import json
from pathlib import Path
import sys
import zipfile
from tools.tournaments.common import file_hash
from tools.tournaments.results import Results
root=Path(sys.argv[1]);target=Path(sys.argv[2]);s=Results(root)
rows=list(s)
assert len(rows)==len(s.manifest['jobs']), 'Refuse to package an incomplete study'
expected={j['id']:j for j in s.manifest['jobs']}
accepted_ids={r['id'] for r in rows}
extras=[r for r in s.attempts() if r['id'] not in accepted_ids]
objects={}
for r in rows+extras:
    assert r['job']==expected[r['job']['id']]
    for obj in r['artifacts']:
        objects[obj['sha256']]=obj
    if any(x['path']=='result.json' for x in r['artifacts']):
        with s.open_artifact(r,'result.json') as f:
            assert json.load(f)==r['result'],r['job']['id']
for identity,obj in objects.items():
    path=root/'artifacts'/identity
    assert path.stat().st_size==obj['bytes'] and file_hash(path)==identity,identity
retention={'accepted_records':len(rows),'additional_attempt_records':len(extras),'verified_objects':len(objects),
    'verified_object_bytes':sum(o['bytes'] for o in objects.values()),
    'archive_contains':'Complete accepted records and native reports, immutable experiment, pinned runner, build manifests.',
    'object_store':'Original results/artifacts directory retained separately; duplicate objects omitted from this review ZIP.'}
with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for name in ('experiment.json','worker.pyz'):
        z.write(root/name,name)
    for r in rows:
        name='results/'+r['job']['id']+'.json';z.write(root/name,name)
    for r in extras:
        name='attempts/'+r['id']+'.json';z.write(root/name,name)
    for path in (root/'failures').glob('*-lease.json'):
        z.write(path,'failures/'+path.name)
    if (root/'coordinator-status.json').exists():z.write(root/'coordinator-status.json','coordinator-status.json')
    for build in {j['build'] for j in s.manifest['jobs']}:
        name='builds/'+build+'/bundle.json';z.write(root/name,name)
    z.writestr('retention.json',json.dumps(retention,indent=2)+'\n')
with zipfile.ZipFile(target) as z:assert z.testzip() is None
print(json.dumps(retention|{'zip_bytes':target.stat().st_size},indent=2))
