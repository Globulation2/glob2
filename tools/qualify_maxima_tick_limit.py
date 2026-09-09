#!/usr/bin/env python3
"""Verify exact 200k termination and outcome handling through the real worker."""
import argparse,json,os
from pathlib import Path
import maxima_win_experiment as e
import maxima_experiment_queue as q


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign',type=Path);parser.add_argument('--known-long-case',type=Path,required=True);parser.add_argument('--cpu',type=int,required=True)
    a=parser.parse_args();p=json.loads((a.campaign/'protocol.json').read_text());e.verify_freeze(p)
    assert p['hard_tick_limit']==200000 and e.CHECKPOINTS==(200000,) and p['checkpoint_harvest_interval']==200000
    out=a.campaign/'tick-limit';out.mkdir(exist_ok=False)
    rows=[]
    for i,disabled in enumerate((False,True)):
        s=e.scenario(p,'qualification',1000)
        s.update(format='duel',opponent=6,map=p['maps']['duel'][0],seat=0,offset=1)
        count=s['map']['teams'];s['players']=[{'player':j,'team':(1+j*count//2)%count,'focal':j==0} for j in range(2)]
        s['scenario_id']=e.identity({k:v for k,v in s.items() if k!='scenario_id'})
        settings=e.arm_settings(p,s)
        if disabled:settings['0'][e.NO_ORDERS]=True
        payload={'scenario':s,'settings':settings,'execution_id':e.identity({'s':s,'settings':settings})}
        result=q.run_job(p,payload,out/str(i),a.cpu)
        assert result['tick']<=200000
        if result['outcome'] is None:
            assert result['tick']==200000
            assert (out/str(i)/'checkpoints/checkpoint-200000.game').is_file()
        assert len(list((out/str(i)).glob('*.command.json')))==1
        rows.append({'disabled':disabled,'tick':result['tick'],'outcome':result['outcome'],'audit_sha256':result['receipts'][-1]['audit_sha256']})
    payload=json.loads(a.known_long_case.read_text())
    result=q.run_job(p,payload,out/'known-long',a.cpu)
    assert result['tick']==200000 and result['natural_outcome'] is None
    assert result['decision_reason'].startswith('population_') and result['outcome'] in (0,1)
    assert (out/'known-long/checkpoints/checkpoint-200000.game').is_file()
    rows.append({'known_long_qualification_only':True,'tick':result['tick'],'outcome':result['outcome'],'decision_reason':result['decision_reason'],'population_by_side':result['population_by_side'],'audit_sha256':result['receipts'][-1]['audit_sha256']})
    e.verify_freeze(p);e.atomic(out/'PASS.json',{'protocol_id':p['protocol_id'],'passed':True,'cases':rows,'limit':200000,'single_engine_per_case':True,'unfinished':'population_v1','script_sha256':e.sha(__file__)})
    print('200000-tick native worker cap: PASS',flush=True)

if __name__=='__main__':main()
