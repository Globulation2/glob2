#!/usr/bin/env python3
"""Frozen, local-only one-Maxima-versus-three-Nicowar FourSquares FFA audit."""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import time
import crash_capture
import optimize_maxima_portfolio as portfolio

ROOT = Path(__file__).resolve().parents[1]
KEEP = {'siege_candidate_evaluated','director_snapshot','recon_snapshot','strategy_loaded','posture_changed',
        'attack_finished','dig_out_started','reactive_defense_updated',
        'explorer_strike_launched','recon_suspended','recon_resumed',
        'colony_swarm_selected','colony_swarm_completed','colony_swarm_operating',
        'colony_swarm_failed'}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=ROOT/'build/src/glob2')
    parser.add_argument('--maps', nargs='+', default=['FourSquares1'])
    parser.add_argument('--players', type=int, default=0, help='Active players; 0 uses every map start')
    parser.add_argument('--position-offsets', type=int, nargs='+', default=[0], help='Rotate the occupied starting positions')
    parser.add_argument('--candidate-ai', type=int, default=7)
    parser.add_argument('--seat-count', type=int, default=0, help='0 rotates every seat; 1 avoids duplicate all-Nicowar games')
    parser.add_argument('--seeds', type=int, default=24)
    parser.add_argument('--seed', type=int, default=2026090524)
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--max-steps', type=int, default=180000)
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()
    if min(args.seeds,args.jobs,args.max_steps,args.timeout)<1: parser.error('limits must be positive')
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    frozen=out/'snapshot';metadata=out/'manifest.json'
    env=os.environ.copy()
    for key in ('GLOB2_MAXIMA_OVERRIDES','GLOB2_MAXIMA_TUNING','GLOB2_NICOWAR_V3_OVERRIDES','GLOB2_NICOWAR_V3_TUNING'):
        env.pop(key,None)
    if not metadata.exists():
        frozen.mkdir(exist_ok=True)
        shutil.copy2(args.binary,frozen/'glob2')
        shutil.copytree(ROOT/'data',frozen/'data',dirs_exist_ok=True)
        available={Path(m['file']).stem:m for m in portfolio.discover_maps(args.binary.resolve(),ROOT)}
        selected=[available[name] for name in args.maps]
        for m in selected: shutil.copy2(ROOT/m['file'],frozen/Path(m['file']).name)
        (frozen/'source').mkdir(exist_ok=True)
        for path in [*(ROOT/'src').glob('AIMaxima*'), ROOT/'src/Engine.cpp', ROOT/'src/Engine.h']:
            shutil.copy2(path,frozen/'source'/path.name)
        resolved=subprocess.run([str(frozen/'glob2'),'--dump-maxima-strategy','--maxima-format','ffa4'],cwd=frozen,env=env,capture_output=True,text=True,check=True)
        payload=json.loads(resolved.stdout)
        (frozen/'resolved.json').write_text(json.dumps(payload,indent=2)+'\n')
        (frozen/'resolved.strategy').write_text(''.join(f"{v['key']} = {str(v['value']).lower()}\n" for v in payload['parameters']))
        rng=random.Random(args.seed);seeds=rng.sample(range(1,2**32),args.seeds)
        schedule=[]
        for map_index,m in enumerate(selected):
            teams=args.players or m['teams']
            if not 2<=teams<=min(5,m['teams']): parser.error('Scenario requires 2–5 active players, within the map capacity; use --players for larger maps')
            if any(offset<0 or offset>=m['teams'] for offset in args.position_offsets): parser.error('Position offset outside map capacity')
            for block,seed in enumerate(seeds):
                for offset in args.position_offsets:
                    for seat in range(min(teams,args.seat_count) if args.seat_count else teams):
                        schedule.append({'id':len(schedule)+1,'block':map_index*args.seeds+block+1,'round':block+1,'format':'duel' if teams==2 else 'ffa'+str(teams),'kind':'scenario','map':m['name'],'map_file':str(frozen/Path(m['file']).name),'map_teams':m['teams'],'seed':seed,'players_requested':teams,'candidate_ai':args.candidate_ai,'opponent_ai':5,'candidate_seat':seat,'position_offset':offset,'max_steps':args.max_steps,'telemetry':True})
        manifest={'created_utc':datetime.now(timezone.utc).isoformat(),'design':'candidate versus unallied Nicowars; all seats per map and seed','seed_generator':args.seed,'independent_seed_blocks':args.seeds,'matches':len(schedule),'jobs':args.jobs,'wall_timeout_seconds':args.timeout,'max_game_ticks':args.max_steps,'hashes':{str(p.relative_to(frozen)):digest(p) for p in frozen.rglob('*') if p.is_file()},'schedule':schedule}
        metadata.write_text(json.dumps(manifest,indent=2)+'\n')
    manifest=json.loads(metadata.read_text())
    for rel,sha in manifest['hashes'].items():
        if digest(frozen/rel)!=sha:raise RuntimeError('Frozen input changed: '+rel)
    for name in ('logs','match-telemetry','matches'): (out/name).mkdir(exist_ok=True)
    def run(match):
        stem=f"match-{match['id']:05d}";dest=out/'matches'/(stem+'.json')
        if dest.exists():return json.loads(dest.read_text())
        command=portfolio.command_for(match,str(frozen/'glob2'))
        command+=['--maxima-base',str(frozen/'resolved.strategy'),'--maxima-layer',str(frozen/'resolved.strategy'),'--maxima-format','ffa4']
        raw=out/'logs'/(stem+'.log')
        capture=crash_capture.run_process(command,cwd=frozen,timeout=manifest['wall_timeout_seconds'],environment=env,crash_root=out/'crashes',label=stem,live_output_path=raw)
        result=portfolio.parse_scenario_output(match,capture.output,capture.returncode)
        result.update(wall_seconds=capture.wall_seconds,command=command,worker_host='local',timed_out=capture.timed_out,crash=capture.crash)
        result.pop('output',None)
        result['telemetry']=[e for e in result['telemetry'] if e['event'] in KEEP or e['event'].startswith('mission_')]
        with gzip.open(out/'match-telemetry'/(stem+'.json.gz'),'wt',encoding='utf-8',compresslevel=3) as h:json.dump(result,h,separators=(',',':'))
        for key in ('telemetry','observer','score_telemetry'):result.pop(key,None)
        with raw.open('rb') as src,gzip.open(raw.with_suffix('.log.gz'),'wb',compresslevel=3) as dst:shutil.copyfileobj(src,dst)
        raw.unlink()
        dest.write_text(json.dumps(result,indent=2)+'\n')
        return result
    results=[];started=time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures=[pool.submit(run,m) for m in manifest['schedule']]
        for future in as_completed(futures):
            r=future.result();results.append(r)
            candidate=next((p for p in r['players'] if p['role']=='candidate'),{})
            outcome='win' if candidate.get('won') else 'loss' if candidate.get('lost') else 'unresolved'
            print(f"{len(results)}/{len(futures)} match={r['id']} seed={r['seed']} seat={r['candidate_seat']} {outcome} engine={r['engine_status']} steps={r['steps']} wall={r['wall_seconds']:.1f}s",flush=True)
            (out/'progress.json').write_text(json.dumps({'completed':len(results),'total':len(futures),'elapsed_seconds':round(time.monotonic()-started,1),'failed':sum(x['status']!='completed' for x in results)},indent=2)+'\n')
    (out/'results.json').write_text(json.dumps({'manifest':'manifest.json','matches':sorted(results,key=lambda r:r['id'])},indent=2)+'\n')
    print('COMPLETE '+str(out),flush=True)

if __name__=='__main__':main()
