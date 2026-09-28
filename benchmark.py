import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import shlex
import time

ROOT=Path.cwd()

def perf(log):
    result = {}
    for line in log.read_text().splitlines():
        if line.startswith('GLOB2_PERF_FINAL '):
            values = dict(token.split('=', 1) for token in shlex.split(line)[1:] if '=' in token)
            if 'scope' in values:
                result[values['scope']] = values
    return result

parser=argparse.ArgumentParser()
parser.add_argument('--binary',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--cases',type=Path,default=Path(__file__).with_name('cases.json'))
parser.add_argument('--fixtures',type=Path,required=True)
args=parser.parse_args()
binary=args.binary.resolve(); output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
cases=json.loads(args.cases.read_text());records=[]
env=os.environ.copy()
for key in ['GLOB2_GRADIENT_DEMAND_CSV','GLOB2_LAZY_BUILDING_GRADIENTS']:env.pop(key,None)
env['SDL_VIDEODRIVER']='dummy';env['SDL_AUDIODRIVER']='dummy'
binary_hash=hashlib.sha256(binary.read_bytes()).hexdigest()
for case in cases:
    map_path=args.fixtures.resolve()/case['name']/'map-r0.map'
    expected=None
    for index,lazy in enumerate([False,True,True,False]):
        label='lazy' if lazy else 'eager';name=f"{case['name']}-{index}-{label}"
        dest=output/name
        if dest.exists():raise RuntimeError(f'Refusing to overwrite {dest}')
        log=output/f'{name}.log'
        cmd=[str(binary),'--run-game','--map-file',str(map_path),'--game-seed',str(case['game_seed']),
             '--ticks',str(case['ticks']),'--telemetry','team-timeline','--lazy-building-gradients',str(lazy).lower(),
             '--output-dir',str(dest)]
        for player in case['players']:cmd+=['--player',player]
        print(f'Running {name}',flush=True)
        start=time.monotonic()
        with log.open('w') as stream:
            process=subprocess.Popen(cmd,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT)
            pid,status,usage=os.wait4(process.pid,0)
            process.returncode=os.waitstatus_to_exitcode(status)
        if process.returncode:raise RuntimeError(f'{name} failed: {process.returncode}')
        entry=dict(case=case['name'],index=index,lazy=lazy,command=cmd,binary_sha256=binary_hash,
                   platform=platform.platform(),wall_s=time.monotonic()-start,user_s=usage.ru_utime,
                   system_s=usage.ru_stime,maxrss_bytes=usage.ru_maxrss if sys.platform=='darwin' else usage.ru_maxrss*1024,
                   performance=perf(log),result=json.loads((dest/'result.json').read_text()))
        if expected is None: expected=entry['result']
        for key in ['ticks','termination','teams']:
            assert entry['result'][key]==expected[key],(name,key)
        if lazy:assert int(entry['performance']['gradient.building_resume']['calls'])>0
        records.append(entry)
        (output/'measurements.json').write_text(json.dumps(records,indent=2)+'\n')
        print(f"Completed {name}: cpu={entry['user_s']+entry['system_s']:.3f}s work={int(entry['performance']['loop.work']['total_ns'])/1e9:.3f}s peak_rss={entry['maxrss_bytes']/1024**2:.1f}MiB",flush=True)
print('All timings and deterministic result checks passed.',flush=True)
