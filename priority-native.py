import os,signal,time,json,statistics,random,subprocess
from pathlib import Path
from measure import OUT,scenarios,run,timing
# Get one ten-pair answer per idea before the exhaustive matrix.
while not all((OUT/f).exists() for f in ['queue-check.exit']):time.sleep(5)
paused=[]
try:
 # Pause only the two profiling/summary process groups owned by this campaign.
 import psutil
 for p in psutil.process_iter(['pid','cmdline']):
  cmd=p.info['cmdline'] or []
  if len(cmd)>1 and cmd[0].endswith('python3') and cmd[1] in ('artifacts/serial-opt/counts-while-building.py','artifacts/serial-opt/summarize-completed.py','artifacts/serial-opt/integrate-gcc13.py'):
   group=os.getpgid(p.pid)
   if group not in paused:os.killpg(group,signal.SIGSTOP);paused.append(group)
 print('Starting representative ten-pair comparisons',flush=True)
 report={}
 for variant,scenario in [('hiring','hiring'),('stats','sparse'),('queues','sparse'),('players','sparse'),('vectors','dense')]:
  fixture=next(s for s in scenarios if s['id']==scenario)
  def pairs(start,end):
   for i in range(start,end):
    for name in (('baseline',variant) if i%2==0 else (variant,'baseline')):
     run(name,fixture,4,'timing',f'priority-timings/{variant}/{scenario}-4-{i}')
  def compare(count):
   rows=[{name:timing(OUT/name/f'priority-timings/{variant}/{scenario}-4-{i}') for name in ('baseline',variant)} for i in range(count)]
   metrics={}
   for k in ('owner','process','wall','join'):
    ratios=[(r[variant][k]/r['baseline'][k]-1)*100 for r in rows if r['baseline'][k]]
    if not ratios:continue
    rng=random.Random(7349);boots=sorted(statistics.median(rng.choices(ratios,k=len(ratios))) for _ in range(10000))
    metrics[k]={'paired_median_percent':statistics.median(ratios),'ci95_percent':[boots[250],boots[9749]]}
   return {'fixture':scenario,'participants':4,'pairs':count,'raw_ns':rows,'metrics':metrics}
  pairs(0,10);r=compare(10)
  if any(r['metrics'][k]['paired_median_percent']>2 for k in ('owner','wall')):pairs(10,20);r=compare(20)
  report[variant]=r;(OUT/'priority-paired-timings.json').write_text(json.dumps(report,indent=2));print(variant,r['metrics'],flush=True)
finally:
 for group in paused:
  try:os.killpg(group,signal.SIGCONT)
  except ProcessLookupError:pass
