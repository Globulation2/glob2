#!/usr/bin/env python3
"""Run bounded native routing and continuation audits before any full-game dispatch.

A failing determinism/routing check writes a fleet stop artifact immediately.
No switch is marked behaviorally validated by configuration-only fixtures.
"""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import time

import maxima_win_experiment as exp


def execute(protocol,scenario,settings,directory,cap,checkpoint=None,save_tick=None):
    directory.mkdir(parents=True,exist_ok=False)
    audit=directory/'audit.jsonl'; save=directory/'checkpoint.game'
    args,envelope=exp.command(protocol,scenario,settings,cap,audit,checkpoint)
    if save_tick is not None: args+=['--maxima-checkpoint-save',str(save.resolve()),str(save_tick)]
    exp.atomic(directory/'command.json',{'argv':args,'identities':envelope})
    started=time.monotonic()
    with (directory/'engine.log').open('w') as log:
        result=subprocess.run(args,cwd=exp.ROOT,env=exp.clean_environment(),stdout=log,stderr=subprocess.STDOUT)
    if result.returncode: raise exp.IntegrityError(f'engine exit {result.returncode}: {directory}')
    receipt=exp.read_audit(audit,envelope,scenario,settings)
    receipt['seconds']=time.monotonic()-started
    exp.atomic(directory/'receipt.json',receipt)
    return receipt,save


def extended_continuation(protocol,out):
    """Exercise a later, non-round checkpoint before qualifying other AI types."""
    records=[]
    for opponent in exp.OPPONENTS:
        scenario=exp.scenario(protocol,'qualification',2000+opponent)
        scenario.update(format='duel',opponent=opponent,map=protocol['maps']['duel'][0],seat=0,offset=1)
        teams=scenario['map']['teams']
        scenario['players']=[{'player':i,'team':(1+i*teams//2)%teams,'focal':i==0} for i in range(2)]
        scenario['scenario_id']=exp.identity({k:v for k,v in scenario.items() if k!='scenario_id'})
        settings=exp.arm_settings(protocol,scenario)
        directory=out/f'extended-{opponent}'
        full,save=execute(protocol,scenario,settings,directory/'full',5000,save_tick=2137)
        loaded,_=execute(protocol,scenario,settings,directory/'loaded',5000,checkpoint=(save,2137))
        raw=[json.loads(line) for line in (directory/'full/audit.jsonl').read_text().splitlines()]
        def orders(path):
            with path.open() as stream:
                return [{k:v for k,v in row.items() if k!='sequence'}
                        for line in stream if (row:=json.loads(line))['type'] in ('order_issued','order_dispatched')
                        and row['tick']>=2137]
        boundary=next(record for record in raw if record['type']=='checkpoint')
        checks={
            'boundary_equal':exp.deterministic_signature(boundary)==exp.deterministic_signature(loaded['start']),
            'terminal_equal':exp.deterministic_signature(full['terminal'])==exp.deterministic_signature(loaded['terminal']),
            'world_equal':full['terminal']['world_checksum']==loaded['terminal']['world_checksum'],
            'rng_equal':full['terminal']['rng']==loaded['terminal']['rng'],
            'orders_equal':orders(directory/'full/audit.jsonl')==orders(directory/'loaded/audit.jsonl')}
        records.append({'opponent':opponent,**checks})
        exp.atomic(out/'extended-determinism.json',records)
        if not all(checks.values()):
            raise exp.IntegrityError(f'extended continuation failed for opponent {opponent}: '+exp.canonical(checks))


def qualify(protocol,out):
    exp.verify_freeze(protocol)
    records=[]
    try:
        # Weaker opponents first, every 2v2 partition and both sides.
        for opponent in exp.OPPONENTS:
            for fmt in exp.FORMATS:
                for arrangement in range(6 if fmt=='2v2' else 1):
                    s=exp.scenario(protocol,'qualification',len(records))
                    s.update(format=fmt,opponent=opponent)
                    s['map']=protocol['maps'][fmt][0]
                    s.update(partition=arrangement%3,swap=arrangement//3,seat=0,offset=1)
                    count=exp.FORMATS[fmt]
                    if fmt=='2v2':
                        side={0,s['partition']+1}; side=set(range(4))-side if s['swap'] else side
                        s['players']=[{'player':i,'team':i,'focal':i in side} for i in range(4)]
                    else:
                        s['players']=[{'player':i,'team':(1+i*s['map']['teams']//count)%s['map']['teams'],
                                       'focal':i==0} for i in range(count)]
                    s['scenario_id']=exp.identity({k:v for k,v in s.items() if k!='scenario_id'})
                    settings=exp.arm_settings(protocol,s,'farming.enabled',False)
                    receipt,_=execute(protocol,s,settings,out/f'routing-{len(records):03d}',10)
                    records.append({'opponent':opponent,'format':fmt,'arrangement':arrangement,
                                    'status':'passed','receipt':receipt['audit_sha256']})
        exp.atomic(out/'routing.json',records)
        # Same world, RNG and AI state: two fresh starts and a split continuation.
        s=exp.scenario(protocol,'qualification',1000)
        s.update(format='duel',opponent=1,map=protocol['maps']['duel'][0],seat=0,offset=1)
        teams=s['map']['teams']
        s['players']=[{'player':i,'team':(1+i*teams//2)%teams,'focal':i==0} for i in range(2)]
        s['scenario_id']=exp.identity({k:v for k,v in s.items() if k!='scenario_id'})
        settings=exp.arm_settings(protocol,s)
        full,save=execute(protocol,s,settings,out/'uninterrupted',1000,save_tick=500)
        repeat,_=execute(protocol,s,settings,out/'identical',1000)
        if exp.deterministic_signature(full['terminal'])!=exp.deterministic_signature(repeat['terminal']):
            raise exp.IntegrityError('identical fresh starts diverged')
        loaded,_=execute(protocol,s,settings,out/'continuation',1000,checkpoint=(save,500))
        raw=[json.loads(line) for line in (out/'uninterrupted/audit.jsonl').read_text().splitlines()]
        boundary=next(r for r in raw if r['type']=='checkpoint')
        comparisons={'restored_boundary_equal':exp.deterministic_signature(boundary)==exp.deterministic_signature(loaded['start']),
                     'continued_terminal_equal':exp.deterministic_signature(full['terminal'])==exp.deterministic_signature(loaded['terminal']),
                     'world_equal':full['terminal']['world_checksum']==loaded['terminal']['world_checksum'],
                     'rng_equal':full['terminal']['rng']==loaded['terminal']['rng']}
        exp.atomic(out/'determinism.json',comparisons)
        if not all(comparisons.values()):
            raise exp.IntegrityError('save/load determinism failure: '+exp.canonical(comparisons))
        extended_continuation(protocol,out)
        exp.atomic(out/'qualification-progress.json',{'status':'behavioral_and_cross_host_fixtures_required',
            'protocol_id':protocol['protocol_id'],'routing_cases':len(records),
            'no_full_game_dispatch':True})
    except Exception as error:
        exp.atomic(out/'routing.json',records)
        exp.atomic(out/'STOP_DISPATCH.json',{'reason':str(error),'protocol_id':protocol['protocol_id'],
                  'status':'investigate; controls/pilot/confirmation blocked','active_engines':'preserve'})
        raise


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign',type=Path)
    args=parser.parse_args()
    qualify(json.loads((args.campaign/'protocol.json').read_text()),args.campaign.resolve())
