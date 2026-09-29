#!/usr/bin/env python3
"""Real tournament artifact and offline telemetry roundtrip, on supplied hosts.

Each host entry needs an absolute registered bundle path. Runs all eight AIs,
retains saves/checksums, compares telemetry-on/off and save continuation, and
stops the workers after collection. No machines or bundles are provisioned here.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.analysis import reanalyze
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json, read_json
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job
from tools.tournaments.results import Results
from tools.tournaments.transport import Transport
from tournament_cli_integration import tick_records


def run_host(host, output):
    bundle = inspect_bundle(host['bundle'])
    assert bundle['capabilities']['performance_telemetry_version'] == 1
    build = bundle['id']
    generation = job('generate_map', build, config={'generator':15,
        'params':{'width':7,'height':7,'teams':2}}, seeds={'map':42}, outputs={'map':True})
    jobs = [generation]
    for players in (['numbi','castor'], ['warrush','econo'], ['nicowar','cortex'], ['maxima','cabino']):
        config = {'players':players, 'ticks':700}
        inputs = {'map':{'job':generation['id'],'artifact':'map-r0.map.gz'}}
        enabled = job('game', build, config=config, inputs=inputs, depends_on=[generation['id']],
                      seeds={'game':19}, outputs={'telemetry':['team-timeline','checksums'],
                      'saves':['initial','final','every:513']}, labels={'variant':'on'})
        disabled = job('game', build, config=config, inputs=inputs, depends_on=[generation['id']],
                       seeds={'game':19}, outputs={'telemetry':['checksums']}, labels={'variant':'off'})
        # Cortex now persists its complete decision state on master. Check a
        # deterministic double-reload for all AIs (historic Castor/Numbi limits).
        for variant in ('reload-a','reload-b'):
            jobs.append(job('game', build, config={'ticks':700},
                            inputs={'save':{'job':enabled['id'],'artifact':'checkpoint-513.game.gz'}},
                            depends_on=[enabled['id']], outputs={'telemetry':['team-timeline','checksums']},
                            labels={'variant':variant,'pair':enabled['id']}))
        jobs.extend([enabled, disabled])
    root = output/host['name']
    coordinator = Coordinator.submit(root, {'schema_version':1,'id':'game-telemetry-'+host['name'],
        'jobs':jobs,'settings':{'heartbeat_seconds':1}}, [host['bundle']])
    transport = Transport(host, root/'worker.pyz')
    try:
        coordinator.run([host])
        source = Results(root)
        records = list(source)
        assert len(records) == len(jobs) and all(r['category']=='success' for r in records)
        traces, rows, counts = {}, {}, Counter()
        for record in records:
            if record['job']['type'] != 'game': continue
            identity = record['job']['id']
            trace = root/(identity+'.checksums')
            with source.open_artifact(record, 'game.replay.checksums', 'rb') as stream:
                trace.write_bytes(stream.read())
            traces[identity] = tick_records(trace)
            rows[identity] = list(source.telemetry(record))
            if record['job']['labels']['variant'] == 'on':
                with source.open_artifact(record, 'stdout.log') as stream:
                    log = stream.read()
                assert log.index('final: wrote ') < log.index('GLOB2_PERF_FINAL'), 'final save missing from timing session'
            assert not any('error' in row for row in rows[identity])
            counts.update(row['record'] for row in rows[identity])
            if record['job']['labels']['variant'] != 'off':
                finals = [r for r in rows[identity] if r['record']=='GLOB2_AI_FINAL']
                assert len(finals)==2 and {r['values']['player'] for r in finals}=={0,1}
                assert all(r['values']['tick']==700 for r in finals)
                assert any(r['record']=='GLOB2_PERF_FINAL' for r in rows[identity])
                assert any(r['record']=='GLOB2_MEASURE' and r['values']['final']==1 and
                           r['values']['tick']==700 for r in rows[identity])
        for record in records:
            j = record['job']
            if j['labels'].get('variant')=='on':
                off = next(r['job'] for r in records if r['job']['labels'].get('variant')=='off' and r['job']['config']==j['config'])
                assert traces[j['id']]==traces[off['id']], 'export changes per-tick execution'
                reloads = [r['job'] for r in records if r['job']['labels'].get('pair')==j['id']]
                a,b = (r['id'] for r in reloads)
                assert traces[a]==traces[b], 'reloads disagree'
                def measurements(identity):
                    return [(r['record'],r['values']) for r in rows[identity] if r['family']!='performance']
                assert measurements(a)==measurements(b), 'reload telemetry disagrees'
        report = reanalyze(root, draws=0)
        assert report['game_telemetry']['records']==dict(counts)
        for status in report['game_telemetry']['jobs']:
            if status['requested']:
                assert not status['errors'] and not status['unavailable'] and not status['missing_final']
        evidence = {'host':host['name'], 'bundle':build, 'jobs':len(records), 'counts':dict(counts),
                    'export_checksums_equal':True, 'reload_checksums_and_measurements_equal':True,
                    'offline_record_counts_equal':True}
        atomic_json(root/'reports/telemetry-verification.json', evidence)
        return evidence
    finally:
        coordinator.close()
        transport.rpc('stop')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hosts', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    evidence = [run_host(host, output) for host in read_json(args.hosts)]
    atomic_json(output/'verification.json', evidence)
    print(json.dumps(evidence, indent=2))

if __name__ == '__main__': main()
