#!/usr/bin/env python3
"""Bounded parameter search with fresh-process samples and held-out seeds."""
import argparse, atexit, concurrent.futures, csv, itertools, json, os, random, shutil, subprocess, threading, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/map-generator-validation'
BINARY=ROOT/'build/src/MapGeneratorStudy'
FIELDS=['method','seed','success','tiles','grass_tiles','sand_tiles','water_tiles','shore','free','fit4','um_grass','um_sand','um_water','seconds','hash']
EXTRA=['min_local_fit4','worst_wheat_distance','worst_wood_distance','viable_teams','wheat_tiles','wood_tiles','stone_tiles','algae_tiles']
PROFILES=set()
atexit.register(lambda:[shutil.rmtree(Path.home()/('.'+p),ignore_errors=True) for p in PROFILES])

def sample(task):
    config,seed=task
    p=f'glob2-tuning-{os.getpid()}-{threading.get_ident()}';PROFILES.add(p)
    params=config.get('params',{})
    command=[str(BINARY),str(config['method']),str(seed),p,'tuning']
    command += ['preset']
    command += [f'{k}={v}' for k,v in params.items()]
    if 'dump' in config: command += ['dump='+config['dump']]
    start=time.monotonic()
    try:
        result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=12)
        lines=result.stdout.splitlines()
        a=[x for x in lines if x.startswith('STUDY,')]
        b=[x for x in lines if x.startswith('TUNE,')]
        if result.returncode==0 and len(a)==len(b)==1:
            row=dict(zip(FIELDS,a[0].split(',')[1:]))|dict(zip(EXTRA,b[0].split(',')[1:]))
            if row['success'] != '1':
                (OUT/f'failure-{config["id"]}-{seed}.txt').write_text(result.stderr)
            return {'config':config['id'],**row,'status':'ok' if row['success']=='1' else 'failed'}
        status=f'exit_{result.returncode}'
        detail=result.stdout[-1500:]+result.stderr[-1500:]
    except subprocess.TimeoutExpired:
        status='timeout';detail='12-second timeout'
    (OUT/f'error-{config["id"]}-{seed}.txt').write_text(detail)
    return {'config':config['id'],'method':config['method'],'seed':seed,'success':0,'status':status,'seconds':time.monotonic()-start}

def run(configs,count,start,label,workers=8):
    # Fail before a long run if any production RNG stream escaped the seed controls.
    cases=[(config,seed) for config in configs for seed in sorted({start,start+count//2,start+count-1})]
    def repeat_case(case):
        config,seed=case
        a,b=sample(case),sample(case)
        assert {k:v for k,v in a.items() if k!='seconds'} == {k:v for k,v in b.items() if k!='seconds'}, (config['id'],seed,'non-reproducible generation')
        return a,b
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        repeated=[row for pair in pool.map(repeat_case,cases) for row in pair]
    with (OUT/(label+'-reproducibility.csv')).open('w') as f:
        writer=csv.DictWriter(f,['config']+FIELDS+EXTRA+['status']);writer.writeheader();writer.writerows(repeated)
    print(f'PASS reproducibility: {len(repeated)//2} independently repeated seed/configuration pairs',flush=True)
    tasks=[(c,s) for s in range(start,start+count) for c in configs]
    begun=time.monotonic()
    (OUT/(label+'-configs.json')).write_text(json.dumps(configs,indent=2))
    with (OUT/(label+'.csv')).open('w') as f, concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        writer=csv.DictWriter(f,['config']+FIELDS+EXTRA+['status']);writer.writeheader()
        for i,row in enumerate(pool.map(sample,tasks)):
            writer.writerow(row);f.flush()
            if (i+1)%500==0:print(f'{label}: {i+1}/{len(tasks)}, {time.monotonic()-begun:.0f}s',flush=True)
    print(f'DONE {label}: {len(tasks)} attempts, {time.monotonic()-begun:.1f}s',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--configs');p.add_argument('--binary',default=str(BINARY));p.add_argument('--output',default=str(OUT));p.add_argument('--count',type=int,default=32);p.add_argument('--start',type=int,default=10001);p.add_argument('--label',default='sweep');p.add_argument('--workers',type=int,default=8)
    a=p.parse_args();OUT=Path(a.output);OUT.mkdir(parents=True,exist_ok=True);BINARY=Path(a.binary).resolve()
    if a.configs:
        c=json.loads(Path(a.configs).read_text())
    else:
        catalog=json.loads(subprocess.check_output([BINARY,'--catalog'],text=True))
        c=[{'id':'modular-'+str(d['method']),'method':d['method'],'preset':True} for d in catalog if not d.get('editorOnly',d['method']==0)]
    run(c,a.count,a.start,a.label,a.workers)
