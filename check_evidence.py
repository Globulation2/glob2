#!/usr/bin/env python3
import pathlib,json,gzip,hashlib
r=pathlib.Path(__file__).resolve().parent;sha=lambda x:hashlib.sha256(x).hexdigest()
cases=json.loads((r/'cases.json').read_text());assert len(cases)==14
for c in cases:assert sha((r/c['save']).read_bytes())==c['input_sha256']
reference={}
for host in ['linux','macos','therig']:
 rows=json.loads((r/'validation'/host/'results.json').read_text());assert len(rows)==28
 for row in rows:
  signature=tuple(row[k] for k in ['trace_sha256','order_sha256','trace_ticks']);name=row['case'];assert row['trace_ticks']==512
  if name in reference:assert reference[name]==signature,(host,name)
  reference[name]=signature
  if host=='linux' and row['variant']=='baseline':
   d=r/'validation'/host/row['directory'];assert sha(gzip.decompress((d/'game.replay.checksums.gz').read_bytes()))==row['trace_sha256'];assert sha((d/'orders.bin').read_bytes())==row['order_sha256']
rows=json.loads((r/'continuation/results.json').read_text());assert len(rows)==12
for name in {x['case'] for x in rows}:
 group=[x for x in rows if x['case']==name];assert len(group)==2;assert group[0]['trace_sha256']==group[1]['trace_sha256']
 expected=12171 if name=='continents12' else None
 assert all(row['first_uninterrupted_mismatch']==expected for row in group)
 for row in group:
  assert sha(gzip.decompress((r/'continuation'/(name+'-'+row['variant'])/'game.replay.checksums.gz').read_bytes()))==row['trace_sha256']
print('PASS: 14 input states, 84 equivalent 512-tick runs, 12 paired reload runs')
