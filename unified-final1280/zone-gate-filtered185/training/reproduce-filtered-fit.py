"""Reproduce selected filtered heads from the five original published archives.

Run from the tested checkout with the pinned fit environment. Archive extraction
uses the original evidence helper; no game is run. This performs only the final
combined+four-family development fits, not CV/search or new collection.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--evidence',type=Path,required=True,help='Extracted original five episode archives with terminal-inputs.json and map-splits.json')
parser.add_argument('--expected-model',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
checkout=Path.cwd()
sys.path.insert(0,str(checkout))
from tools import adaptive_gradient_collection as collection,adaptive_gradient_dataset as data,adaptive_gradient_model as model,resource_gradient_model as integer
from tools.resource_gradient_dataset import manifest_records

root=args.evidence.resolve()
inputs=json.loads((root/'terminal-inputs.json').read_text())
paths=[root/name for name in inputs['manifests']]
rosters={}
for path in paths:
    records,_=manifest_records(path)
    for record in records:
        identity='|'.join((record['group'],record['binary_sha256'],record['episode_sha256']))
        rosters[identity]=record['spec'].get('ais') or [record['spec']['ai']]*record['spec']['players']
all_rows=collection.load(paths)
rows=data.eligible(all_rows)
assert len(rows)==287045
nonpainters={'cabino','castor','cortex','econo','nicowar','numbi'}
retained=[row for row in rows if not (row['family']=='guard' and rosters[row['collection']][row['team']] in nonpainters)]
assert len(retained)==237120
registry=root/'map-splits.json'
registry_hash=hashlib.sha256(registry.read_bytes()).hexdigest()
assignment=json.loads(registry.read_text())['assignments']
assert all(row['group'] in assignment for row in retained)
development=[row for row in retained if assignment[row['group']]!=-1]
assert len(development)==181538
expected=json.loads(args.expected_model.read_text())
model.validate(expected)
actual=integer.fit_heads(development,data.FEATURES,data.TRANSFORMS,weighting=data.balanced_weights,threshold=24000)
actual['family_experts']=model.fit_family_experts(development,24000)
for key in ('features','transforms',*integer.HEADS):
    if actual[key]!=expected[key]:raise ValueError('Combined head mismatch: '+key)
if actual['family_experts']!=expected['family_experts']:raise ValueError('Family expert coefficients/support mismatch')
assert hashlib.sha256(registry.read_bytes()).hexdigest()==registry_hash
args.output.write_text(json.dumps(actual,sort_keys=True,indent=2)+'\n')
print('Exact filtered combined and four family expert heads reproduced; map holdouts unchanged.')
