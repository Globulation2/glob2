"""Retain accepted and failed-attempt evidence without promoting failures to success."""
import json,sys,zipfile
from pathlib import Path
from tools.tournaments.results import Results
from tools.tournaments.common import file_hash
root=Path(sys.argv[1]);s=Results(root);attempts=list(s.attempts());objects={}
assert {r['job']['id'] for r in attempts}=={j['id'] for j in s.manifest['jobs']}
for r in attempts:
 for a in r['artifacts']:objects[a['sha256']]=a
for h,a in objects.items():
 p=root/'artifacts'/h;assert p.stat().st_size==a['bytes'] and file_hash(p)==h
retention={'planned':len(s.manifest['jobs']),'accepted':len(list(s)),'attempts':len(attempts),'verified_objects':len(objects),'object_store':'Retained in original framework-results/artifacts; omitted duplicate objects from review ZIP.'}
with zipfile.ZipFile(sys.argv[2],'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for path in [root/'experiment.json',root/'worker.pyz',*sorted((root/'results').glob('*.json')),*sorted((root/'attempts').glob('*.json')),*sorted((root/'builds').glob('*/bundle.json'))]:z.write(path,str(path.relative_to(root)))
 z.writestr('retention.json',json.dumps(retention,indent=2))
print(json.dumps(retention,indent=2))
