#!/usr/bin/env python3
"""Read retained run measurements; keep timing and diagnostics separate."""
import collections, csv, hashlib, json, pathlib, statistics
D = pathlib.Path(__file__).resolve().parent
P = json.loads((D / 'PLAN.json').read_text())
S = json.loads((D / 'STATUS.json').read_text())
rows = S['rows']
extension = D / 'extended-quality/STATUS.json'
E = json.loads(extension.read_text()) if extension.exists() else None
diagnostic_rows = E['rows'] if E is not None else [r for r in rows if r['kind'] == 'on']
quality_source = 'extended8192-steps-two1024-tick-sample-windows' if E is not None else 'initial2048-steps-only-first1024-tick-sample-window'
movement_keys = ['extra_cost', 'false_unreachable', 'false_reachable', 'invalid_goal',
                 'missed_move', 'escape_failure', 'stall', 'guard_balance']
planning_keys = ['false_unreachable', 'false_reachable']

def events(path, keys):
    totals = collections.Counter()
    families = {}
    if not path.exists(): return None
    with path.open() as f:
        for r in csv.DictReader(f):
            fam = families.setdefault(r['family'], collections.Counter())
            totals['queries'] += 1
            fam['queries'] += 1
            for k in keys:
                n = int(r[k]); totals[k] += n; fam[k] += n
    def finish(t):
        return dict(totals=dict(t), per_query={k: t[k] / t['queries'] if t['queries'] else None for k in keys})
    return dict(overall=finish(totals), families={k: finish(v) for k, v in families.items()})

diagnostics = []
for r in diagnostic_rows:
    if r['kind'] != 'on': continue
    out = pathlib.Path(r['output'])
    q = dict(group=r['group'], format=r['format'], arm=r['arm'],
             actual_ticks=r.get('actual_ticks'), full_horizon=r.get('full_horizon'),
             movement=events(out / 'gradient-quality.csv', movement_keys),
             planning=events(out / 'gradient-planning-quality.csv', planning_keys))
    p = out / 'gradient-work.csv.summary.csv'
    if p.exists():
        with p.open() as f: q['work_by_family_reason'] = list(csv.DictReader(f))
    diagnostics.append(q)

timing = []
for m in P['maps']:
    byarm = {}
    for a in P['arms']:
        rr = [r for r in rows if r['kind'] == 'off' and r['arm'] == a['id'] and r['group'] == m['group']]
        if not rr: continue
        v = dict(runs=len(rr), full_runs=sum(r.get('full_horizon', False) for r in rr),
                 simulation_cpu_ms_per_tick=[r.get('cpu_ms_per_tick') for r in rr],
                 process_cpu_ms_per_tick=[r.get('process_cpu_ms_per_tick') for r in rr],
                 total_cpu_seconds=sum(r['total_cpu_seconds'] for r in rr),
                 total_wall_seconds=sum(r['wall_seconds'] for r in rr),
                 actual_ticks=sum(r.get('actual_ticks', 0) for r in rr),
                 native_jobs=[r.get('gradient_jobs') for r in rr],
                 lazy_jobs=[r.get('building_gradient_jobs') for r in rr])
        values = [x for x in v['simulation_cpu_ms_per_tick'] if x is not None]
        v['mean_cpu_ms_per_tick'] = statistics.mean(values) if values else None
        v['median_cpu_ms_per_tick'] = statistics.median(values) if values else None
        v['observed_cpu_range'] = [min(values), max(values)] if values else None
        byarm[a['id']] = v
    timing.append(dict(group=m['group'], format=m['format'], generator=m['spec']['generator'], by_arm=byarm))

aggregate = {}
for a in P['arms']:
    arm = a['id']
    tt = [m['by_arm'][arm] for m in timing if arm in m['by_arm']]
    qq = [q for q in diagnostics if q['arm'] == arm]
    entry = dict(timing_maps=len(tt), diagnostic_maps=len(qq))
    if tt:
        entry.update(equal_map_mean_cpu_ms_per_tick=statistics.mean(t['mean_cpu_ms_per_tick'] for t in tt),
                     timing_runs=sum(t['runs'] for t in tt),
                     full_timing_runs=sum(t['full_runs'] for t in tt),
                     total_cpu_seconds=sum(t['total_cpu_seconds'] for t in tt),
                     total_wall_seconds=sum(t['total_wall_seconds'] for t in tt),
                     actual_ticks=sum(t['actual_ticks'] for t in tt))
    for kind, keys in [('movement', movement_keys), ('planning', planning_keys)]:
        vv = [q[kind]['overall'] for q in qq if q[kind]]
        if not vv: continue
        totals = {k: sum(v['totals'].get(k, 0) for v in vv) for k in ['queries'] + keys}
        entry[kind] = dict(totals=totals,
            pooled_per_query={k: totals[k] / totals['queries'] if totals['queries'] else None for k in keys},
            equal_map_per_query={k: statistics.mean(v['per_query'][k] for v in vv if v['per_query'][k] is not None) for k in keys})
    aggregate[arm] = entry

summary = dict(status=E['status'] if E is not None else S['status'], plan_sha256=hashlib.sha256((D / 'PLAN.json').read_bytes()).hexdigest(),
    finished_runs=len(rows) + (len(E['rows']) if E else 0), expected_runs=144 if E else 120,
    timing_status=S['status'], quality_source=quality_source,
    timing_by_map=timing, diagnostics=diagnostics,
    aggregate=aggregate, caveats=[P['sampling_limit'], P['primary_comparison'],
        'Timing repeats measure within-world timing variation, not independent games. Observed repeat ranges are not confidence intervals.',
        'Diagnostic worlds and query mixes diverge by policy. Show pooled and equal-map quality; keep planning and stalls separate.',
        'The observer samples1024ticks per4096. Initial2048-step runs repeated only the original window; final extended8192-step diagnostics sample two1024-tick windows. Quality is not observed on every timing tick.'])
(D / 'SUMMARY.json').write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n')
lines = ['# Mac threshold comparison', '', f'Status: {summary["status"]}; {summary["finished_runs"]}/{summary["expected_runs"]} executed runs.', '',
         'Same gen003 coefficients, H128; mature tick16384 saves. CPU is probe OFF; quality is separately sampled in probe ON runs.',
         'Quality source: ' + quality_source + '.', '',
         '| Policy | CPU ms/tick (equal maps) | CPU timing runs | Direction extra/query (equal maps) | Direction extra/query (pooled) | Stall/query (pooled) |',
         '|---|---:|---:|---:|---:|---:|']
def fmt(x): return '—' if x is None else f'{x:.4f}'
for a in P['arms']:
    v = aggregate[a['id']]; q = v.get('movement', {})
    lines.append('| ' + ' | '.join([a['id'], fmt(v.get('equal_map_mean_cpu_ms_per_tick')), str(v.get('timing_runs', 0)),
        fmt(q.get('equal_map_per_query', {}).get('extra_cost')), fmt(q.get('pooled_per_query', {}).get('extra_cost')),
        fmt(q.get('pooled_per_query', {}).get('stall'))]) + ' |')
lines += ['', 'Do not compare partial aggregates until every arm covers the same maps and repeats.', '', *summary['caveats'], '']
(D / 'READOUT.md').write_text('\n'.join(lines))
print(json.dumps(dict(status=summary['status'], finished_runs=summary['finished_runs'], arms={k: {
    'timing_runs':v.get('timing_runs', 0), 'cpu_ms_per_tick':v.get('equal_map_mean_cpu_ms_per_tick'),
    'diagnostic_maps':v['diagnostic_maps']} for k,v in aggregate.items()})))
