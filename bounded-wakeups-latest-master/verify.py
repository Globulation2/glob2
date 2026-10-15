from pathlib import Path
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
