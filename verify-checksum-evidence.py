from pathlib import Path
import csv,hashlib,json
root=Path(__file__).resolve().parent
results={}
for codec in ('h264','vp9','av1'):
 base=root/('official-'+codec)/'engine'
 tables=list(base.rglob('per-tick-checksums.csv'))
 traces=list(base.rglob('threaded-baseline.replay.checksums'))
 assert tables and traces, (codec,tables,traces)
 comparisons=[]
 for p in tables:
  rows=list(csv.DictReader(p.open()))
  assert len(rows)==60 and all(r['baseline']==r['recording'] for r in rows),p
  comparisons.append({'file':str(p.relative_to(base)),'ticks':len(rows),'allMatch':True})
 for p in traces:
  recorded=p.with_name('threaded-recording.replay.checksums')
  assert p.read_bytes() and p.read_bytes()==recorded.read_bytes(),p
  comparisons.append({'baseline':str(p.relative_to(base)),'recording':str(recorded.relative_to(base)),'identicalBytes':True,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
 results[codec]=comparisons
(root/'publish/checksum-verification.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
