#!/usr/bin/env python3
import pathlib,json,statistics,collections
r=pathlib.Path(__file__).resolve().parent;cases={c['id']:c for c in json.loads((r/'cases.json').read_text())};rows=json.loads((r/'timing/results.json').read_text());groups=collections.defaultdict(lambda:collections.defaultdict(list))
assert len(rows)==40
for x in rows:
 assert not x.get('compiler_interference'),x
 if x['repeat']>=0:groups[x['case']][x['variant']].append(x)
assert len(groups)==5
labels={'even12':'Mixed AIs, 512×512','held-islands8':'Castor/Numbi, 256×256','held-islands12-late':'Late Maxima/Cortex, 512×256','cortex12':'12 Cortex, islands, 512×256','cortex-continents12':'12 Cortex, continents, 512×256'}
lines=['# Final combined performance','', 'Baseline ef1f90ed1 → final b8b328771. Therig, release GCC build, one gradient worker with delay eight, pinned to CPUs 8 and 10. Each cell is a median of three measured runs after a discarded warmup. Positive reductions mean faster. Ranges are sample minima/maxima, not confidence intervals.','', '| Workload | Ticks | Baseline simulation, s (range) | Candidate simulation, s (range) | Reduction | Whole-process before → after, s | Process reduction |','|---|---:|---:|---:|---:|---:|---:|'];summary={}
for name,vs in groups.items():
 assert set(vs)=={'baseline','candidate'} and all(len(v)==3 for v in vs.values())
 d={label:{key:{'median':statistics.median(x[key] for x in values),'min':min(x[key] for x in values),'max':max(x[key] for x in values)} for key in ['run_s','wall_s','cpu_s']} for label,values in vs.items()};d['reductions_percent']={key:100*(1-d['candidate'][key]['median']/d['baseline'][key]['median']) for key in ['run_s','wall_s','cpu_s']};d['ticks']=cases[name]['timing_ticks'];summary[name]=d
 b=d['baseline'];c=d['candidate'];g=d['reductions_percent']
 def cell(v):return f"{v['median']:.3f} ({v['min']:.3f}–{v['max']:.3f})"
 lines.append(f"| {labels[name]} | {d['ticks']:,} | {cell(b['run_s'])} | {cell(c['run_s'])} | {g['run_s']:.2f}% | {b['wall_s']['median']:.3f} → {c['wall_s']['median']:.3f} | {g['wall_s']:.2f}% |")
lines+=['','Simulation timing excludes startup/save loading and includes gradient-worker drain. Whole-process CPU time and all raw samples are retained in performance.json and timing/. Final timings waited for compiler activity to finish; detected overlapping samples are excluded and retried. Results are workload-specific and do not imply a universal speedup.']
(r/'performance.json').write_text(json.dumps(summary,indent=2));(r/'performance.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
