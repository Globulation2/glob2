from pathlib import Path
import json,hashlib
p=Path(__file__).resolve().parent
platforms={'macOS':p,'therig':p/'therig','devlaptop':p/'devlaptop'}
records={name:json.loads((path/'results.json').read_text()) for name,path in platforms.items()}
summary={'paired':[],'cross_platform':[],'ci':[]}
for name,rows in records.items():
 for case in sorted(set(r['case'] for r in rows)):
  a,b=[next(r for r in rows if r['case']==case and r['variant']==v) for v in ['base','head']]
  matches={f:a['files'][f]==b['files'][f] for f in a['files']}
  summary['paired'].append({'platform':name,'case':case,'ticks':a['result']['ticks']-a['start_tick'],'files':matches})
for ref in records['macOS']:
 for name,rows in records.items():
  if name=='macOS':continue
  candidate=next(r for r in rows if r['case']==ref['case'] and r['variant']==ref['variant'])
  summary['cross_platform'].append({'platform':name,'case':ref['case'],'variant':ref['variant'],'files':{f:ref['files'][f]==candidate['files'][f] for f in ref['files']}})
traces=list((p/'ci').rglob('*.checksums'))+list((p/'mac-base').glob('*.checksums'))+list((p/'mac-head').glob('*.checksums'))
reference=(p/'mac-base/native.replay.checksums').read_bytes()
for trace in traces:
 b=trace.read_bytes();summary['ci'].append({'trace':str(trace.relative_to(p)),'sha256':hashlib.sha256(b).hexdigest(),'matches_original_mac':b==reference})
summary['within_platform_pr_equivalence']=all(all(r['files'].values()) for r in summary['paired'])
summary['cross_platform_tick_equivalence']=all(r['files']['game.replay.checksums'] for r in summary['cross_platform'])
summary['cross_platform_all_save_bytes_identical']=all(all(r['files'].values()) for r in summary['cross_platform'])
summary['ci_tick_equivalence']=all(r['matches_original_mac'] for r in summary['ci'])
(p/'comparison.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
assert summary['cross_platform_tick_equivalence'] and summary['ci_tick_equivalence']
