from pathlib import Path
import json
b=Path('artifacts/resource-growth/remaining');rs=json.load(open(b/'final-retained-work/results.json'));results=[]
for r in rs:
 s=r['scenario'];folder='final-master-work' if r['variant']=='final-master-work' else 'final-retained-work';old='current-master-work' if folder=='final-master-work' else 'retained-work';current=(b/folder/s/'game.replay.checksums').read_bytes();prior=(b/old/s/'game.replay.checksums').read_bytes();results.append({'scenario':s,'variant':folder,'matches_6487_integration':current==prior});assert current==prior,(s,folder)
(b/'final-work-trace-comparison.json').write_text(json.dumps(results,indent=2));print('All16 final master/retained traces match earlier integration at1024ticks.')
