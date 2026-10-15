from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parent.parent
x=json.loads((p/'index/content-manifest.json').read_text())
for f in x['files']:
 h=hashlib.sha256()
 with (p/f['portable']).open('rb') as s:
  for chunk in iter(lambda:s.read(1048576),b''):h.update(chunk)
 assert h.hexdigest()==f['sha256'],f['portable']
print('Selected content verified; see tools/README.json for separately qualified final-cohort recomputation')
