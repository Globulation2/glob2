"""Diagnostic attribution only; these clock-heavy samples cannot qualify performance."""
import argparse
import json
from pathlib import Path


def analyze(result, diagnostics):
    start, end = diagnostics['threads_at_start'], diagnostics['threads_at_end']
    gs, ge = result['benchmark_gradient_at_start'], result['benchmark_gradient_at_end']
    rows, unmatched = [], {'started': [], 'exited': []}
    if start.get('available') and end.get('available'):
        before = {(t['tid'], t['start_ticks']): t for t in start['threads']}
        after = {(t['tid'], t['start_ticks']): t for t in end['threads']}
        workers = {v for k, v in ge.items() if k.startswith('compute_worker_tid_')}
        for key in sorted(before.keys() & after.keys()):
            a, b = before[key], after[key]
            tid = b['tid']
            role = 'owner' if tid == end['owner_tid'] else 'coordinator' if tid == ge.get('coordinator_tid') else 'compute_worker' if tid in workers else 'other_unclassified'
            user, system = b['user_cpu_ns'] - a['user_cpu_ns'], b['system_cpu_ns'] - a['system_cpu_ns']
            if min(user, system) < 0:
                raise ValueError('thread CPU counter decreased')
            rows.append(dict(tid=tid, name=b['name'], role=role, cpu_ns=user + system,
                             user_cpu_ns=user, system_cpu_ns=system, allowed_cpus=b.get('allowed_cpus')))
        unmatched['started'] = [after[k] for k in sorted(after.keys() - before.keys())]
        unmatched['exited'] = [before[k] for k in sorted(before.keys() - after.keys())]
    scopes = []
    initial_scopes = {s['scope']: s for s in diagnostics['owner_scopes_at_start']}
    for s in diagnostics['owner_scopes_at_end']:
        a = initial_scopes[s['scope']]
        delta = {k: s[k] - a[k] for k in ('inclusive_cpu_ns', 'self_cpu_ns', 'cpu_samples')}
        if any(v < 0 for v in delta.values()):
            raise ValueError('scope CPU counter decreased')
        if delta['cpu_samples']:
            scopes.append(dict(scope=s['scope'], **delta, self_complete=a['self_complete'] and s['self_complete']))
    ticks = diagnostics['ticks']
    if len(ticks) != result['benchmark_measured_ticks']:
        raise ValueError('diagnostic tick coverage differs from measured window')
    tails = sorted(ticks, key=lambda t: t['wall_ns'], reverse=True)[:max(1, (len(ticks)+99)//100)]
    totals = {k: sum(t[k] for t in tails) for k in ('wall_ns', 'publication_wait_ns', 'gpu_publication_wait_ns', 'gpu_backend_overlap_wait_ns', 'building_wait_ns')}
    return dict(diagnostic_only=True, acceptance_eligible=False,
                total_process_cpu_ns=result['benchmark_run_cpu_ns'],
                known_counter_deltas={k: ge[k]-gs.get(k, 0) for k in ge if k.endswith('_cpu_ns')},
                matched_thread_cpu_ns=sum(t['cpu_ns'] for t in rows),
                process_minus_matched_threads_ns=result['benchmark_run_cpu_ns']-sum(t['cpu_ns'] for t in rows),
                threads=sorted(rows, key=lambda t: t['cpu_ns'], reverse=True), unmatched_threads=unmatched,
                proc_coverage=dict(start_available=start.get('available'), end_available=end.get('available'),
                                   start_failures=start.get('failed_threads'), end_failures=end.get('failed_threads'),
                                   clock_ticks_per_second=end.get('clock_ticks_per_second')),
                owner_scopes=scopes,
                slowest_one_percent=dict(ticks=len(tails), totals=totals,
                    publication_majority_ticks=sum(2*t['publication_wait_ns'] >= t['wall_ns'] for t in tails),
                    gpu_publication_majority_ticks=sum(2*t['gpu_publication_wait_ns'] >= t['wall_ns'] for t in tails),
                    largest_examples=tails[:10]),
                note='Thread quantization and boundary scan time limit reconciliation; unmatched threads remain unallocated. Inclusive scopes overlap and must not be summed. Backend overlap includes host driver work and waits, not physical GPU kernel duration.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    reports = {}
    for path in sorted(args.runs.rglob('benchmark-diagnostics.json')):
        reports[str(path.parent.relative_to(args.runs))] = analyze(
            json.loads((path.parent/'result.json').read_text()), json.loads(path.read_text()))
    if not reports:
        raise ValueError('no diagnostic reports found')
    args.output.write_text(json.dumps(reports, indent=2)+'\n')


if __name__ == '__main__':
    main()
