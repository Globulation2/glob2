#!/usr/bin/env python3
"""Compare worker packages on identical real games; retain results and commands."""
import argparse
import json
from pathlib import Path
import sys
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package-root',required=True,help='repository root containing tools/tournaments')
    p.add_argument('--bundle',required=True);p.add_argument('--map',required=True)
    p.add_argument('--output',required=True);p.add_argument('--hosts')
    p.add_argument('--jobs',type=int,default=24);p.add_argument('--slots',type=int,default=4)
    p.add_argument('--ticks',type=int,default=20000)
    args=p.parse_args()
    sys.path.insert(0,str(Path(args.package_root).resolve()))
    from tools.tournaments.bundles import inspect_bundle
    from tools.tournaments.common import atomic_json,file_hash,store_artifact
    from tools.tournaments.coordinator import Coordinator
    from tools.tournaments.model import job
    from tools.tournaments.results import Results
    from tools.tournaments.transport import Transport
    from tools.tournaments.transfer import send_file
    root=Path(args.output).resolve();bundle=inspect_bundle(args.bundle)
    if root.exists():raise ValueError('use a fresh output directory')
    source=store_artifact(args.map,root/'artifacts')
    jobs=[job('game',bundle['id'],inputs={'map':source},seeds={'game':23000+i},
              config={'players':['maxima','nicowar'],'ticks':args.ticks},
              outputs={'replay':True,'saves':['initial','final']}) for i in range(args.jobs)]
    c=Coordinator.submit(root,{'schema_version':1,'id':'pipeline-benchmark','jobs':jobs,
                               'settings':{'prefetch':1,'heartbeat_seconds':15,'lease_seconds':300}},[args.bundle])
    hosts=json.loads(Path(args.hosts).read_text()) if args.hosts else [
        {'name':'localhost','transport':'local','directory':str(root/'worker'),
         'slots':args.slots,'collect_slots':2,'input_slots':2,'result_slots':2}]
    atomic_json(root/'hosts.json',hosts)
    transports=[Transport(h,root/'worker.pyz') for h in hosts]
    try:
        archive=c.archive_bundle(bundle['id']);identity=file_hash(archive)
        for t in transports:
            t.rpc('status')
            while not send_file(t,archive,identity):pass
            t.rpc('install',identity=identity,bundle_id=bundle['id'])
            while not send_file(t,root/'artifacts'/source['sha256'],source['sha256']):pass
        started=time.time();status=c.run(hosts);finished=time.time()
        rows=list(Results(root));assert len(rows)==args.jobs and all(r['category']=='success' for r in rows)
        wall=finished-started;slots=sum(h['slots'] for h in hosts)
        report={'package_id':c.manifest['package_id'],'command':sys.argv,'started':started,
                'jobs':len(rows),'slots':slots,'wall_seconds':wall,'games_per_minute':60*len(rows)/wall,
                'execution_slot_occupancy':sum(r['seconds'] for r in rows)/(wall*slots),
                'engine_cpu_utilization':sum(sum(r.get('resource_usage',{}).get(k,0) for k in ('user_seconds','system_seconds')) for r in rows)/(wall*slots),
                'raw_bytes':sum(a['raw_bytes'] for r in rows for a in r['artifacts']),
                'transfer_bytes':sum(a['bytes'] for r in rows for a in r['artifacts']),
                'outcomes':[{'seed':r['job']['seeds']['game'],'result':r['result'],
                             'artifacts':{a['path']:a['raw_sha256'] for a in r['artifacts'] if a['path'] in ('initial.game','final.game','game.replay')}} for r in rows],
                'status':status}
        atomic_json(root/'benchmark.json',report)
        print(json.dumps({k:v for k,v in report.items() if k not in ('outcomes','status')},indent=2))
    finally:
        for t in transports:
            t.rpc('stop')
            if hasattr(t,'close'):t.close()
        c.close()


if __name__=='__main__':main()
