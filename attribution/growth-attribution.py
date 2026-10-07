import json,statistics as S,sys
from pathlib import Path
sys.path.insert(0,'test')
from benchmark_resource_growth import interval
out={}
for campaign in ['engine-default-final','engine-controlled']:
 rows=[json.loads(l) for l in Path('artifacts/resource-growth',campaign,'measurements.jsonl').read_text().splitlines()]
 rows=[r for r in rows if r['repeat']>=0]
 result={}
 for sc in sorted({r['scenario'] for r in rows}):
  variants={v:{r['repeat']:r for r in rows if r['scenario']==sc and r['variant']==v} for v in ['legacy-t4','d8t4-owner','d8t4-shared']}
  table={}
  for v,rs in variants.items():
   fields={
    'run_ms':lambda r:r['result']['run_ns']/1e6,
    'run_cpu_ms':lambda r:r['result']['benchmark_run_cpu_ns']/1e6,
    'capture_ms':lambda r:r['result']['ai_pipeline']['extraction_ns']/1e6,
    'copied_MiB':lambda r:r['result']['ai_pipeline']['bytes_copied']/2**20,
    'captures':lambda r:r['result']['ai_pipeline']['captures'],
    'compute_ms':lambda r:r['result'].get('growth_computeNs',0)/1e6,
    'publication_ms':lambda r:r['result'].get('growth_publicationNs',0)/1e6,
    'wait_ms':lambda r:r['result'].get('growth_waitNs',0)/1e6,
   }
   entry={k:S.median(f(r) for r in rs.values()) for k,f in fields.items()}
   entry['paired_legacy_deltas']={k:{'median':S.median(a:=[f(r)-f(variants['legacy-t4'][i]) for i,r in rs.items()]),'ci':interval(a)} for k,f in fields.items()}
   table[v]=entry
  result[sc]=table
 out[campaign]=result
Path('artifacts/resource-growth/attribution/engine-breakdown.json').write_text(json.dumps(out,indent=2))
for c,result in out.items():
 print(c)
 for sc,t in result.items():
  a=t['d8t4-shared'];b=t['legacy-t4'];d=a['paired_legacy_deltas']
  print(sc,'captures',b['captures'],a['captures'],'capture ms',round(b['capture_ms'],2),round(a['capture_ms'],2),'delta',round(d['capture_ms']['median'],2),[round(x,2) for x in d['capture_ms']['ci']], 'MiB',round(b['copied_MiB'],1),round(a['copied_MiB'],1),'CPU delta',round(d['run_cpu_ms']['median'],2))
