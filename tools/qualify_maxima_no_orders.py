#!/usr/bin/env python3
"""Paired full-AI-off qualification across the benchmark and all 2v2 sides."""
import argparse
import json
from pathlib import Path
import maxima_win_experiment as exp
from qualify_maxima_win_experiment import execute


def events(path, tick=0):
    with path.open() as stream:
        return [{k:v for k,v in r.items() if k!='sequence'} for line in stream
                if (r:=json.loads(line))['type'] in ('order_issued','order_dispatched') and r['tick']>=tick]


def qualify(protocol, output):
    exp.verify_freeze(protocol)
    output.mkdir(parents=True,exist_ok=False)
    rows=[]
    for opponent in exp.OPPONENTS:
        for fmt,count in exp.FORMATS.items():
            for arrangement in range(6 if fmt=='2v2' else 1):
                s=exp.scenario(protocol,'qualification',6000+len(rows))
                s.update(format=fmt,opponent=opponent,map=protocol['maps'][fmt][0],
                         partition=arrangement%3,swap=arrangement//3,seat=0,offset=1)
                side={0,s['partition']+1}
                if s['swap']: side=set(range(4))-side
                s['players']=[{'player':i,'team':i if fmt=='2v2' else (1+i*s['map']['teams']//count)%s['map']['teams'],
                               'focal':i in side if fmt=='2v2' else i==0} for i in range(count)]
                s['scenario_id']=exp.identity({k:v for k,v in s.items() if k!='scenario_id'})
                on=exp.arm_settings(protocol,s)
                off={player:dict(values) for player,values in on.items()}
                focal={p['player'] for p in s['players'] if p['focal']}
                for player in focal: off[str(player)][exp.NO_ORDERS]=True
                directory=output/f'case-{len(rows):02d}'
                normal,_=execute(protocol,s,on,directory/'normal',5000)
                disabled,save=execute(protocol,s,off,directory/'disabled',5000,save_tick=2137)
                loaded,_=execute(protocol,s,off,directory/'loaded',5000,checkpoint=(save,2137))
                normal_events=events(directory/'normal/audit.jsonl')
                disabled_events=events(directory/'disabled/audit.jsonl')
                # Receipts already reject all focal orders/decisions in the off arm.
                assert any(r['player'] in focal for r in normal_events), 'inactive baseline fixture'
                assert any(r['player'] not in focal for r in disabled_events), 'inactive opponents'
                assert not any(r['player'] in focal for r in disabled_events)
                for player in focal:
                    start=next(p for p in disabled['start']['players'] if p['player']==player)
                    terminal=next(p for p in disabled['terminal']['players'] if p['player']==player)
                    assert start['serialized_ai_checksum']==terminal['serialized_ai_checksum'], 'disabled AI ticked'
                with (directory/'disabled/audit.jsonl').open() as stream:
                    boundary=next(r for line in stream if (r:=json.loads(line))['type']=='checkpoint')
                assert exp.deterministic_signature(boundary)==exp.deterministic_signature(loaded['start'])
                assert exp.deterministic_signature(disabled['terminal'])==exp.deterministic_signature(loaded['terminal'])
                assert events(directory/'disabled/audit.jsonl',2137)==events(directory/'loaded/audit.jsonl',2137)
                rows.append({'format':fmt,'opponent':opponent,'arrangement':arrangement,'focal':sorted(focal),
                             'focal_orders':0,'baseline_focal_orders':sum(r['player'] in focal for r in normal_events),
                             'opponent_orders':len(disabled_events),'boundary_equal':True,'terminal_equal':True,'orders_equal':True,
                             'receipts':{k:v['audit_sha256'] for k,v in [('normal',normal),('disabled',disabled),('loaded',loaded)]}})
                exp.atomic(output/'progress.json',rows)
    exp.verify_freeze(protocol)
    exp.atomic(output/'PASS.json',{'protocol_id':protocol['protocol_id'],'passed':True,'cases':rows,
        'control_settings':{exp.NO_ORDERS:True},'scope':'no AI ticks or non-null orders from any focal player; opponents active; world continues',
        'script_sha256':exp.sha(__file__)})
    print('No-orders control: 40 paired routing and continuation cases PASS',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    qualify(json.loads((args.campaign/'protocol.json').read_text()),args.output)
