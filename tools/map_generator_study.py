#!/usr/bin/env python3
"""Bounded parameter search with fresh-process samples and held-out seeds."""
import argparse, atexit, csv, itertools, json, os, random, shutil, subprocess, threading, time
from pathlib import Path
from tournaments.local import run_job, parallel_map
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/map-generator-validation'
BINARY=ROOT/'build/src/glob2'
FIELDS=['method','seed','success','tiles','grass_tiles','sand_tiles','water_tiles','shore','free','fit4','um_grass','um_sand','um_water','seconds','hash']
EXTRA=['min_local_fit4','worst_wheat_distance','worst_wood_distance','viable_teams','wheat_tiles','wood_tiles','stone_tiles','algae_tiles','best_wheat_distance','best_wood_distance']
PROFILES=set()
atexit.register(lambda:[shutil.rmtree(Path.home()/('.'+p),ignore_errors=True) for p in PROFILES])

def sample(task):
    config,seed=task[:2]
    run_tag=task[2] if len(task)>2 else "sample"
    p=f'glob2-tuning-{os.getpid()}-{threading.get_ident()}';PROFILES.add(p)
    params={ {'w':'width','h':'height'}.get(key,key):value for key,value in config.get('params',{}).items() }
    start=time.monotonic()
    directory=OUT/'execution'/f'{config["id"]}-{seed}-{run_tag}'
    attempt,source=run_job(BINARY,ROOT,directory,'generate_map',
        config={'generator':config['method'],'params':params},seeds={'map':seed},timeout=12,
        outputs={'reports':['terrain']} if 'dump' in config else {})
    if 'dump' in config and attempt['category']=='success':
        with source.open_artifact(attempt,'terrain.txt') as stream:
            Path(config['dump']).write_text(stream.read())
    with source.open_artifact(attempt,'stdout.log') as stream: lines=stream.read().splitlines()
    a=[x for x in lines if x.startswith('STUDY,')]
    b=[x for x in lines if x.startswith('TUNE,')]
    if len(a)==len(b)==1:
        row=dict(zip(FIELDS,a[0].split(',')[1:]))|dict(zip(EXTRA,b[0].split(',')[1:]))
        return {'config':config['id'],**row,'status':'ok' if attempt['category']=='success' else attempt['category']}
    status=attempt['category'];detail=attempt.get('diagnostic','')
    (OUT/f'error-{config["id"]}-{seed}.txt').write_text(detail)
    return {'config':config['id'],'method':config['method'],'seed':seed,'success':0,'status':status,'seconds':time.monotonic()-start}

def run(configs,count,start,label,workers=8):
    # Fail before a long run if any production RNG stream escaped the seed controls.
    cases=[(config,seed) for config in configs for seed in sorted({start,start+count//2,start+count-1})]
    def repeat_case(case):
        config,seed=case
        a,b=sample((*case,"repeat-a")),sample((*case,"repeat-b"))
        assert {k:v for k,v in a.items() if k!='seconds'} == {k:v for k,v in b.items() if k!='seconds'}, (config['id'],seed,'non-reproducible generation')
        return a,b
    repeated=[row for pair in parallel_map(repeat_case,cases,workers) for row in pair]
    with (OUT/(label+'-reproducibility.csv')).open('w') as f:
        writer=csv.DictWriter(f,['config']+FIELDS+EXTRA+['status']);writer.writeheader();writer.writerows(repeated)
    print(f'PASS reproducibility: {len(repeated)//2} independently repeated seed/configuration pairs',flush=True)
    tasks=[(c,s) for s in range(start,start+count) for c in configs]
    begun=time.monotonic()
    (OUT/(label+'-configs.json')).write_text(json.dumps(configs,indent=2))
    with (OUT/(label+'.csv')).open('w') as f:
        writer=csv.DictWriter(f,['config']+FIELDS+EXTRA+['status']);writer.writeheader()
        for i,row in enumerate(parallel_map(sample,tasks,workers)):
            writer.writerow(row);f.flush()
            if (i+1)%500==0:print(f'{label}: {i+1}/{len(tasks)}, {time.monotonic()-begun:.0f}s',flush=True)
    print(f'DONE {label}: {len(tasks)} attempts, {time.monotonic()-begun:.1f}s',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--configs');p.add_argument('--binary',default=str(BINARY));p.add_argument('--output',default=str(OUT));p.add_argument('--count',type=int,default=32);p.add_argument('--start',type=int,default=10001);p.add_argument('--label',default='sweep');p.add_argument('--workers',type=int,default=8)
    a=p.parse_args();OUT=Path(a.output);OUT.mkdir(parents=True,exist_ok=True);BINARY=Path(a.binary).resolve()
    if a.configs:
        c=json.loads(Path(a.configs).read_text())
    else:
        catalog=json.loads(subprocess.check_output([BINARY,'--headless-catalog'],text=True))
        c=[{'id':'modular-'+str(d['method']),'method':d['method'],'preset':True} for d in catalog['generators'] if not d.get('editorOnly',d['method']==0)]
    run(c,a.count,a.start,a.label,a.workers)
