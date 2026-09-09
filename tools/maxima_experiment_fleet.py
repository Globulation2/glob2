#!/usr/bin/env python3
"""Commission and supervise persistent per-host experiment queues over SSH.

Engine processes belong to host workers. Coordinator probes and result transfers
have independent lifetimes; losing the coordinator expires dispatch authority.
"""
import argparse
import gzip
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import sys
import time

import maxima_win_experiment as exp
import maxima_experiment_queue as queue


def ssh():
    config = os.environ.get('MAXIMA_FLEET_SSH_CONFIG')
    return ['ssh'] + (['-F', config] if config else [])


def topology(host):
    raw = subprocess.check_output(['lscpu', '-p=CPU,CORE,SOCKET,ONLINE'], text=True)
    return queue.affinity(raw, exp.HOSTS[host], os.sched_getaffinity(0))


def pulse(out, host):
    q = queue.Queue(out / 'queue.sqlite')
    allocation = topology(host)
    q.health(host, True, allocation)
    jobs = [dict(r) for r in q.db.execute('SELECT id,status,error FROM jobs')]
    stopped = out / 'STOP_DISPATCH.json'
    for job in q.db.execute("SELECT id,pid FROM jobs WHERE status='running'"):
        command = Path(f'/proc/{job["pid"]}/cmdline')
        if not command.exists() or b'maxima_experiment_queue.py' not in command.read_bytes():
            exp.atomic(stopped, {'reason': 'worker vanished with an uncertain running job',
                'job': job['id'], 'active_engines': 'preserve and investigate; never automatically retry'})
    import shutil
    return {'host': host, 'time': time.time(), 'allocation': allocation, 'jobs': jobs,
            'summary': q.summary(), 'free_bytes': shutil.disk_usage(out).free,
            'stop': json.loads(stopped.read_text()) if stopped.exists() else None}


def start(out, host, cohort):
    p = json.loads((out / 'protocol.json').read_text())
    exp.verify_freeze(p)
    manifest = json.loads((out / 'manifest.json').read_text())
    if manifest['protocol_id'] != p['protocol_id']:
        raise exp.IntegrityError('manifest belongs to another freeze')
    exp.require_gates(out, p, manifest['stage'])
    allocation = topology(host)
    assignments = json.loads((out / 'assignments.json').read_text())[host][cohort]
    by_id = {j['execution_id']: j for j in manifest['jobs']}
    q = queue.Queue(out / 'queue.sqlite')
    q.health(host, True, allocation)
    for key in assignments:
        q.enqueue(by_id[key])
    workers = out / 'workers'; workers.mkdir(exist_ok=True)
    for cpu in allocation['simulation_cpus']:
        record = workers / f'{cpu}.json'
        if record.exists():
            pid = json.loads(record.read_text())['pid']
            command = Path(f'/proc/{pid}/cmdline')
            if command.exists() and b'maxima_experiment_queue.py' in command.read_bytes():
                continue
        with (workers / f'{cpu}.log').open('a') as log:
            child = subprocess.Popen([sys.executable, str(exp.ROOT / 'tools/maxima_experiment_queue.py'),
                '--output', str(out), '--host', host, '--cpu', str(cpu)],
                cwd=exp.ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True)
        exp.atomic(record, {'pid': child.pid, 'cpu': cpu, 'started': time.time()})
    return {'host': host, 'enqueued': len(assignments), 'workers': allocation['jobs']}


def assign(manifest, allocations):
    result = {h: {'weak': [], 'strong': []} for h in allocations}
    slots = [h for h, a in allocations.items() for _ in range(a['jobs'])]
    rng = random.Random(manifest['protocol_id'] + manifest['stage'])
    for cohort in ('weak', 'strong'):
        jobs = [j for j in manifest['jobs'] if
                ('weak' if j['scenario']['opponent'] in (1, 2) else 'strong') == cohort]
        rng.shuffle(jobs); rng.shuffle(slots)
        for i, job in enumerate(jobs):
            result[slots[i % len(slots)]][cohort].append(job['execution_id'])
    return result


def remote(host, root, out, mode, cohort=None, value=None):
    args = ['python3', str(root / 'tools/maxima_experiment_fleet.py'), mode,
            '--output', str(out), '--host', host]
    if cohort: args += ['--cohort', cohort]
    # Only probe/configuration commands run in this bounded SSH client, never engines.
    result = subprocess.run([*ssh(), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', host,
        shlex.join(args)], input=json.dumps(value) if value is not None else None,
        capture_output=True, text=True, timeout=20)
    if result.returncode: raise RuntimeError(result.stderr[-2000:])
    return json.loads(result.stdout)


def propagate_stop(local, root, out, reason):
    value = {'reason': reason, 'time': time.time(), 'active_engines': 'preserve'}
    exp.atomic(local / 'STOP_DISPATCH.json', value)
    def stop(host):
        try: remote(host, root, out, 'stop', value=value)
        except Exception: pass  # Unreachable workers lose their 90-second lease.
    with ThreadPoolExecutor(max_workers=5) as pool:
        list(pool.map(stop, exp.HOSTS))


def launch_pilot(local, root, out, protocol, controls):
    import shutil
    import maxima_win_report as report
    target = local.parent / (local.name + '-pilot')
    target.mkdir(exist_ok=True)
    remote_out = out.parent / (out.name + '-pilot')
    matrix = json.loads((local / 'audit-matrix.json').read_text())
    validated = [row['switch'] for row in matrix if row.get('validated')]
    if not validated: raise RuntimeError('no behaviorally validated switches for pilot')
    qualification = json.loads((local / 'qualification.json').read_text())
    for gate in qualification.values():
        if isinstance(gate, dict) and gate.get('path'):
            dest = target / gate['path']; dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(local / gate['path'], dest)
    exp.atomic(target / 'controls-acceptance.json', controls)
    qualification['positive_control'] = {'status': 'passed', 'path': 'controls-acceptance.json',
        'sha256': exp.sha(target / 'controls-acceptance.json')}
    manifest = report.comparisons(protocol, 'pilot', {key: 100 for key in validated}, validated)
    allocations = json.loads((local / 'allocations.json').read_text())
    for name, value in [('protocol.json', protocol), ('qualification.json', qualification),
                        ('audit-matrix.json', matrix), ('manifest.json', manifest),
                        ('allocations.json', allocations), ('assignments.json', assign(manifest, allocations))]:
        exp.atomic(target / name, value)
    # Only immutable metadata/evidence is copied. Persistent queues and runs are never overwritten.
    files = [str(path.relative_to(target)) for path in target.rglob('*') if path.is_file()
             and path.suffix == '.json' and 'results' not in path.parts]
    for host in exp.HOSTS:
        subprocess.run([*ssh(), '-o', 'BatchMode=yes', host, shlex.join(['mkdir', '-p', str(remote_out)])],
                       check=True, timeout=20)
        subprocess.run(['rsync', '-az', '-e', shlex.join(ssh()), '--files-from=-', str(target) + '/', host + ':' + str(remote_out) + '/'],
                       input='\n'.join(files)+'\n', text=True, check=True, timeout=45)
        remote(host, root, remote_out, 'start', 'weak')
    return target, remote_out


def supervise(local, root, out, pilot_after_controls=False):
    import maxima_win_report as report
    p = json.loads((local / 'protocol.json').read_text())
    manifest = json.loads((local / 'manifest.json').read_text())
    assignments = json.loads((local / 'assignments.json').read_text())
    state_path = local / 'supervisor.json'
    state = json.loads(state_path.read_text()) if state_path.exists() else {
        'cohort': 'weak', 'started': time.time(), 'transfer_seconds': 0.0}
    results = {}
    for path in (local / 'results').glob('*/result.json*'):
        if path.name not in ('result.json', 'result.json.gz'): continue
        with (gzip.open(path, 'rt') if path.suffix == '.gz' else path.open()) as stream:
            value = json.load(stream)
        results[value['execution_id']] = value
    def probe(host):
        try: return host, remote(host, root, out, 'pulse'), None
        except Exception as error: return host, None, str(error)
    while not (local / 'STOP_DISPATCH.json').exists():
        cycle = time.monotonic()
        with ThreadPoolExecutor(max_workers=5) as pool:
            probes = list(pool.map(probe, exp.HOSTS))
        failures = {}; summaries = {}
        for host, value, error in probes:
            if error:
                failures[host] = error; continue
            summaries[host] = value
            if value['stop'] or any(j['status'] == 'needs_investigation' for j in value['jobs']):
                propagate_stop(local, root, out, {'host': host, 'evidence': value})
                return
            missing = [j['id'] for j in value['jobs'] if j['status'] == 'complete' and j['id'] not in results][:32]
            if missing:
                # Transfer retries cannot enqueue or rerun any simulation.
                target = local / 'results'; target.mkdir(exist_ok=True)
                files = ''.join(key + '/result.json\n' for key in missing)
                try:
                    transfer_started = time.monotonic()
                    transfer = subprocess.run(['rsync', '-az', '-e', shlex.join(ssh()), '--files-from=-',
                        host + ':' + str(out / 'runs') + '/', str(target) + '/'],
                        input=files, text=True, capture_output=True, timeout=45)
                    state['transfer_seconds'] = state.get('transfer_seconds', 0) + time.monotonic() - transfer_started
                    if transfer.returncode: failures[host + ':transfer'] = transfer.stderr[-1000:]
                    for key in missing:
                        path = target / key / 'result.json'
                        if path.exists():
                            raw = path.read_bytes(); results[key] = json.loads(raw)
                            compressed = path.with_suffix('.json.gz')
                            pending = compressed.with_suffix('.gz.pending')
                            with gzip.open(pending, 'wb') as stream: stream.write(raw)
                            pending.replace(compressed); path.unlink()
                except subprocess.TimeoutExpired:
                    failures[host + ':transfer'] = 'transfer timed out; retry independently'
        try:
            report.assemble(manifest, results)  # Includes deterministic repeat checks.
        except Exception as error:
            propagate_stop(local, root, out, str(error)); return
        completed = sum(row['on'] in results and row['off'] in results for row in manifest['comparisons'])
        elapsed = time.time() - state['started']
        status = {'time': time.time(), 'stage': manifest['stage'], 'cohort': state['cohort'],
                  'completed_pairs': completed, 'scheduled_pairs': len(manifest['comparisons']),
                  'completed_executions': len(results), 'scheduled_executions': len(manifest['jobs']),
                  'pairs_per_hour': completed * 3600 / elapsed if elapsed else 0,
                  'unresolved_executions': sum(r['outcome'] is None for r in results.values()),
                  'hosts': {h: v['summary'] for h, v in summaries.items()}, 'failures': failures}
        status['eta_seconds'] = ((len(manifest['comparisons']) - completed) * elapsed / completed) if completed else None
        exp.atomic(local / 'STATUS.json', status)
        exp.atomic(state_path, state)
        weak = {key for a in assignments.values() for key in a['weak']}
        if state['cohort'] == 'weak' and weak <= results.keys():
            started = True
            for host in exp.HOSTS:
                try: remote(host, root, out, 'start', 'strong')
                except Exception: started = False
            if started:
                state['cohort'] = 'strong'; exp.atomic(state_path, state)
        if all(j['execution_id'] in results for j in manifest['jobs']):
            if manifest['stage'] == 'controls':
                value = report.controls_checkpoint(p, manifest, results, True)
                exp.atomic(local / 'controls-budget-checkpoint.json', value)
            elif manifest['stage'] == 'pilot':
                exp.atomic(local / 'pilot-report.json', report.report(p, manifest, results))
                allocation = json.loads((local / 'allocations.json').read_text())
                exp.atomic(local / 'pilot-budget-checkpoint.json', report.pilot_budget(p, manifest, results,
                    state.get('transfer_seconds', 0) / max(1, len(results)),
                    sum(a['jobs'] for a in allocation.values())))
            elif manifest['stage'] in ('confirmation', 'combination'):
                report.publish_final(local / 'CONFIRMATION_RESULT.json', p, manifest, results)
            else:
                raise ValueError('unsupported completed experiment stage')
            status['status'] = 'stage_complete'; exp.atomic(local / 'STATUS.json', status)
            if manifest['stage'] == 'controls' and pilot_after_controls and value['accepted']:
                pilot_local, pilot_remote = launch_pilot(local, root, out, p, value)
                status['next_stage'] = 'pilot'; status['pilot_output'] = str(pilot_local)
                exp.atomic(local / 'STATUS.json', status)
                results.clear()
                supervise(pilot_local, root, pilot_remote)
            return
        time.sleep(max(0, 30 - (time.monotonic() - cycle)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['pulse', 'start', 'stop', 'supervise'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host', choices=list(exp.HOSTS))
    parser.add_argument('--cohort', choices=['weak', 'strong'], default='weak')
    parser.add_argument('--remote-root', type=Path)
    parser.add_argument('--remote-output', type=Path)
    parser.add_argument('--pilot-after-controls', action='store_true')
    args = parser.parse_args()
    if args.mode == 'supervise':
        supervise(args.output.resolve(), args.remote_root, args.remote_output, args.pilot_after_controls)
    elif args.mode == 'stop':
        exp.atomic(args.output / 'STOP_DISPATCH.json', json.load(sys.stdin)); print('{}')
    else:
        print(json.dumps(pulse(args.output, args.host) if args.mode == 'pulse'
                         else start(args.output, args.host, args.cohort)))
