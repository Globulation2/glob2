#!/usr/bin/env python3
"""Bulk Savannah generation: retain every request/report, never retry a failed seed.

The finite control space is enormous (13^5 resource combinations before terrain and
size choices). This matrix covers every *individual* control value, every dry/pond
pair on the smallest crowded map and both 2:1 orientations, then samples mixed
controls independently. All requests are within the documented size/area envelope;
actual spacing and essential pond fit remain generator checks under test.
"""
import json, random, statistics, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
root=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/'tools'))
import map_telemetry as mt

RESOURCES=['wheat','wood','stone','algae','fruit']
AMOUNTS=list(range(0,301,25))
SHAPES=[(128,128,1),(128,128,3),(128,128,4),(256,128,1),(256,128,5),
        (256,128,8),(128,256,1),(128,256,5),(128,256,8),(256,256,4),
        (256,256,8),(256,256,12),(512,256,12),(256,512,12),(512,512,12)]

def request(shape,pond=1,dry=8,workers=4,resources=None):
    w,h,teams=shape
    d=dict(width=str(w),height=str(h),teams=str(teams),workers=str(workers),
           **{'watering-holes':str(pond),'dry-patches':str(dry)})
    d.update({r+'-amount':str((resources or {}).get(r,100)) for r in RESOURCES})
    return d

def matrix(mode):
    cases=[]
    def add(group,shape,pond=1,dry=8,workers=4,resources=None):
        cases.append(dict(group=group,settings=request(shape,pond,dry,workers,resources)))
    if mode=='pilot':
        # Two resource extremes deliberately stress starter floors, plot saturation,
        # crowded pond reservations and vegetation on each of the tight map shapes.
        for shape in [(128,128,4),(256,128,8),(128,256,8),(256,256,12),(128,128,1)]:
            for pond in range(3):
                for dry in [0,8,20]:
                    for amount in [0,300]:
                        add('pilot-edges',shape,pond,dry,1 if amount==0 else 8,
                            {r:amount for r in RESOURCES})
        rng=random.Random(62073)
        for i in range(40):
            shape=SHAPES[i%len(SHAPES)]
            add('pilot-mixed',shape,rng.randrange(3),rng.randrange(21),
                rng.choice([1,4,8]),{r:rng.choice(AMOUNTS) for r in RESOURCES})
        first_seed=11000
    elif mode=='full':
        # 63 terrain pairs on the small crowded square and both rectangle directions.
        for shape in [(128,128,4),(256,128,8),(128,256,8)]:
            for pond in range(3):
                for dry in range(21):
                    add('terrain-grid',shape,pond,dry)
        # All 13 legal values of each resource control; repeat on both rectangles.
        for shape in [(128,128,4),(256,128,8),(128,256,8)]:
            for r in RESOURCES:
                for amount in AMOUNTS:
                    add('resource-ladder',shape,resources={r:amount})
        # Default supplies across a broad size/colony envelope and eight independent seeds
        # per representative setting. The per-case seed assignment below is unique.
        for shape in [(128,128,1),(128,128,3),(128,128,4),(256,128,5),
                      (256,128,8),(128,256,5),(128,256,8),(256,256,12),
                      (512,256,12),(256,512,12),(512,512,12)]:
            for _ in range(8):add('size-seeds',shape)
        # All-zero and all-maximum resources at dry extremes on each legal shape.
        for shape in SHAPES:
            for amount in [0,300]:
                add('shape-edges',shape,2 if amount==300 else 0,
                    20 if amount==300 else 0,8 if amount==300 else 1,
                    {r:amount for r in RESOURCES})
        # Independent mixed combinations, including worker extremes. Fixed Python seed
        # makes the request manifest reproducible without biasing engine map seeds.
        rng=random.Random(74291)
        for i in range(240):
            shape=SHAPES[i%len(SHAPES)]
            add('mixed',shape,rng.randrange(3),rng.randrange(21),
                rng.choice([1,4,8]),{r:rng.choice(AMOUNTS) for r in RESOURCES})
        first_seed=23000
    else:raise ValueError('mode must be pilot or full')
    for i,c in enumerate(cases):c['seed']=first_seed+i
    return cases

def run(out,mode,jobs):
    out=Path(out).resolve();out.mkdir(exist_ok=False,parents=True)
    cases=matrix(mode);mt.write_json(out/'requests.json',cases)
    binary=root/'build/src/glob2'
    def work(v):
        i,c=v
        return mt.attempt(binary,'savannah',c['seed'],c['settings'],out/f'attempt-{i:04}',120)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results=list(pool.map(work,enumerate(cases)))
    mt.summarize(out)
    groups={};shape_groups={};failures=[];pond_omissions=0;timings=[]
    for i,(case,result) in enumerate(zip(cases,results)):
        ok=result['status']=='ok';g=groups.setdefault(case['group'],[0,0]);g[0]+=1;g[1]+=ok
        s=case['settings'];shape=f"{s['width']}x{s['height']}/{s['teams']}"
        h=shape_groups.setdefault(shape,[0,0]);h[0]+=1;h[1]+=ok
        timings.append(result.get('seconds',0))
        try:
            report=json.loads((out/f'attempt-{i:04}'/'report.json').read_text())
            records=(report.get('generation',{}).get('telemetry') or {}).get('records',[])
            values={r['key']:r.get('value') for r in records if r['key'].startswith('savannah.ponds.')}
            if values.get('savannah.ponds.placed',0)<values.get('savannah.ponds.requested',0):pond_omissions+=1
        except (OSError,ValueError):report={}
        if not ok:
            failures.append(dict(index=i,group=case['group'],seed=case['seed'],settings=s,
                                 status=result['status'],returncode=result.get('returncode'),
                                 outcome=result.get('outcome'),
                                 report_type=result.get('report_type'),
                                 diagnostic=report.get('generation',{}).get('outcome'),
                                 report=f'attempt-{i:04}/report.json'))
    summary=dict(mode=mode,attempts=len(results),passed=sum(r['status']=='ok' for r in results),
                 failed=len(failures),groups={k:dict(attempts=v[0],passed=v[1]) for k,v in groups.items()},
                 shapes={k:dict(attempts=v[0],passed=v[1]) for k,v in shape_groups.items()},
                 pond_omission_maps=pond_omissions,process_seconds=dict(mean=statistics.mean(timings),
                 maximum=max(timings)),failures=failures)
    mt.write_json(out/'aggregate.json',summary)
    print(json.dumps({k:summary[k] for k in ['mode','attempts','passed','failed','pond_omission_maps','process_seconds']}))
    return summary

if __name__=='__main__':
    if len(sys.argv)!=4:
        raise SystemExit('usage: bulk_sweep.py OUT pilot|full JOBS')
    run(sys.argv[1],sys.argv[2],int(sys.argv[3]))
