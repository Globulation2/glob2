from pathlib import Path
import re,json
out=Path.cwd()/'artifacts/serial-reanalysis';data=json.loads((out/'results.json').read_text());groups={}
for r in data:
 if r['mode']!='perf' or r['participants']!=4:continue
 sums=dict(owner=0,capture=0,copy=0,tower=0,owner_gradient=0,worker=0,normalize=0,worker_gradient=0,worker_walk=0)
 for line in (out/f"{r['fixture']}-4-perf/report.txt").read_text().splitlines():
  m=re.match(r'\s*([\d.]+)%\s+(\d+):\S+\s+\[([^]]+)\]\s+(.+?)(?:\s{2,}|$)',line)
  if not m:continue
  v=float(m[1]);sym=m[4]
  if int(m[2])==r['owner_tid']:
   sums['owner']+=v
   if 'SimulationSnapshot::capture' in sym:sums['capture']+=v
   if 'memmove' in sym or 'memcpy' in sym:sums['copy']+=v
   if 'locationIsInEnemyGuardTowerRange' in sym:sums['tower']+=v
   if 'BuildingGradientSearch::' in sym:sums['owner_gradient']+=v
  else:
   sums['worker']+=v
   if 'AIMaximaFoodLedger::Input::normalize' in sym:sums['normalize']+=v
   if 'gradient_preparation::propagate' in sym:sums['worker_gradient']+=v
   if 'AIMaximaFoodLedger::Ledger::walk' in sym:sums['worker_walk']+=v
 groups[r['fixture']]={k:v/sums['owner' if k in ('capture','copy','tower','owner_gradient') else 'worker']*100 for k,v in sums.items() if k not in ('owner','worker')}
(out/'groups.json').write_text(json.dumps(groups,indent=2))
lines=['','## Grouped self samples, four participants','','Sums combine distinct self symbols in each named family and normalize within the owner or other-thread role. Snapshot capture excludes libc copy samples; copy samples include callers other than snapshot capture. These are sample shares, not predicted speedups. One profile per fixture; percentages are approximate.','','| Fixture | Owner snapshot capture | Owner memcpy/memmove | Owner tower query | Owner building-gradient search | Worker X/Y normalization | Worker gradient propagation |','|---|---:|---:|---:|---:|---:|---:|']
for key,r in groups.items():lines.append('| '+key+' | '+' | '.join(f"{r[k]:.1f}%" for k in ('capture','copy','tower','owner_gradient','normalize','worker_gradient'))+' |')
p=out/'report.md';p.write_text(p.read_text()+'\n'.join(lines)+'\n')
