import json, math, statistics
from pathlib import Path

OUT=Path(__file__).resolve().parent
rows=[r for r in json.loads((OUT/'runs.json').read_text()) if 'pair' in r]
if len(rows)!=384: raise RuntimeError(f'Incomplete timed run inventory: {len(rows)}/384')
if any(r['result'].get('gpu_requested',0) or r['result'].get('gradient_gpu_fields',0) for r in rows):
    raise RuntimeError('CPU-only receipt failed')
indexed={(r['fixture'],r['count'],r['pair'],r['revision']):r for r in rows}
def interval(values):
    if len(values)!=8: raise ValueError('Protocol requires eight pairs')
    mean=statistics.mean(values)
    half=2.364624251*statistics.stdev(values)/math.sqrt(8)
    return dict(mean=mean,ci95=[mean-half,mean+half])
metrics={
    'cpu_per_tick_ms':lambda r:r['result']['benchmark_run_cpu_ns']/r['result']['benchmark_measured_ticks']/1e6,
    'startup_inclusive_cpu_seconds':lambda r:r['process_cpu_seconds'],
    'setup_cpu_seconds':lambda r:r['result']['benchmark_setup_cpu_ns']/1e9,
    'run_wall_seconds':lambda r:r['result']['run_ns']/1e9,
    'tick_p95_ms':lambda r:r['result'].get('tick_p95_ns',0)/1e6,
    'periodic_seed_cpu_us_per_field':lambda r:r['result']['gradient_execution']['required_seed_cpu_ns']/r['result']['gradient_execution']['cpu_complete_fields']/1e3,
    'periodic_solve_cpu_us_per_field':lambda r:r['result']['gradient_execution']['required_propagation_cpu_ns']/r['result']['gradient_execution']['cpu_complete_fields']/1e3,
}
summaries=[]
for baseline in ('before','control'):
    cells=[]
    cohort={key:[[] for _ in range(8)] for key in metrics}
    for fixture in sorted({r['fixture'] for r in rows}):
        for count in (1,2,4,8):
            cell=dict(fixture=fixture,count=count,metrics={})
            for metric,access in metrics.items():
                before=[access(indexed[fixture,count,p,baseline]) for p in range(8)]
                after=[access(indexed[fixture,count,p,'after']) for p in range(8)]
                if any(v<=0 for v in before): continue
                changes=[100*(a/b-1) for a,b in zip(after,before)]
                cell['metrics'][metric]=dict(before_mean=statistics.mean(before),after_mean=statistics.mean(after),change_percent=interval(changes))
                for pair,change in enumerate(changes):cohort[metric][pair].append(change)
            cells.append(cell)
    groups={}
    for fixture in sorted({r['fixture'] for r in rows}):
        group={}
        for metric,access in metrics.items():
            before=[access(indexed[fixture,c,p,baseline]) for c in (1,2,4,8) for p in range(8)]
            after=[access(indexed[fixture,c,p,'after']) for c in (1,2,4,8) for p in range(8)]
            if any(v<=0 for v in before): continue
            changes=[statistics.mean(100*(access(indexed[fixture,c,p,'after'])/access(indexed[fixture,c,p,baseline])-1) for c in (1,2,4,8)) for p in range(8)]
            group[metric]=dict(before_mean=statistics.mean(before),after_mean=statistics.mean(after),change_percent=interval(changes))
        groups[fixture]=group
    summaries.append(dict(baseline=baseline,candidate='after',cells=cells,fixtures=groups,cohort={key:interval([statistics.mean(pair) for pair in values]) for key,values in cohort.items() if all(values)}))
(OUT/'summary.json').write_text(json.dumps(summaries,indent=2)+'\n')
lines=['# CPU backend before/after measurements','', 'Positive changes mean more CPU/time. Linux x86_64 AMD Ryzen 7 6800H, release GCC13 -O3; CPU automatic; four frozen fixtures; counts 1/2/4/8; eight alternating triples per cell; 256 warmup + 2048 measured ticks. Process CPU includes all threads and final drain. Other workloads active: load observations ranged from 6.3 to 41.5. Wall times especially are contention-sensitive. This is a headless CPU comparison, not graphical or cross-platform qualification.','', 'Before = original 8862b0930. Control = pre-merge master 2d5cf67e8. After = final tested PR head 3f4a275d6 (tree merged as c7d688e9c). Control/after isolates feature changes from intervening master updates.','', '48 untimed runs matched per-tick checksums across revisions and counts. All 384 timed runs matched final checksums and recorded CPU-only execution. No production code was edited for measurement. Protocol, binary hashes, runtime library inventories, fixtures, exact commands, results and logs are retained beside this report.','', 'The main CPU window excludes setup and warmup, includes final drain, and uses process CPU time across all threads. Seed/solve counters below cover the full 2304-tick simulation, including warmup, and are normalized by completed periodic fields. They exclude building/AI traversals and are supplementary diagnostics; they do not establish a causal breakdown of the total regression.','']
for summary in summaries:
    lines.extend([f'## {summary["baseline"]} versus after','', '| Metric | Cohort change | Paired 95% CI |','|---|---:|---|'])
    for name,c in summary['cohort'].items():
        lines.append(f'| {name} | {c["mean"]:+.2f}% | [{c["ci95"][0]:+.2f}, {c["ci95"][1]:+.2f}]% |')
    lines.extend(['', 'Fixture aggregates give each compute count equal weight; confidence intervals use eight pair-level averages.','', '| Fixture | Before CPU ms/tick | After CPU ms/tick | Change | Paired 95% CI |','|---|---:|---:|---:|---|'])
    for name,metrics in summary['fixtures'].items():
        m=metrics['cpu_per_tick_ms'];c=m['change_percent']
        lines.append(f'| {name} | {m["before_mean"]:.4f} | {m["after_mean"]:.4f} | {c["mean"]:+.2f}% | [{c["ci95"][0]:+.2f}, {c["ci95"][1]:+.2f}]% |')
    lines.extend(['', 'Individual compute counts:','', '| Fixture | Compute count | Before CPU ms/tick | After CPU ms/tick | Change | Paired 95% CI |','|---|---:|---:|---:|---:|---|'])
    for cell in summary['cells']:
        m=cell['metrics']['cpu_per_tick_ms']; c=m['change_percent']
        lines.append(f'| {cell["fixture"]} | {cell["count"]} | {m["before_mean"]:.4f} | {m["after_mean"]:.4f} | {c["mean"]:+.2f}% | [{c["ci95"][0]:+.2f}, {c["ci95"][1]:+.2f}]% |')
    lines.append('')
(OUT/'report.md').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
