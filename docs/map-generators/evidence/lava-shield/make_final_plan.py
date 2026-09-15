"""Repeat every baseline and held-out request on the final immutable executable.

This paired request matrix can detect a fallback that repairs retained failures
but breaks a previously successful setting. Each new logical job has a new build
identity and retains its source cohort label for direct CSV comparison.
"""
import json
from pathlib import Path
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json
from tools.tournaments.model import job,validate_experiment

ROOT=Path(__file__).resolve().parent
BUNDLE=next((ROOT/'fallback-bundles').glob('*/bundle.json')).parent
BUILD=inspect_bundle(BUNDLE)['id']
jobs=[]
for cohort,source in [('bulk','plan.json'),('heldout','heldout-plan.json')]:
    for old in json.loads((ROOT/source).read_text())['jobs']:
        labels=dict(old['labels'],cohort=cohort)
        jobs.append(job('generate_map',BUILD,seeds=old['seeds'],
                        config=old['config'],outputs=old['outputs'],
                        limits=old['limits'],labels=labels))
manifest={'schema_version':1,'id':'lava-shield-final-bulk-20260915-v1',
          'kind':'generator_stress','jobs':jobs,
          'design':{'paired_request_sources':['plan.json','heldout-plan.json'],
                    'hypothesis':'Capacity-ranked patch retries rescue observed starter failures without breaking other legal maps.'},
          'labels':{'build':BUILD}}
validate_experiment(manifest)
atomic_json(ROOT/'final-plan.json',manifest)
print(json.dumps({'jobs':len(jobs),'build':BUILD}))
