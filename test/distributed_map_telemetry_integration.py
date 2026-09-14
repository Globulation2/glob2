#!/usr/bin/env python3
"""Opt-in real workers: complete telemetry roundtrip on every configured host.

hosts.json entries include bundle: absolute registered bundle directory. Each host
gets an independent experiment so a fast host cannot consume another's coverage.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.tournaments.analysis import reanalyze
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json, read_json
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job
from tools.tournaments.results import Results
from tools.tournaments.transport import Transport


def run_host(host, output):
    bundle=inspect_bundle(host['bundle'])
    assert bundle['capabilities']['map_report_version']==2
    jobs=[]
    for method in (15, 22):
        for seed in (17,18,19):
            jobs.append(job('generate_map',bundle['id'],config={'generator':method,
                'params':{'width':7,'height':7,'teams':4}},seeds={'map':seed}))
    jobs.append(job('generate_map',bundle['id'],config={'generator':15,
        'params':{'teams':0}},seeds={'map':20}))
    root=output/host['name']
    c=Coordinator.submit(root,{'schema_version':1,'id':'telemetry-'+host['name'],
        'jobs':jobs,'settings':{'heartbeat_seconds':1}},[host['bundle']])
    transport=Transport(host,root/'worker.pyz')
    try:
        c.run([host])
        records=list(Results(root))
        assert len(records)==len(jobs)
        counts=[]
        for record in records:
            with Results(root).open_artifact(record,'result.json') as stream:
                assert json.load(stream)==record['result'], 'artifact and committed payload differ'
            report=record['result']['map_report']
            trace=report['generation']['telemetry']
            assert trace['schema_version']==1 and trace['enabled'] and trace['records']
            assert not trace['dropped_records'] and not trace['invalid_values']
            assert report['report_type']==('map' if record['category']=='success' else 'generation_failure')
            counts.append(len(trace['records']))
        summary=reanalyze(root,draws=100,seed=19)['map_telemetry']
        assert len(summary['records'])==sum(counts)
        assert sum(g['maps'] for g in summary['groups'])==len(jobs)
        evidence={'host':host['name'],'bundle':bundle['id'],'jobs':len(jobs),
            'telemetry_records':sum(counts),'categories':[r['category'] for r in records],
            'roundtrip_equal':True,'status':c.status()}
        atomic_json(root/'reports/telemetry-verification.json',evidence)
        return evidence
    finally:
        c.close()
        transport.rpc('stop')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hosts',required=True);p.add_argument('--output',required=True)
    args=p.parse_args();output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=False)
    hosts=read_json(args.hosts)
    with ThreadPoolExecutor(max_workers=len(hosts)) as pool:
        evidence=list(pool.map(lambda host:run_host(host,output),hosts))
    atomic_json(output/'verification.json',evidence)
    print(json.dumps(evidence,indent=2))
if __name__=='__main__': main()
