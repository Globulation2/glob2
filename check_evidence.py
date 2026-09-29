#!/usr/bin/env python3
"""Check the retained input hashes, reference traces, and equivalence manifests."""
import gzip,hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parent
sha=lambda data:hashlib.sha256(data).hexdigest()
cases=json.loads((root/'cases.json').read_text());assert len(cases)==12
for c in cases:assert sha((root/c['save']).read_bytes())==c['input_sha256'],c['id']
reference={}
for host in ['linux','macos','therig']:
 rows=json.loads((root/'validation'/host/'results.json').read_text())
 assert len(rows)==(12 if host=='macos' else 24)
 for row in rows:
  name=row['case'];signature=tuple(row[k] for k in ['trace_sha256','order_sha256','trace_ticks'])
  if name in reference:assert signature==reference[name],(host,name)
  reference[name]=signature
  if host=='linux' and row['variant']=='baseline':
   p=root/'validation'/host/row['directory']
   assert sha(gzip.decompress((p/'game.replay.checksums.gz').read_bytes()))==row['trace_sha256']
   assert sha((p/'orders.bin').read_bytes())==row['order_sha256']
rows=json.loads((root/'continuation/results.json').read_text());assert len(rows)==8
for name in {r['case'] for r in rows}:
 values=[r for r in rows if r['case']==name];assert len(values)==2
 assert len({r['trace_sha256'] for r in values})==1
 expected=12171 if name=='continents12' else None
 assert all(r['first_uninterrupted_mismatch']==expected for r in values)
 for r in values:
  data=gzip.decompress((root/'continuation'/(name+'-'+r['variant'])/'game.replay.checksums.gz').read_bytes());assert sha(data)==r['trace_sha256']
print('PASS: 12 input states; 60 equivalent verification runs; 8 save/reload checks')
