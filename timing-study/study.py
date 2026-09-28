#!/usr/bin/env python3
"""Fixed, balanced blocked comparison of baseline/eager/lazy executables."""
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import random
import re
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
BINS = {'base': ROOT/'artifacts/lazy-gradient/glob2-base',
        'eager': ROOT/'artifacts/lazy-gradient-cleanup/glob2',
        'lazy': ROOT/'artifacts/lazy-gradient-cleanup/glob2'}

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()

def output(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout.strip()

def environment():
    power = output(['pmset', '-g', 'batt'])
    match = re.search(r'(\d+)%;', power)
    return {'time': time.time(), 'loadavg': os.getloadavg(), 'power': power,
            'power_source': 'battery' if "'Battery Power'" in power else 'ac',
            'battery_percent': int(match.group(1)) if match else None,
            'thermal': output(['pmset', '-g', 'therm'])}

def perf(path):
    scopes = {}
    for line in path.read_text().splitlines():
        if line.startswith('GLOB2_PERF_FINAL '):
            fields = dict(x.split('=',1) for x in shlex.split(line)[1:] if '=' in x)
            if 'scope' in fields: scopes[fields['scope']] = fields
    return scopes

env = os.environ.copy()
for key in list(env):
    if key.startswith('GLOB2_'): env.pop(key)
env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')

if not (OUT/'plan.json').exists():
    cases = json.loads((ROOT/'artifacts/lazy-gradient/cases.json').read_text())
    for c in cases:
        c.update(map_file=str(ROOT/'artifacts/lazy-gradient/runs'/c['name']/'generated/map-r0.map'),
                 repetitions=12 if c['name'] in ['arena128-2','arena256-2'] else 6,
                 family='original', save_every=0)
    # New seeds are preselected, not chosen by measured speedup.
    for name, generator, map_seed, game_seed in [('arena256-seed73',15,73,31),('lakes256-seed131',4,131,31)]:
        c = dict(name=name, generator=generator, map_seed=map_seed, game_seed=game_seed,
                 params={'width':8,'height':8,'teams':4}, players=['nicowar','warrush','cortex','maxima'],
                 ticks=8192, repetitions=6, family='additional-seed', save_every=0)
        dest=OUT/'maps'/name
        cmd=[str(BINS['base']), '--generate-map', '--generator', str(generator), '--map-seed', str(map_seed), '--write-map','true','--output-dir',str(dest)]
        for k,v in c['params'].items():cmd += ['--param',f'{k}={v}']
        with (OUT/f'generate-{name}.log').open('w') as f:subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
        c['generation_command']=cmd;c['map_file']=str(dest/'map-r0.map');cases.append(c)
    c=dict(cases[3]);c.update(name='lakes256-checkpoints',family='checkpoint',save_every=4096,repetitions=6);cases.append(c)
    rng=random.Random(20260928)
    orders=list(itertools.permutations(BINS));schedules={}
    for c in cases:
        schedule=[]
        for cycle in range(c['repetitions']//6):
            balanced=list(orders);rng.shuffle(balanced);schedule.extend(balanced)
        schedules[c['name']]=schedule
        c['map_sha256']=digest(Path(c['map_file']))
    blocks=[]
    for repeat in range(max(c['repetitions'] for c in cases)):
        eligible=[c for c in cases if repeat<c['repetitions']];rng.shuffle(eligible)
        for c in eligible:blocks.append(dict(case=c['name'],repeat=repeat,order=schedules[c['name']][repeat]))
    plan={'seed':20260928,'source_commit':output(['git','rev-parse','HEAD']),
          'platform':platform.platform(),'cpu':output(['sysctl','-n','machdep.cpu.brand_string']),
          'memory_bytes':output(['sysctl','-n','hw.memsize']),'power_settings':output(['pmset','-g','custom']),
          'binaries':{k:{'path':str(v),'sha256':digest(v)} for k,v in BINS.items()},
          'cases':cases,'blocks':blocks,
          'policy':'Serial, no compilation or compression during timings. Six permutations balanced per six blocks; case order shuffled per round. No outcome-based exclusions or stopping. Primary CPU user+system; paired log-ratio t intervals by block. Flag source transitions, thermal warnings, and large wall/CPU gaps; report sensitivity separately. Stop before starting another block if battery falls below 12%. Three arena128 warmups excluded. Checkpoints are synchronous headless saves, not GUI asynchronous autosaves.'}
    (OUT/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
else:
    plan=json.loads((OUT/'plan.json').read_text())
    assert all(digest(Path(v['path']))==v['sha256'] for v in plan['binaries'].values())

cases={c['name']:c for c in plan['cases']}
records_path=OUT/'measurements.jsonl'
records=[json.loads(s) for s in records_path.read_text().splitlines()] if records_path.exists() else []
done={(r['case'],r['repeat'],r['variant']) for r in records}
expected={r['case']:{k:r['result'][k] for k in ['ticks','termination','teams']} for r in records if r['variant']=='base'}


def run(c, variant, repeat, warmup=False):
    label=f"{c['name']}-r{repeat:02d}-{variant}" if not warmup else f'warmup-{variant}'
    dest=OUT/'runs'/label;log=OUT/'logs'/f'{label}.log';log.parent.mkdir(exist_ok=True);dest.parent.mkdir(exist_ok=True)
    assert not dest.exists(),dest
    cmd=[str(BINS[variant]),'--run-game','--map-file',c['map_file'],'--game-seed',str(c['game_seed']),
         '--ticks',str(c['ticks']),'--telemetry','team-timeline','--output-dir',str(dest)]
    if variant!='base':cmd+=['--lazy-building-gradients',str(variant=='lazy').lower()]
    if c['save_every']:cmd+=['--save',f"every:{c['save_every']}"]
    for ai in c['players']:cmd+=['--player',ai]
    before=environment();start=time.monotonic()
    print(f'RUN {label} power={before["power_source"]} battery={before["battery_percent"]}',flush=True)
    with log.open('w') as stream:
        proc=subprocess.Popen(cmd,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT)
        _,status,usage=os.wait4(proc.pid,0);proc.returncode=os.waitstatus_to_exitcode(status)
    wall=time.monotonic()-start;after=environment()
    if proc.returncode:raise RuntimeError((label,proc.returncode))
    result=json.loads((dest/'result.json').read_text());scopes=perf(log)
    if variant=='lazy':assert int(scopes['gradient.building_resume']['calls'])>0
    observed={k:result[k] for k in ['ticks','termination','teams']}
    if c['name'] in expected:assert observed==expected[c['name']],label
    if variant=='base':expected[c['name']]=observed
    r={'case':c['name'],'repeat':repeat,'variant':variant,'warmup':warmup,'command':cmd,
       'binary_sha256':plan['binaries'][variant]['sha256'],'before':before,'after':after,
       'cpu_s':usage.ru_utime+usage.ru_stime,'user_s':usage.ru_utime,'system_s':usage.ru_stime,
       'wall_s':wall,'peak_rss_bytes':usage.ru_maxrss,'minor_faults':usage.ru_minflt,
       'major_faults':usage.ru_majflt,'voluntary_switches':usage.ru_nvcsw,'involuntary_switches':usage.ru_nivcsw,
       'performance':scopes,'result':result}
    with (OUT/'warmups.jsonl' if warmup else records_path).open('a') as f:f.write(json.dumps(r)+'\n')
    print(f'DONE {label} cpu={r["cpu_s"]:.3f}s wall={wall:.3f}s rss={r["peak_rss_bytes"]/1048576:.1f}MiB',flush=True)
    return r

if not records and not (OUT/'warmups.jsonl').exists():
    for variant in BINS:run(cases['arena128-2'],variant,0,True)
for block_index,block in enumerate(plan['blocks']):
    if all((block['case'],block['repeat'],v) in done for v in block['order']):continue
    e=environment()
    if e['power_source']=='battery' and e['battery_percent'] is not None and e['battery_percent']<12:
        (OUT/'stopped.json').write_text(json.dumps({'reason':'battery below 12%; completed blocks retained','environment':e,'block':block_index},indent=2)+'\n')
        print('STOP battery below 12%; resume on external power',flush=True);sys.exit(3)
    for variant in block['order']:
        if (block['case'],block['repeat'],variant) in done:continue
        r=run(cases[block['case']],variant,block['repeat']);records.append(r);done.add((r['case'],r['repeat'],variant))
    siblings=[r for r in records if r['case']==block['case'] and r['repeat']==block['repeat']]
    assert len(siblings)==3
    for r in siblings:
        assert {k:r['result'][k] for k in ['ticks','termination','teams']}==expected[block['case']]
    print(f'BLOCK {block_index+1}/{len(plan["blocks"])} complete',flush=True)
(OUT/'completed.json').write_text(json.dumps({'completed_at':time.time(),'runs':len(records),'blocks':len(plan['blocks'])},indent=2)+'\n')
print('STUDY COMPLETE',flush=True)
