"""Compare complete native map reports and saved map bytes for each paired job."""
import hashlib,json,sys
from pathlib import Path
from tools.tournaments.results import Results
s=Results(sys.argv[1]);pairs={};records={r['job']['id']:r for r in s}
# Artifact failures are not accepted logical samples. Retain them explicitly for
# equivalence diagnostics, checking the known rotation-only failure independently.
for r in s.attempts():
    if r['job']['id'] in records:continue
    if r['category']=='artifact_failure':records[r['job']['id']]=r
assert len(records)==len(s.manifest['jobs'])
rotation_failures=[]
for r in records.values():
    if r['category']=='artifact_failure':
        with s.open_artifact(r,'stdout.log') as f:lines=f.read().splitlines()
        flags=next(x for x in lines if x.startswith('ROTATIONS,')).split(',')
        assert int(flags[1]) in (11,12) and flags[2:] == ['1','0','1','1','0','1'],flags
        rotation_failures.append(r['job']['id'])
    else:assert r['category']=='success'
    pairs.setdefault(r['job']['labels']['pair'],{})[r['job']['labels']['variant']]=r
checks=[]
for pair, variants in sorted(pairs.items()):
    a,b=variants['baseline'],variants['shared']
    assert a['category']==b['category'],pair
    assert a['result']['map_report']==b['result']['map_report'],('report',pair)
    hashes=[]
    for r in (a,b):
        with s.open_artifact(r,'map-r0.map','rb') as f:hashes.append(hashlib.sha256(f.read()).hexdigest())
    assert hashes[0]==hashes[1],('saved map',pair)
    checks.append({'pair':pair,'original_job':a['job']['labels']['original_job'],'map_sha256':hashes[0]})
Path(sys.argv[2]).write_text(json.dumps({'pairs':len(checks),'accepted_jobs':len(list(s)),'rotation_artifact_failure_jobs':rotation_failures,'identical_complete_reports':True,'identical_saved_maps':True,'checks':checks},indent=2)+'\n')
print(len(checks),'pairs: complete reports and saved maps identical')
