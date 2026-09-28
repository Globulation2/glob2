#!/usr/bin/env python3
"""Replay the recorded balanced timing plan from a source checkout.

Build base commit 1c49596f5 and PR commit d50968d06 with the same release compiler.
Pass their executables explicitly. This tool expects fixtures/ beside plan.json.
Outputs must be new; no existing results are overwritten.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import shlex
import subprocess
import sys
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base-binary',type=Path,required=True)
p.add_argument('--pr-binary',type=Path,required=True)
p.add_argument('--plan',type=Path,default=Path(__file__).with_name('plan.json'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--case',help='Optional single case from the plan')
a=p.parse_args();plan=json.loads(a.plan.read_text());out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
cases={c['name']:c for c in plan['cases']};binaries={'base':a.base_binary.resolve(),'eager':a.pr_binary.resolve(),'lazy':a.pr_binary.resolve()}
if a.case and a.case not in cases:p.error('case is not in the plan')
env=os.environ.copy()
for k in list(env):
    if k.startswith('GLOB2_'):env.pop(k)
env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
expected={}
shutil.copyfile(a.plan,out/'plan.json')

def environment():
    state={'time':time.time(),'loadavg':os.getloadavg(),'power_source':'unknown'}
    if sys.platform=='darwin':
        power=subprocess.check_output(['pmset','-g','batt'],text=True).strip()
        state.update(power=power,power_source='battery' if "'Battery Power'" in power else 'ac',
                     thermal=subprocess.check_output(['pmset','-g','therm'],text=True).strip())
    return state

def scopes(path):
    result={}
    for line in path.read_text().splitlines():
        if line.startswith('GLOB2_PERF_FINAL '):
            fields=dict(s.split('=',1) for s in shlex.split(line)[1:] if '=' in s)
            if 'scope' in fields:result[fields['scope']]=fields
    return result

def run(case,variant,label,repeat=0,warmup=False):
    fixture=a.plan.resolve().parent/'fixtures'/f"{case['name']}.map"
    assert hashlib.sha256(fixture.read_bytes()).hexdigest()==case['map_sha256']
    command=[str(binaries[variant]),'--run-game','--map-file',str(fixture),'--game-seed',str(case['game_seed']),
             '--ticks',str(case['ticks']),'--telemetry','team-timeline','--output-dir',str(out/label)]
    if variant!='base':command+=['--lazy-building-gradients',str(variant=='lazy').lower()]
    if case['save_every']:command+=['--save',f"every:{case['save_every']}"]
    for player in case['players']:command+=['--player',player]
    log=out/f'{label}.log';before=environment();started=time.monotonic()
    with log.open('w') as f:
        proc=subprocess.Popen(command,env=env,stdout=f,stderr=subprocess.STDOUT)
        _,status,usage=os.wait4(proc.pid,0);proc.returncode=os.waitstatus_to_exitcode(status)
    assert proc.returncode==0,(label,proc.returncode)
    wall=time.monotonic()-started;after=environment();result=json.loads((out/label/'result.json').read_text())
    outcomes={k:result[k] for k in ['ticks','termination','teams']}
    if case['name'] in expected:assert outcomes==expected[case['name']]
    else:expected[case['name']]=outcomes
    row={'case':case['name'],'variant':variant,'label':label,'warmup':warmup,'repeat':repeat,'before':before,'after':after,'command':command,'result':result,
         'cpu_s':usage.ru_utime+usage.ru_stime,'wall_s':wall,
         'user_s':usage.ru_utime,'system_s':usage.ru_stime,'major_faults':usage.ru_majflt,
         'minor_faults':usage.ru_minflt,'involuntary_switches':usage.ru_nivcsw,'voluntary_switches':usage.ru_nvcsw,
         'peak_rss_bytes':usage.ru_maxrss*(1 if sys.platform=='darwin' else 1024),'performance':scopes(log)}
    with (out/('warmups.jsonl' if warmup else 'measurements.jsonl')).open('a') as f:f.write(json.dumps(row)+'\n')
    print(label,round(row['cpu_s'],3),'CPU seconds',flush=True)

for v in binaries:run(cases['arena128-2'],v,f'warmup-{v}',warmup=True)
for block in plan['blocks']:
    if a.case and block['case']!=a.case:continue
    for v in block['order']:run(cases[block['case']],v,f"{block['case']}-r{block['repeat']:02d}-{v}",repeat=block['repeat'])
