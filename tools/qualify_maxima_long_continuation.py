#!/usr/bin/env python3
"""Reproduce archived failures under a new freeze; never count them as samples."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import maxima_win_experiment as exp
from qualify_maxima_win_experiment import execute


def qualify(out, inventory, cpus, reference=None, fresh_jobs=()):
    protocol = json.loads((out / 'protocol.json').read_text())
    exp.verify_freeze(protocol)
    if not cpus or not set(cpus) <= os.sched_getaffinity(0):
        raise ValueError('explicit permitted CPUs required')
    cases = [dict(row, original_host=host) for host, rows in
             json.loads(inventory.read_text()).items() for row in rows]
    if not cases: raise ValueError('no archived failure cases')
    directory = out / 'long-continuation'; directory.mkdir(exist_ok=False)

    def assigned(slot):
        cpu, rows = slot
        os.sched_setaffinity(0, {cpu})  # Only this Linux worker thread and its children.
        results = []
        for row in rows:
            target = directory / row['job']
            cap = row['checkpoint'] + 512
            reuse = reference is not None and row['job'] not in fresh_jobs
            full_dir = reference / row['job'] / 'full' if reuse else target / 'full'
            if reuse:
                full = json.loads((full_dir / 'receipt.json').read_text())
                save = full_dir / 'checkpoint.game'
                if exp.sha(full_dir / 'audit.jsonl') != full['audit_sha256']:
                    raise exp.IntegrityError('archived reference audit changed')
            else:
                full, save = execute(protocol, row['scenario'], row['settings'], full_dir,
                                     cap, save_tick=row['checkpoint'])
            loaded, _ = execute(protocol, row['scenario'], row['settings'], target / 'loaded',
                                cap, checkpoint=(save, row['checkpoint']))
            def orders(path):
                with path.open() as stream:
                    return [{k:v for k,v in r.items() if k != 'sequence'} for line in stream
                            if (r := json.loads(line))['type'] in ('order_issued', 'order_dispatched')
                            and r['tick'] >= row['checkpoint']]
            with (full_dir / 'audit.jsonl').open() as stream:
                boundary = next(json.loads(line) for line in stream if json.loads(line)['type'] == 'checkpoint')
            checks = {
                'boundary_equal': exp.deterministic_signature(boundary) == exp.deterministic_signature(loaded['start']),
                'terminal_equal': exp.deterministic_signature(full['terminal']) == exp.deterministic_signature(loaded['terminal']),
                'orders_equal': orders(full_dir / 'audit.jsonl') == orders(target / 'loaded/audit.jsonl')}
            result = {'original_job': row['job'], 'original_host': row['original_host'],
                      'checkpoint': row['checkpoint'], 'cpu': cpu, **checks,
                      'reference_audit_sha256': full['audit_sha256'],
                      'reference_command_sha256': exp.sha(full_dir / 'command.json'),
                      'checkpoint_sha256': exp.sha(save),
                      'archived_reference': str(full_dir) if reuse else None}
            exp.atomic(target / 'comparison.json', result)
            if not all(checks.values()):
                exp.atomic(out / 'STOP_DISPATCH.json', result)
                raise exp.IntegrityError('late continuation mismatch: ' + row['job'])
            print(json.dumps(result), flush=True); results.append(result)
        return results

    try:
        with ThreadPoolExecutor(max_workers=len(cpus)) as pool:
            results = sum(pool.map(assigned, [(cpu, cases[i::len(cpus)])
                          for i, cpu in enumerate(cpus)]), [])
        exp.atomic(directory / 'PASS.json', {'protocol_id': protocol['protocol_id'],
            'inventory_sha256': exp.sha(inventory), 'script_sha256': exp.sha(__file__),
            'cases': results, 'purpose': 'regression evidence only; never inferential samples'})
    except Exception as error:
        exp.atomic(out / 'STOP_DISPATCH.json', {'reason': str(error), 'stage': 'long continuation qualification'})
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign', type=Path)
    parser.add_argument('inventory', type=Path)
    parser.add_argument('--cpus', required=True)
    parser.add_argument('--reference', type=Path, help='Archived uninterrupted reference fixtures; never inferential samples')
    parser.add_argument('--fresh-job', action='append', default=[], help='Regenerate a fixture requiring the new save format')
    args = parser.parse_args()
    qualify(args.campaign.resolve(), args.inventory.resolve(), [int(x) for x in args.cpus.split(',')],
            args.reference.resolve() if args.reference else None, args.fresh_job)
