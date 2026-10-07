from pathlib import Path
import json,statistics as S,sys
sys.path.insert(0,'test');from benchmark_resource_growth import interval
r=Path('artifacts/resource-growth/simple');samples=json.load(open(r/'component.json'))['samples'];lines=['## Component experiment','',
'The component harness resets fixtures outside the timed region and charges every variant for the same pre-existing shared snapshot component union. It compares the original immediate algorithm, the new kernel with immediate serial publication, delayed owner and delayed shared scheduling in the same executable. These fixtures evolve during 64 ticks, so old/new effective work can differ. A split pass still has overhead in some scenarios; simplification does not make the architecture free.','',
'New immediate split elapsed-time change versus the original immediate algorithm, paired medians with bootstrap 95% intervals. Positive values mean slower. Raw compute/publication/capture/copy/stock metrics for all four variants are in component.json (924 rows).','',
'| Scenario | 128² | 256² | 512² |','|---|---:|---:|---:|']
summary={}
for sc in ['sparse','dense','saturated','harvested','blocked','multi','disabled']:
 row=[]
 for size in [128,256,512]:
  a=[x for x in samples if x['size']==size and x['scenario']==sc and x['repeat']>=0];vs={v:{x['repeat']:x for x in a if x['variant']==v} for v in range(4)};ratios=[vs[1][i]['elapsed_ns']/vs[0][i]['elapsed_ns']-1 for i in vs[0]];lo,hi=interval(ratios);m=S.median(ratios);row.append(f'{m*100:+.1f}% ({lo*100:+.1f} to {hi*100:+.1f})');summary[f'{size}-{sc}']={'split_change':m,'ci':[lo,hi]}
 lines.append('| '+sc+' | '+' | '.join(row)+' |')
lines+=['','The identical-input live/snapshot kernel control also passed: 15 scenarios, 32 seeds each, exact ordered proposal/RNG continuation equality; 630 timing rows are included in kernel.json. It excludes capture and publication.','',
'## Twenty-seed ecology check','',
'128² dense fixture, 512 ticks, twenty fixed seeds, compared with the retained immediate algorithm in the same executable. Harvest every eight ticks, either retaining the last unit or allowing depletion. Values are means over seeds. These are controlled ecology probes, not a substitute for playing a full match.','',
'| Policy | Measure | Immediate | Simplified delayed | Change |','|---|---|---:|---:|---:|']
e=json.load(open(r/'ecology.json'))['samples']
for policy in ['reserve','deplete']:
 for key in ['harvested','food','deposits','seeded','replenished','depleted']:
  a=[S.mean(x[key] for x in e if x['variant']==v and x['harvest_policy']==policy) for v in [0,1]];change=f'{(a[1]/a[0]-1)*100:+.1f}%' if a[0] else 'both zero';lines.append(f'| {policy} | {key} | {a[0]:.1f} | {a[1]:.1f} | {change} |')
lines+=['','Removing identity restrictions permits pending increments to revive depleted cells, increasing seed/depletion turnover in the depletion probe. Sustainable harvested output in the reserve probe is 1.6% lower; depletion-policy harvest is 0.2% lower. Multi-material seeding changes are quantified separately in the engine table. Updated dense/multi-material saves are under playable/.','']
p=r/'README.md';s=p.read_text().replace('## Reproducibility and validation','\n'.join(lines)+'\n## Reproducibility and validation').replace('five skipped','five display-dependent tests skipped').replace('Native per-tick checks across worker counts and delays are recorded under determinism/.','All 96 native per-tick checks passed across delays 1/3/8, owner/shared placement and executor sizes 1/2/4/8, over four scenarios and 128 ticks. Both world and replay checksum sidecars match at each fixed delay; raw traces are under determinism/.').replace('Subsequent verification logs are included separately.','All twelve golden cases passed again without update mode. The three component/kernel/ecology cases passed. Final validation revision is 5430ef31c; a fresh merge-tree against master 52e4d3dc0 is clean, whose additions after the integrated base only change scripting test/trace handling.');p.write_text(s);(r/'component-summary.json').write_text(json.dumps(summary,indent=2))
