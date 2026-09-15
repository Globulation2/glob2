"""Paired request comparison of original and final immutable cohorts.

Statuses are matched by complete parameters and seed, not completion order or
worker attempt. This separates rescued failures, new failures, and world metric
changes from launch errors on an incompatible worker.
"""
import hashlib
import json
from pathlib import Path
from tools.tournaments.results import Results

ROOT=Path(__file__).resolve().parent

def key(record):
    j=record['job']
    return (j['seeds']['map'],tuple(sorted(j['config']['params'].items())))

def world_digest(result):
    m=result.get('map_report') or {}
    if not m:return None
    fields={k:m.get(k) for k in ('terrain','resources','space','movement',
                                 'fertility','canonical_quality')}
    return hashlib.sha256(json.dumps(fields,sort_keys=True,separators=(',',':')).encode()).hexdigest()

old={key(x):x for study in ('results','heldout-results') for x in Results(ROOT/study)}
new={key(x):x for x in Results(ROOT/'final-results')}
assert len(old)==504, len(old)
paired=[]
for k,x in old.items():
    y=new.get(k)
    a=x['result']['status']; b=y['result']['status'] if y else 'pending'
    paired.append({'seed':k[0],'shape':x['job']['labels']['shape'],
                   'variant':x['job']['labels']['variant'],
                   'original':a,'final':b,
                   'world_metrics_changed':a==b=='completed' and
                                             world_digest(x['result'])!=world_digest(y['result'])})
summary={'planned':len(paired),'final_accepted':len(new),
         'rescued':[p for p in paired if p['original']=='generation_failed' and p['final']=='completed'],
         'new_failures':[p for p in paired if p['original']=='completed' and p['final']=='generation_failed'],
         'old_success_metric_changes':sum(p['world_metrics_changed'] for p in paired),
         'pending':sum(p['final']=='pending' for p in paired)}
(ROOT/'paired-comparison.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
