#!/usr/bin/env python3
import collections,json,pathlib,statistics
root=pathlib.Path(__file__).resolve().parent
rows=json.loads((root/'timing/results.json').read_text());groups=collections.defaultdict(lambda:collections.defaultdict(list))
for row in rows:
 if row['repeat']>=0:groups[row['case']][row['variant']].append(row)
summary={};lines=['# Final performance measurements','', 'Exactly 1,000 simulation ticks per run; one discarded warmup and three measured repetitions per variant. Baseline `508942f08`, final candidate `ef1f90ed1`. Positive reduction means faster. These small samples do not establish a confidence interval.','', '| Workload | Baseline simulation median (range), s | Candidate simulation median (range), s | Reduction | Baseline process wall, s | Candidate process wall, s | Process reduction |','|---|---:|---:|---:|---:|---:|---:|']
for name,variants in groups.items():
 assert all(len(v)==3 for v in variants.values())
 result={v:{metric:{'median':statistics.median(r[metric] for r in values),'min':min(r[metric] for r in values),'max':max(r[metric] for r in values)} for metric in ['run_s','wall_s','cpu_s']} for v,values in variants.items()}
 result['reductions_percent']={metric:100*(1-result['candidate'][metric]['median']/result['baseline'][metric]['median']) for metric in ['run_s','wall_s','cpu_s']};summary[name]=result
 b=result['baseline'];c=result['candidate'];gain=result['reductions_percent']
 def cell(r):return f"{r['median']:.3f} ({r['min']:.3f}–{r['max']:.3f})"
 lines.append(f"| {name} | {cell(b['run_s'])} | {cell(c['run_s'])} | {gain['run_s']:.2f}% | {b['wall_s']['median']:.3f} | {c['wall_s']['median']:.3f} | {gain['wall_s']:.2f}% |")
lines+=['','`held-islands8` contains no Maxima players and is the noise control. Full CPU-time statistics are in `performance.json`; every raw sample, including warmups, is in `timing/results.json`. The two late cases begin at tick 40,000 with 845 and 2,376 units. Map dimensions, rosters and seeds are in `scenarios.json`.']
(root/'performance.json').write_text(json.dumps(summary,indent=2));(root/'performance.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
