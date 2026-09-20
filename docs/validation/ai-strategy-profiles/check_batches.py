"""Check map-effect direction in each completed 10k batch; descriptive, not a new significance test."""
import collections
import gzip
import json
from pathlib import Path
root = Path(__file__).resolve().parent
with gzip.open(root/'outcomes.json.gz', 'rt') as f: games = json.load(f)
with gzip.open(root/'first-batch-ids.json.gz', 'rt') as f: first = set(json.load(f))
names = {g['method']: g['name'] for g in json.loads((root/'catalog.json').read_text())['generators']}
assert len(games) == len({g['job_id'] for g in games}) == 20000
assert len(first) == 10000 and first <= {g['job_id'] for g in games}
out = {}
for batch, selected in [('first', [g for g in games if g['job_id'] in first]), ('second', [g for g in games if g['job_id'] not in first])]:
    base, cells = collections.defaultdict(list), collections.defaultdict(list)
    for g in selected:
        for i, ai in enumerate(g['competitors']):
            v = 1 if g['placements'][i] < g['placements'][1-i] else 0 if g['placements'][i] > g['placements'][1-i] else .5
            k = ai, g['competitors'][1-i], g['build']
            base[k].append(v)
            cells[ai,g['generator']].append((v,k))
    means = {k: sum(v)/len(v) for k,v in base.items()}
    out[batch] = {'games': len(selected), 'rows': [dict(ai=a, generator=names[m], games=len(v), score_percent=100*sum(x for x,k in v)/len(v), difference_pp=100*sum(x-means[k] for x,k in v)/len(v)) for (a,m),v in sorted(cells.items())]}
(root/'batch-check.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS: two disjoint complete 10,000-game batches')
