"""Compatible ai-benchmark.sh entry point backed by the shared local scheduler."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import tempfile

from .common import digest, store_artifact
from .local import execute_jobs, supplied_binary
from .model import job
from .fairness_statistics import wilson


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map',required=True);parser.add_argument('--matchup',required=True)
    parser.add_argument('--games',type=int,default=40);parser.add_argument('--swap-sides',action='store_true')
    parser.add_argument('--jobs',type=int,default=max(1,(os.cpu_count() or 1)-2))
    parser.add_argument('--seed-base',type=int,default=1);parser.add_argument('--bin',default='build/src/glob2')
    parser.add_argument('--out');parser.add_argument('--keep',action='store_true')
    parser.add_argument('--ticks',type=int,default=int(os.environ.get('GLOB2_TEST_MAX_TICKS','90000')))
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    output=Path(args.out).resolve() if args.out else Path(tempfile.mkdtemp(prefix='glob2-benchmark-'))
    output.mkdir(parents=True,exist_ok=True)
    bundle=supplied_binary(args.bin,root,output/'.bundles')
    directory=output/'execution'
    map_file=Path(args.map)
    if not map_file.is_file(): map_file=root/'maps'/(args.map+'.map')
    artifact=store_artifact(map_file,directory/'artifacts')
    players=args.matchup.split(',')
    if args.swap_sides and len(players)!=2: parser.error('--swap-sides requires two AIs')
    tuning={}
    if os.environ.get('GLOB2_CORTEX_TUNING'):
        for line in Path(os.environ['GLOB2_CORTEX_TUNING']).read_text().splitlines():
            line=line.split('#',1)[0].strip()
            if line:
                key,value=line.split();tuning[key]=int(value)
    jobs=[]
    for index in range(args.games):
        for reverse in range(2 if args.swap_sides else 1):
            roster=players[::-1] if reverse else players
            tag=f'{index:04d}'+('-rev' if reverse else '-fwd') if args.swap_sides else f'{index:04d}'
            jobs.append(job('game',bundle.name,inputs={'map':artifact},seeds={'game':args.seed_base+index},
                            config={'players':roster,'ticks':args.ticks,'ai_params':{str(i):tuning for i,ai in enumerate(roster) if ai=='cortex'}},
                            outputs={'replay':args.keep},labels={'tag':tag,'format':'1v1' if len(players)==2 else 'ffa','block':args.seed_base+index}))
    source=execute_jobs(directory,jobs,bundle,args.jobs)
    wins=Counter();completed=0;draws=0
    for record in source:
        tag=record['job']['labels']['tag']
        with source.open_artifact(record,'stdout.log') as stream:
            (output/f'game-{tag}.log').write_text(stream.read())
        result=record.get('result') or {}
        if record['category']!='success': continue
        completed+=1
        winners=result.get('winning_teams',[])
        if len(winners)==1: wins[record['job']['config']['players'][winners[0]]]+=1
        else: draws+=1
    print(f'{completed}/{len(jobs)} completed; capped/unresolved games are draws. Results: {directory}')
    for name,count in [*wins.items(),('draws',draws)]:
        interval=wilson(count,completed) if completed else (0,1)
        print(f'{name}: {count}/{completed}; 95% Wilson interval {interval[0]:.3f}..{interval[1]:.3f}')
    return 0 if completed else 1


if __name__=='__main__': raise SystemExit(main())
