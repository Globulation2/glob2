#!/usr/bin/env python3
"""Regenerate benchmark charts and a standalone HTML report from per-run JSON."""
from pathlib import Path
import base64, datetime as dt, html, json, statistics, subprocess, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
ROOT=Path(__file__).resolve().parent
config=json.loads((ROOT/'experiment.json').read_text())
target_runs=len(config['seeds'])*len(config['snapshots'])
metrics=[('ticks_per_second','Simulation throughput','ticks / second',1),
         ('wall_seconds','Completed game: end-to-end time','seconds',1),
         ('peak_rss_kib','Peak resident memory','MiB',1/1024),
         ('total_cpu_seconds','Total process CPU','CPU seconds',1),
         ('cpu_seconds_per_tick','Total process CPU per tick','microseconds / tick',1e6),
         ('ticks','Game duration','simulation ticks',1)]
rows=[]
for snap in config['snapshots']:
 folder=ROOT/'snapshots'/(snap['sample_date']+'-'+snap['commit'][:9])
 runs=[json.loads(p.read_text()) for p in sorted((folder/'runs').glob('*/result.json'))]
 rows.append((snap,runs))
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'axes.grid':True,'grid.alpha':.18})
fig,axes=plt.subplots(3,2,figsize=(14,12),layout='constrained')
rng=np.random.default_rng(20261011)
for ax,(key,title,unit,scale) in zip(axes.flat,metrics):
 x=[];y=[];lo=[];hi=[]
 for index,(snap,runs) in enumerate(rows):
  values=[r[key]*scale for r in runs if r['status']=='completed' and r[key] is not None]
  if not values:continue
  values=np.array(values);boot=rng.choice(values,(10000,len(values)),replace=True).mean(axis=1)
  x.append(index);y.append(values.mean());bounds=np.percentile(boot,[2.5,97.5]);lo.append(bounds[0]);hi.append(bounds[1])
  ax.scatter(np.full(len(values),index),values,s=9,alpha=.2,color='#4068a0')
 if x:
  ax.plot(x,y,'o-',color='#126a6b',lw=2,markersize=5)
  ax.fill_between(x,lo,hi,color='#126a6b',alpha=.15)
 ax.set_title(title,loc='left',fontweight='bold');ax.set_ylabel(unit)
 ax.set_xticks(range(len(rows)),[s['sample_date'][5:] for s,_ in rows],rotation=45,ha='right')
 ax.set_xlim(-.3,len(rows)-.7)
 ax.set_xlabel('Snapshot date (2026; baseline predates September 3)')
 if key!='ticks_per_second':ax.set_ylim(bottom=0)
finished=sum(len(rs) for _,rs in rows);wins=sum(r['status']=='completed' for _,rs in rows for r in rs)
fig.suptitle(f'Playground · 128×128 · 8 Nicowar AIs · FFA\n{finished}/{target_runs} measured runs, {wins} winners · means and bootstrap 95% intervals',fontsize=17)
fig.savefig(ROOT/'performance.png',dpi=160)
fig.savefig(ROOT/'performance.svg')
headers=['Snapshot','Commit','Measured','Winners','Timeouts','Mean ticks/s','Mean e2e seconds','Mean peak MiB','Mean CPU seconds','Mean CPU µs/tick','Mean game ticks']
trs=[]
for snap,runs in rows:
 def mean(key,scale=1):
  vs=[r[key]*scale for r in runs if r['status']=='completed' and r[key] is not None]
  return f'{statistics.mean(vs):,.2f}' if vs else '—'
 values=[snap['sample_date'],snap['commit'][:9],f'{len(runs)}/{len(config["seeds"])}',str(sum(r['status']=='completed' for r in runs)),str(sum(r['status']=='timeout' for r in runs)),mean('ticks_per_second'),mean('wall_seconds'),mean('peak_rss_kib',1/1024),mean('total_cpu_seconds'),mean('cpu_seconds_per_tick',1e6),mean('ticks')]
 trs.append('<tr>'+''.join('<td>'+html.escape(v)+'</td>' for v in values)+'</tr>')
subprocess.run([sys.executable,str(ROOT/'plot_build_times.py')],check=True)
build_png=base64.b64encode((ROOT/'build-times.png').read_bytes()).decode()
png=base64.b64encode((ROOT/'performance.png').read_bytes()).decode()
report=f'''<!doctype html><meta charset="utf-8"><title>Glob2 historical engine performance</title>
<style>body{{font:16px system-ui;margin:32px auto;max-width:1500px;padding:0 24px;color:#182a34}}img{{width:100%}}table{{border-collapse:collapse;font-size:13px;width:100%}}td,th{{text-align:right;padding:8px;border-bottom:1px solid #d8e0e5}}th:first-child,td:first-child{{text-align:left}}p{{max-width:1050px;line-height:1.6}}code{{font-size:13px}}</style>
<h1>Glob2 engine performance over time</h1><p><a href="history-summary.html">Historical analysis: summary</a> · <a href="history-detailed.html">Detailed narrative and proposed experiments</a> · <a href="commit-inventory.html">Complete commit inventory</a></p><p>Frozen master: <code>{config['tip']}</code>. Playground map SHA-256: <code>{config['map_sha256']}</code>. {len(config['seeds'])} fixed random game seeds, identical across {len(config['snapshots'])} snapshots at three-day calendar intervals. All binaries are compiled before measuring any games. Games run four at a time on six dedicated logical CPUs each, to a winner, with a one-hour wall-clock timeout.</p>
<p><b>Progress:</b> {finished}/{target_runs} measured runs; {wins} reached a winner. Only winner runs contribute to plotted means. Points show individual seeds; bands are bootstrap 95% intervals for the mean. Partial snapshots are provisional. CPU is user plus system time summed over process threads. Peak memory is Linux maximum resident set size. Throughput uses ticks divided by the wall time of Engine::run; end-to-end time and CPU include startup and shutdown. Automatic replay recording remains at each revision's default.</p>
<img src="data:image/png;base64,{png}" alt="Six performance charts"><table><thead><tr>{''.join('<th>'+html.escape(h)+'</th>' for h in headers)}</tr></thead><tbody>{''.join(trs)}</tbody></table>
<p>Changes in rules, random streams and AI can change game length and workload. End-to-end savings alone do not isolate engine optimization; inspect tick counts and CPU per tick together. These measurements cover Linux x86-64 on one host with GCC 13, release builds and four disjoint groups of three physical cores plus their SMT siblings. Historical release defaults and runtime dependencies can differ. The legacy runner uses eight AI controllers plus one idle local observer. See <a href="experiment.json">experiment.json</a>, <a href="summary.csv">summary.csv</a>, <a href="runs.csv">runs.csv</a>, and per-snapshot logs and adapter patches for provenance.</p>'''
report+=f'''<h2>Build elapsed time</h2><p><strong>These are mixed successful SCons invocations, not clean-build performance measurements.</strong> September 2 and 5 logged zero native compiler commands and reported the game/library targets up to date; their 11.3 and 19.0 seconds are resumed/no-op revalidation. The lower panel counts printed compiler commands, including possible ccache hits, rather than CPU work. Job counts, retained prior attempts, source asset dimensions and packaging flags are recorded in <a href="build-times.csv">build-times.csv</a> and <a href="build-times.json">build-times.json</a>; <a href="../engine-investigations/build-stages/build-audit.json">the complete read-only audit</a> retains log hashes. Compiler CPU and initial cache-hit states are unavailable and remain null. The former elapsed × jobs ÷ 32 panel has been removed: it cannot normalize cache/resume differences or the serial asset-export action. Raw successful-invocation values are preserved; neither bars nor source-file counts establish a historical compile regression percentage.</p><img src="data:image/png;base64,{build_png}" alt="Raw successful SCons invocation times and logged compiler commands">'''
(ROOT/'report.html').write_text(report)
print(f'{finished}/{target_runs} runs; report: {ROOT}/report.html')
