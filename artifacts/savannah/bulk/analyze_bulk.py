#!/usr/bin/env python3
"""Audit full discrete control coverage and final-world metrics from retained reports.

Rates are per generated map. Optional pond and plot saturation records may repeat on
one map, so they are not counted as independent failures or additional attempts.
"""
import collections,json,statistics,sys
from pathlib import Path
AMOUNTS=list(range(0,301,25))
RESOURCES=['wheat','wood','stone','algae','fruit']

def analyze(path):
    root=Path(path);cases=json.loads((root/'requests.json').read_text())
    aggregate=json.loads((root/'aggregate.json').read_text())
    values={k:set() for k in ['watering-holes','dry-patches','workers']+[r+'-amount' for r in RESOURCES]}
    pond=collections.defaultdict(lambda:dict(maps=0,requested=0,placed=0,omitted_maps=0,min_placed=999))
    quality=[];room=[];wheat=[];wood=[];worst=[];plot_saturation_maps=0;zero_pond_maps=0
    for i,c in enumerate(cases):
        s=c['settings']
        for key in values:values[key].add(int(s[key]))
        manifest=json.loads((root/f'attempt-{i:04}'/'manifest.json').read_text())
        if manifest['status']!='ok':continue
        report=json.loads((root/f'attempt-{i:04}'/'report.json').read_text())
        tel=report['generation']['telemetry'];records=tel['records']
        measurements={r['key']:r.get('value') for r in records if r['kind']=='measurement' and r['subject'] is None}
        wanted=int(measurements['savannah.ponds.requested']);placed=int(measurements['savannah.ponds.placed'])
        shape=f"{s['width']}x{s['height']}/{s['teams']}";key=(shape,int(s['watering-holes']))
        group=pond[key];group['maps']+=1;group['requested']+=wanted;group['placed']+=placed
        group['omitted_maps']+=placed<wanted;group['min_placed']=min(group['min_placed'],placed)
        zero_pond_maps+=placed==0
        if any(r['key']=='savannah.plot.saturated' for r in records):plot_saturation_maps+=1
        fairness=report['canonical_quality']['fairness'];quality.append(fairness)
        colonies=report['movement']['walking']['colonies']
        worst_wheat=max(v['resources']['wheat']['nearest_gather_cost'] for v in colonies)
        worst_wood=max(v['resources']['wood']['nearest_gather_cost'] for v in colonies)
        min_room=min(v['raw']['build_sites_4x4'] for v in report['canonical_quality']['colonies'])
        wheat.append(worst_wheat);wood.append(worst_wood);room.append(min_room)
        worst.append(dict(index=i,seed=c['seed'],shape=shape,fairness=fairness,
                          worst_wheat_distance=worst_wheat,worst_wood_distance=worst_wood,
                          weakest_build_room=min_room,requested_ponds=wanted,placed_ponds=placed,
                          settings=s,report=f'attempt-{i:04}/report.json'))
    coverage={key:sorted(v) for key,v in values.items()}
    expected={'watering-holes':[0,1,2],'dry-patches':list(range(21)),
              'workers':[1,4,8],**{r+'-amount':AMOUNTS for r in RESOURCES}}
    checks={key:coverage[key]==expected[key] for key in expected}
    def pct(x,p):return sorted(x)[min(len(x)-1,int((len(x)-1)*p))] if x else None
    result=dict(attempts=aggregate['attempts'],passed=aggregate['passed'],failed=aggregate['failed'],
        success_rate=aggregate['passed']/aggregate['attempts'],shapes=aggregate['shapes'],
        control_coverage=coverage,all_discrete_control_values_covered=all(checks.values()),
        coverage_checks=checks,ponds={f'{k[0]} choice{k[1]}':v for k,v in sorted(pond.items())},
        pond_omission_maps=sum(v['omitted_maps'] for v in pond.values()),
        zero_pond_maps=zero_pond_maps,
        plot_saturation_maps=plot_saturation_maps,
        static_metrics=dict(min_fairness=min(quality),p05_fairness=pct(quality,.05),
            max_wheat_gather_steps=max(wheat),max_wood_gather_steps=max(wood),
            min_weakest_home_build_sites_4x4=min(room)),
        slowest_seconds=aggregate['process_seconds']['maximum'],
        weakest_fairness=sorted(worst,key=lambda x:x['fairness'])[:5],
        longest_wheat=sorted(worst,key=lambda x:-x['worst_wheat_distance'])[:5],
        least_room=sorted(worst,key=lambda x:x['weakest_build_room'])[:5],
        failures=aggregate['failures'])
    (root/'audit.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ['attempts','passed','failed','success_rate',
      'all_discrete_control_values_covered','pond_omission_maps','zero_pond_maps',
      'plot_saturation_maps','static_metrics','slowest_seconds']},indent=2))
    return result
if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit('usage: analyze_bulk.py OUTPUT_DIR')
    analyze(sys.argv[1])
