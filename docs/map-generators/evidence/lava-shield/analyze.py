"""Condense committed map reports without counting worker retries as new maps.

Only accepted logical records in the immutable manifest count. This script keeps
all settings and failure reasons per row, so a reviewer can find a rare bad
corner instead of relying on an aggregate success percentage.
"""
import csv
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from tools.tournaments.results import Results

ROOT = Path(__file__).resolve().parent
study = sys.argv[1] if len(sys.argv)>1 else 'results'
prefix = {'heldout-results':'heldout-', 'final-results':'final-'}.get(study,'')
records = Results(ROOT/study)
planned = records.manifest['jobs']
accepted = {r['job']['id']:r for r in records}
rows = []
for j in planned:
    r = accepted.get(j['id'])
    result = r.get('result',{}) if r else {}
    m = result.get('map_report') or {}
    quality = result.get('quality') or {}
    telemetry = ((m.get('generation') or {}).get('telemetry') or {})
    walking = ((m.get('movement') or {}).get('walking') or {})
    water = (((m.get('space') or {}).get('water_regions') or {}).get('component_sizes') or {})
    measures = defaultdict(list)
    fallbacks = set()
    for event in telemetry.get('records',[]):
        if event['kind']=='measurement' and isinstance(event.get('value'),(int,float)):
            measures[event['key']].append(event['value'])
        elif event['kind']=='fallback':
            fallbacks.add(event['key'])
    p = j['config']['params']
    colonies = quality.get('colonies') or []
    rows.append(dict(job=j['id'],host=r.get('host','') if r else '',
                     seed=j['seeds']['map'],variant=j['labels']['variant'],
                     shape=j['labels']['shape'],cohort=j['labels'].get('cohort',''),
                     status=result.get('status','pending'),
                     diagnostic=result.get('diagnostic',''),
                     **{k:p[k] for k in sorted(p)},
                     fairness=quality.get('fairness',''),
                     score=quality.get('score',''),
                     minimum_build_sites=min((c['build_sites'] for c in colonies),default=''),
                     minimum_wheat=min((c['wheat'] for c in colonies),default=''),
                     minimum_wood=min((c['wood'] for c in colonies),default=''),
                     branches_requested=sum(measures['lava-shield.branches.requested']),
                     branches_placed=sum(measures['lava-shield.branches.placed']),
                     approaches_narrow='lava-shield.approach.narrow' in fallbacks,
                     starter_secondary='lava-shield.starter.secondary' in fallbacks,
                     unreachable_walking_pairs=walking.get('unreachable_directed_pairs',''),
                     water_components=water.get('count',''),
                     telemetry_dropped=telemetry.get('dropped_records',''),
                     telemetry_invalid=telemetry.get('invalid_values',''),
                     report_version=m.get('schema_version','')))

with (ROOT/(prefix+'jobs.csv')).open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=rows[0].keys())
    writer.writeheader();writer.writerows(rows)
outcomes=Counter(x['status'] for x in rows)
groups={}
for field in ('cohort','shape','variant'):
    group=defaultdict(Counter)
    for x in rows:group[x[field]][x['status']]+=1
    groups[field]={k:dict(v) for k,v in sorted(group.items())}
failures=[x for x in rows if x['status'] not in ('completed','pending')]
complete=[x for x in rows if x['status']=='completed']
summary={'planned':len(planned),'accepted':len(accepted),'outcomes':dict(outcomes),
         'completed_percent_of_accepted':round(100*len(complete)/len(accepted),2) if accepted else None,
         'groups':groups,'failures':failures,
         'quality':{'minimum_fairness':min((x['fairness'] for x in complete),default=None),
                    'minimum_build_sites':min((x['minimum_build_sites'] for x in complete),default=None),
                    'minimum_wheat':min((x['minimum_wheat'] for x in complete),default=None),
                    'minimum_wood':min((x['minimum_wood'] for x in complete),default=None),
                    'maximum_unreachable_walking_pairs':max((x['unreachable_walking_pairs'] for x in complete),default=None),
                    'water_component_counts':dict(Counter(x['water_components'] for x in complete))},
         'telemetry':{'completed_with_record':sum(bool(x['report_version']) for x in complete),
                      'maps_with_narrow_approach':sum(x['approaches_narrow'] for x in complete),
                      'maps_with_secondary_starter':sum(x['starter_secondary'] for x in complete),
                      'dropped_records':sum(x['telemetry_dropped'] for x in complete),
                      'invalid_values':sum(x['telemetry_invalid'] for x in complete)}}
(ROOT/(prefix+'summary.json')).write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n')
print(json.dumps({k:summary[k] for k in ('planned','accepted','outcomes','quality','telemetry')},indent=2))
if failures:
    print('FAILURES',json.dumps([{k:x[k] for k in ('shape','variant','seed','status','diagnostic')} for x in failures],ensure_ascii=False))
