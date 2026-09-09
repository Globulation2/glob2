#!/usr/bin/env python3
"""Prove that adding exact population receipts leaves engine trajectories unchanged."""
import argparse
import copy
import json
import re
from pathlib import Path
import maxima_win_experiment as e
from qualify_maxima_win_experiment import execute


def legacy_signature(boundary):
    value=copy.deepcopy(e.deterministic_signature(boundary))
    for player in value['players']: player.pop('population',None)
    return value


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign',type=Path)
    parser.add_argument('--prior-protocol',type=Path,required=True)
    args=parser.parse_args()
    protocol=json.loads((args.campaign/'protocol.json').read_text())
    prior=json.loads(args.prior_protocol.read_text())
    e.verify_freeze(protocol)
    assert e.sha(prior['binary'])==prior['binary_sha256']
    out=args.campaign/'population-equivalence-corrected';out.mkdir(exist_ok=False)
    rows=[]
    for index in range(0,200,10):
        s=e.scenario(protocol,'controls',index,balanced=True)
        s.update(stage='qualification',seed=5*2**28+3000+index)
        s['scenario_id']=e.identity({k:v for k,v in s.items() if k!='scenario_id'})
        for disabled in (False,True):
            settings=e.arm_settings(protocol,s)
            if disabled:
                for p in s['players']:
                    if p['focal']:settings[str(p['player'])][e.NO_ORDERS]=True
            directory=out/f'{index}-{int(disabled)}'
            before,_=execute(prior,s,settings,directory/'prior',5000)
            after,_=execute(protocol,s,settings,directory/'current',5000)
            for boundary in ('start','terminal'):
                assert legacy_signature(before[boundary])==legacy_signature(after[boundary]), (index,disabled,boundary)
                assert all(type(p['population']) is int and p['population']>=0 for p in after[boundary]['players'])
            # Compare every decision and order, not just the terminal world hash.
            def events(path):
                with path.open() as stream:
                    records=[r for line in stream if (r:=json.loads(line))['type'] in ('decision','order_issued','order_dispatched')]
                for r in records:
                    if r['type']=='decision' and 'fields' in r:
                        r['fields']=re.sub(r'(?<=\t)microseconds=\d+', 'microseconds=<timing>', r['fields'])
                return records
            assert events(directory/'prior/audit.jsonl')==events(directory/'current/audit.jsonl')
            rows.append({'format':s['format'],'opponent':s['opponent'],'disabled':disabled,
                         'terminal':e.deterministic_signature(after['terminal']),
                         'prior_audit_sha256':before['audit_sha256'],'current_audit_sha256':after['audit_sha256']})
            e.atomic(out/'progress.json',rows)
    e.verify_freeze(protocol)
    e.atomic(out/'PASS.json',{'passed':True,'protocol_id':protocol['protocol_id'],
        'prior_protocol_id':prior['protocol_id'],'cases':rows,'script_sha256':e.sha(__file__),
        'scope':'40 matched before/after runs, all 20 cells and both control arms; only additive population receipts differ'})
    print('Population receipt engine equivalence: 40 cases PASS',flush=True)

if __name__=='__main__':main()
