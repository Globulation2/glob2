from pathlib import Path
import json,re,statistics,csv
out=Path.cwd()/'artifacts/serial-reanalysis';data=json.loads((out/'results.json').read_text());summary={}
for row in data:
 if row['mode']!='perf':continue
 key=f"{row['fixture']}-{row['participants']}";dest=out/f'{key}-perf';samples=[]
 for line in (dest/'report.txt').read_text().splitlines():
  m=re.match(r'\s*([\d.]+)%\s+(\d+):\S+\s+\[([^]]+)\]\s+(.+?)(?:\s{2,}|$)',line)
  if m:samples.append({'total_percent':float(m[1]),'tid':int(m[2]),'mode':m[3],'symbol':m[4]})
 owner=[s for s in samples if s['tid']==row['owner_tid']];others=[s for s in samples if s['tid']!=row['owner_tid']]
 def role(rows):
  grouped={}
  for s in rows:
   key=(s['symbol'],s['mode']);grouped.setdefault(key,dict(s,total_percent=0,tid=None))['total_percent']+=s['total_percent']
  rows=sorted(grouped.values(),key=lambda s:s['total_percent'],reverse=True)
  share=sum(s['total_percent'] for s in rows)
  return {'total_sample_share_percent':share,'kernel_share_percent':sum(s['total_percent'] for s in rows if s['mode']=='k')/share*100 if share else 0,'top':[dict(s,role_percent=s['total_percent']/share*100) for s in rows[:20]]}
 timings=[s for s in data if s['fixture']==row['fixture'] and s['participants']==row['participants'] and s['mode'].startswith('timing')]
 r={'owner':role(owner),'other_threads':role(others),'timing_repeats':len(timings),'timings':timings,'median_seconds':{k:statistics.median(t[k]/1e9 for t in timings) for k in ['owner_cpu_ns','process_cpu_ns','wall_ns','join_wait_ns']} if timings else {}}
 counters=out/f'{key}-counters/counters.csv'
 if counters.exists():
  r['counters']={}
  for c in csv.reader(counters.read_text().splitlines()):
   if len(c)>2:
    try:r['counters'][c[2]]={'value':float(c[0]),'unit':c[1]}
    except ValueError:pass
 summary[key]=r
(out/'summary.json').write_text(json.dumps(summary,indent=2))
lines=['# Merged simulation hot-path reanalysis','','Revision: `6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf` (PR #963, all five commits). Linux x86-64, therig, GCC 13.4.0, normal release optimization and debug symbols; no -pg. Sampling includes user and kernel cycles. Loading, fixture generation, final saving and teardown are outside the measured window. All measured continuations match the archived final checksums and keep eight-tick scheduling delays.','','## Owner work, other-thread CPU and joins','','4,096 ticks per run. Sparse/dense controls have three native repeats; other fixtures have one reconnaissance run. Other builds were active on the shared host; host-load.json records each run. These are diagnostic timings under that load, not baseline/candidate speedup claims or an isolated executor-sizing benchmark. Other-thread CPU is process CPU minus owner CPU and may include small auxiliary-thread costs. Tiny negative residuals from successive clock reads are displayed as zero; raw values are retained. Joins are elapsed waits; they must not be added to CPU totals.','','| Fixture | Participants | Repeats | Owner CPU s | Other CPU s | Wall s | Join elapsed s | Owner kernel sample share |','|---|---:|---:|---:|---:|---:|---:|---:|']
for key,r in summary.items():
 m=r['median_seconds'];other=max(0,statistics.median((t['process_cpu_ns']-t['owner_cpu_ns'])/1e9 for t in r['timings'])) if r['timings'] else 0
 if m:lines.append(f"| {key.rsplit('-',1)[0]} | {key.rsplit('-',1)[1]} | {r['timing_repeats']} | {m['owner_cpu_ns']:.3f} | {other:.3f} | {m['wall_ns']:.3f} | {m['join_wait_ns']:.3f} | {r['owner']['kernel_share_percent']:.1f}% |")
lines+=['','## Process hardware counters','','Counters cover all process threads during the continuation. Values are not normalized by host load. One participant executes worker jobs on the owner; its owner CPU therefore includes both roles.','','| Case | Instructions (billions) | IPC | Context switches | Cache misses (millions) |','|---|---:|---:|---:|---:|']
for key,r in summary.items():
 c={k:v['value'] for k,v in r.get('counters',{}).items()}
 if 'cycles' in c:lines.append(f"| {key} | {c['instructions']/1e9:.2f} | {c['instructions']/c['cycles']:.2f} | {c['context-switches']:.0f} | {c['cache-misses']/1e6:.1f} |")
lines+=['','## Snapshot counters','','End-of-run capture counters from the first native run, including the initial boundary capture (4,097 captures for 4,096 continued ticks). These are capture-accounted bytes, not all memory traffic or a timing measurement. Existing component reuse is active.','','| Fixture | Copied GiB | Component reuses | Capture allocation count | Peak snapshot capacity MiB |','|---|---:|---:|---:|---:|']
for name in ['sparse','hiring','established','dense','combat']:
 p=out/f'{name}-4-timing-0/result.json'
 if p.exists():
  a=json.loads(p.read_text())['ai_pipeline']
  lines.append(f"| {name} | {a['bytes_copied']/2**30:.2f} | {a['component_reuses']} | {a['allocations']} | {a['snapshot_peak_capacity_bytes']/2**20:.1f} |")
lines+=['','## Native sample leaders','','Percentages below are normalized within the indicated role, not wall time. Self samples identify where CPU executes; inclusive caller paths overlap and must not be summed. Full symbolized reports and owner call graphs accompany this report.']
for key,r in summary.items():
 if not key.endswith('-4'):continue
 lines+=['','### '+key,'','| Role | Self sample share | Symbol |','|---|---:|---|']
 for name in ['owner','other_threads']:
  for s in r[name]['top'][:8]:
   label=s['symbol'] if len(s['symbol'])<160 else s['symbol'][:157]+'…'
   lines.append(f"| {name} | {s['role_percent']:.1f}% | `{label}` |")
lines+=['','## Scope','','The previous GCC 15 Callgrind profiles of the five-patch combination are supplemental instruction/caller evidence against the original audit baseline. They predate later gradient-policy changes and are not substituted for the merged-revision native samples. The headless owner includes read-boundary capture, AI publication, order execution and simulation work; interactive rendering/input and network stalls need separate measurements. ARM64 was excluded by the updated local-only scope.']
(out/'report.md').write_text('\n'.join(lines)+'\n')
print('Summarized',len(summary),'fixture/executor profiles')
