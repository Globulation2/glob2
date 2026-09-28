from pathlib import Path
import json
p=Path(__file__).resolve().parent
hosts={'macOS':p,'therig':p/'therig','devlaptop':p/'devlaptop'}
data={h:json.loads((r/'results.json').read_text()) for h,r in hosts.items()}
out={'baseline_vs_fixed':[],'fixed_cross_platform':[]}
for h,rows in data.items():
 for case in sorted({r['case'] for r in rows}):
  a,b=[next(r for r in rows if r['case']==case and r['variant']==v) for v in ['base','head']]
  out['baseline_vs_fixed'].append({'host':h,'case':case,'ticks':b['result']['ticks']-b['start_tick'],'equal_files':{f:a['files'][f]==b['files'][f] for f in a['files']}})
for ref in data['macOS']:
 if ref['variant']!='head':continue
 for h,rows in data.items():
  if h=='macOS':continue
  other=next(r for r in rows if r['case']==ref['case'] and r['variant']=='head')
  out['fixed_cross_platform'].append({'host':h,'case':ref['case'],'equal_files':{f:ref['files'][f]==other['files'][f] for f in ref['files']}})
(p/'comparison.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
assert all(r['equal_files']['game.replay.checksums'] for r in out['baseline_vs_fixed'])
assert all(all(r['equal_files'].values()) for r in out['fixed_cross_platform'])
