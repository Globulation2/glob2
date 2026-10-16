from pathlib import Path
import json,hashlib,math
O=Path(__file__).resolve().parent
for x in json.loads((O/'manifest.json').read_text())['files']:
 p=O/x['path'];assert hashlib.sha256(p.read_bytes()).hexdigest()==x['sha256']and p.stat().st_size==x['bytes']
d=json.loads((O/'native/result.json').read_text());s=json.loads((O/'summary.json').read_text());t=d['compute_telemetry'];assert d['ticks']==20000 and d['compute_threads']==32 and t['mode']=='waits'and t['available']
for row in s['producer_reason_rows']:
 x=t['producers'][row['producer']]['joins'][row['reason']];assert row['wait_ns']==x['background_wait']['total_ns']and row['joined']==x['joined']and row['incomplete']==x['incomplete']
assert math.isclose(sum(r['wait_ns']for r in s['producer_reason_rows'])/1e9,s['total_required_background_wait_seconds'],abs_tol=1e-12)
print('Verified diagnostic file hashes and required-wait aggregation')
