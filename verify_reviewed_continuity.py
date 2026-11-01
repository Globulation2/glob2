"""Focused diagnostic: 32 MiB/attempt, 128 MiB total, 1 GiB free, 600 s/run."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT),str(ROOT/'test')]
from tools.tournaments.storage import CompressedLog, directory_bytes
from tournament_cli_integration import tick_records

OUT=Path(__file__).resolve().parent/'reviewed-continuity-v190'
OUT.mkdir(exist_ok=False)
BINARY=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
MAP=OUT/'map/map-r0.map.gz'
MIB=1024**2
runs=[]

def run_command(name,command):
    target=OUT/name
    if shutil.disk_usage(OUT).free < 1024*MIB: raise RuntimeError('free-space limit')
    command=[str(BINARY),*command,'--output-dir',str(target)]
    proc=subprocess.Popen(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
    capture=CompressedLog(proc.stdout,OUT/(name+'.log.gz'),MIB)
    start=time.monotonic();failure=None
    while proc.poll() is None:
        if (time.monotonic()-start > 600 or directory_bytes(target)>32*MIB or
            directory_bytes(OUT)>128*MIB or shutil.disk_usage(OUT).free<1024*MIB or capture.error):
            failure='runtime/storage limit';os.killpg(proc.pid,signal.SIGKILL);break
        time.sleep(.25)
    code=proc.wait();log=capture.finish()
    record=dict(name=name,command=command,exit_code=code,seconds=time.monotonic()-start,log=log,
                bytes=directory_bytes(target),failure=failure)
    runs.append(record)
    (OUT/'runs.json').write_text(json.dumps(runs,indent=2)+'\n')
    if code or failure or log['error'] or not log['complete']: raise RuntimeError(record)
    result=json.loads((target/'result.json').read_text())
    assert result['status']=='completed'
    return result,target

def run(name,args):
    result,target=run_command(name,['game','run',*args,'--ticks','1024','--compute-threads','1',
        '--telemetry','checksums','--save','final'])
    assert result['ticks']==1024
    assert result['resolved']['rules']['winProbabilityPermille']==950
    return result,target

generated,_=run_command('map',['map','study','15','--seed','42','--set','teams=2',
    '--write-map','--rotations','2'])
assert MAP.exists()
original,original_dir=run('original',['--map-file',str(MAP),'--player','cortex','--player','cortex',
    '--game-seed','19','--ai-param','0:swarmWorkerCap=4','--ai-param','1:swarmWorkerCap=7',
    '--ai-param','0:expandDebounceCycles=3','--ai-param','1:expandDebounceCycles=5',
    '--save','initial','--save','every:512'])
restored,restored_dir=run('restored',['--load-game',str(original_dir/'checkpoint-512.game.gz')])
original_ticks=tick_records(original_dir/'game.replay.checksums')
restored_ticks=tick_records(restored_dir/'game.replay.checksums')
assert len(original_ticks)==1024 and len(restored_ticks)==512
assert all(original_ticks[t]==record for t,record in restored_ticks.items())
for before,after in zip(original['teams'],restored['teams']):
    assert before['win_probability_history']==after['win_probability_history']
    assert before['win_probability']==after['win_probability']
    history=before['win_probability_history'];p=before['win_probability']
    assert history[0][0]==0 and history[-1][0]==1024
    area=sum((b[0]-a[0])*(a[1]+b[1]) for a,b in zip(history,history[1:]))
    assert p['area_twice_permille_ticks']==area
    assert abs(p['mean']-area/(2000*1024))<=.0000005
report=dict(status='passed',ticks=1024,resumed_ticks=512,teams=len(original['teams']),
    map_sha256=hashlib.sha256(MAP.read_bytes()).hexdigest(),
    binary_sha256=hashlib.sha256(BINARY.read_bytes()).hexdigest(),runs=runs)
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='runs'}))
