"""Paired old/new executable validation using shared immutable tournament jobs.

Keep every held-out geometry/resource corner, every crossing interaction, one
seed per supported topology, and all recorded metric tails. No new engine runner.
"""
import json, sys
from pathlib import Path
from tools.tournaments.model import job, validate_experiment
from tools.tournaments.results import Results
main, boundary = Results(sys.argv[1]), Results(sys.argv[2])
new_build = sys.argv[3]
selected = {}
for results, summary_path in [(main,sys.argv[4]),(boundary,sys.argv[5])]:
    summary=json.loads(Path(summary_path).read_text())
    tails={m[k] for m in summary['metrics'].values() for k in ('minimum_job','maximum_job')}
    for r in results:
        j=r['job'];cohort=j['labels']['variant']
        keep=(results is boundary or j['id'] in tails or cohort in ('crossing-grid','corners')
              or (cohort=='topology' and j['seeds']['map']==71001 and r['category']=='success'))
        if keep:
            key=json.dumps([j['config'],j['seeds']],sort_keys=True)
            selected[key]=j
jobs=[]
for i,j in enumerate(selected.values()):
    for variant,build in [('baseline',j['build']),('shared',new_build)]:
        jobs.append(job('generate_map',build,seeds=j['seeds'],config=j['config'],
            outputs={'map':True},limits=j['limits'],labels={'variant':variant,'pair':i,'original_job':j['id']}))
m={'schema_version':1,'id':'highlands-shared-pairs-20260915','jobs':jobs,'settings':main.manifest.get('settings',{})}
validate_experiment(m)
Path(sys.argv[6]).write_text(json.dumps(m,indent=2)+'\n')
print(len(selected),'paired worlds;',len(jobs),'jobs')
