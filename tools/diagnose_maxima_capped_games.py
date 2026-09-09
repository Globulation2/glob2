#!/usr/bin/env python3
"""Explore capped outcomes separately; never modify or pool tournament receipts."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import sqlite3
import maxima_win_experiment as exp
from qualify_maxima_win_experiment import execute


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cpus',required=True)
    args=parser.parse_args()
    campaign=args.campaign.resolve();out=args.output.resolve()
    protocol=json.loads((campaign/'protocol.json').read_text());exp.verify_freeze(protocol)
    cpus=[int(x) for x in args.cpus.split(',')]
    if len(set(cpus))!=len(cpus) or not set(cpus)<=os.sched_getaffinity(0):
        raise ValueError('unique permitted CPUs required')
    q=sqlite3.connect('file:'+str(campaign/'queue.sqlite')+'?mode=ro',uri=True);q.row_factory=sqlite3.Row
    busy={r[0] for r in q.execute("select cpu from jobs where status='running'")}
    if busy.intersection(cpus):raise ValueError('diagnostics must use idle simulation slots')
    selected=[];seen=set()
    for row in q.execute("select id,payload,result from jobs where status='complete' order by id"):
        payload=json.loads(row['payload']);result=json.loads(row['result']);s=payload['scenario'];cell=(s['format'],s['opponent'])
        if result['outcome'] is None and not payload.get('repeat') and cell not in seen:
            selected.append({'job':row['id'],'payload':payload,'terminal':result['receipts'][-1]['terminal']});seen.add(cell)
            if len(selected)==len(cpus):break
    q.close();out.mkdir(parents=True,exist_ok=False)
    exp.atomic(out/'plan.json',{'protocol_id':protocol['protocol_id'],'purpose':'diagnostic only; never inferential samples or changes to existing outcomes','selection':'first completed capped nonrepeat job by id per format/opponent, up to supplied CPU count','target_tick':1440000,'jobs':selected,'cpus':cpus})
    def run(item):
        cpu,row=item;os.sched_setaffinity(0,{cpu});p=row['payload'];s=p['scenario'];settings=p['settings'];target=out/row['job']
        checkpoint=campaign/'runs'/row['job']/'checkpoints/checkpoint-720000.game'
        try:
            short,_=execute(protocol,s,settings,target/'boundary-check',720512,checkpoint=(checkpoint,720000))
            if exp.deterministic_signature(short['start'])!=exp.deterministic_signature(row['terminal']):
                raise exp.IntegrityError('cap checkpoint boundary differs; diagnostic continuation refused')
            result,_=execute(protocol,s,settings,target/'extended',1440000,checkpoint=(checkpoint,720000))
            record={'job':row['job'],'format':s['format'],'opponent':s['opponent'],'cpu':cpu,'checkpoint_sha256':exp.sha(checkpoint),'boundary_equal':True,'diagnostic_outcome':result['outcome'],'terminal_tick':result['terminal']['tick'],'seconds':result['seconds'],'audit_sha256':result['audit_sha256'],'original_outcome_unchanged':True}
        except Exception as error:
            record={'job':row['job'],'cpu':cpu,'error':str(error),'original_outcome_unchanged':True}
        exp.atomic(target/'diagnostic-result.json',record);print(json.dumps(record),flush=True);return record
    with ThreadPoolExecutor(max_workers=len(cpus)) as pool:results=list(pool.map(run,zip(cpus,selected)))
    exp.atomic(out/'SUMMARY.json',{'protocol_id':protocol['protocol_id'],'script_sha256':exp.sha(__file__),'purpose':'diagnostic only; original outcomes unchanged','results':results})


if __name__=='__main__':main()
