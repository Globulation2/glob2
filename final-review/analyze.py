import csv,json,random,statistics
from pathlib import Path
BASE=Path.cwd()/'artifacts/final-master-comparison';OUT=BASE/'runs';experiment=json.loads((OUT/'experiment.json').read_text())
rows=[]
for w in experiment['workloads']:
    for seed in experiment['seeds']:
        for policy in ('master','branch'):
            directory=OUT/w['name']/f'seed-{seed}-{policy}'
            run=json.loads((directory/'run.json').read_text());data=json.loads((directory/'game/result.json').read_text())
            assert run['exit_code']==0 and not run['contention']
            n=data['benchmark_measured_ticks'];assert n==data['ticks']>0
            row={'map':w['name'],'seed':seed,'policy':policy,'ticks':n,'whole_cpu_us_tick':run['whole_cpu_seconds']*1e6/n,'simulation_cpu_us_tick':data['benchmark_run_cpu_ns']/n/1e3,'ticks_second':n*1e9/data['run_ns'],'tick_p50_ms':data['tick_p50_ns']/1e6,'tick_p95_ms':data['tick_p95_ns']/1e6,'tick_p99_ms':data['tick_p99_ns']/1e6,'peak_rss_mib':run['peak_rss_bytes']/2**20,'setup_cpu_ms':data['benchmark_setup_cpu_ns']/1e6}
            for key in ('building_gradient_jobs','building_gradient_synchronous','building_gradient_published','building_gradient_discarded','building_gradient_max_pending'):
                row[key]=data.get(key)
            rows.append(row)
with (OUT/'per-run.csv').open('w',newline='') as file:
    writer=csv.DictWriter(file,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
metrics=['whole_cpu_us_tick','simulation_cpu_us_tick','ticks_second','tick_p50_ms','tick_p95_ms','tick_p99_ms','peak_rss_mib']
rng=random.Random(1197);summary={}
for w in experiment['workloads']:
    name=w['name'];pairs={p:sorted([r for r in rows if r['map']==name and r['policy']==p],key=lambda r:r['seed']) for p in ('master','branch')}
    assert len(pairs['master'])==len(pairs['branch'])==32
    assert [r['seed'] for r in pairs['master']]==[r['seed'] for r in pairs['branch']]
    stats={}
    for metric in metrics:
        before=[r[metric] for r in pairs['master']];after=[r[metric] for r in pairs['branch']]
        b=statistics.mean(before);a=statistics.mean(after);boots=[]
        for _ in range(10000):
            indices=[rng.randrange(32) for _ in range(32)]
            boots.append((sum(after[i] for i in indices)/sum(before[i] for i in indices)-1)*100)
        boots.sort();stats[metric]={'master_mean':b,'branch_mean':a,'change_percent':(a/b-1)*100,'paired_bootstrap_95_percent':[boots[249],boots[9749]]}
    summary[name]={'pairs':32,'actual_ticks':{p:sorted(set(r['ticks'] for r in pairs[p])) for p in pairs},'metrics':stats,'counters':{p:{key:statistics.mean(r[key] for r in pairs[p]) for key in ('building_gradient_jobs','building_gradient_synchronous','building_gradient_published','building_gradient_discarded','building_gradient_max_pending')} for p in pairs}}
(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
lines=['| Map | Whole-process CPU µs/tick, master → PR | Change (paired 95% CI) | Simulation ticks/s, master → PR | Mean per-game p99 tick ms, master → PR |','|---|---:|---:|---:|---:|']
for name,data in summary.items():
    m=data['metrics'];c=m['whole_cpu_us_tick'];t=m['ticks_second'];p=m['tick_p99_ms'];lo,hi=c['paired_bootstrap_95_percent']
    lines.append(f"| {name} | {c['master_mean']:.1f} → {c['branch_mean']:.1f} | {c['change_percent']:+.2f}% ({lo:+.2f}% to {hi:+.2f}%) | {t['master_mean']:.0f} → {t['branch_mean']:.0f} | {p['master_mean']:.3f} → {p['branch_mean']:.3f} |")
(OUT/'table.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines));print(json.dumps(summary,indent=2))
