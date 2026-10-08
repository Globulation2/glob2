from pathlib import Path
import json,statistics as S,sys
sys.path.insert(0,'test');from benchmark_resource_growth import interval
root=Path('artifacts/resource-growth/attribution-v2');summary={}
def estimate(values):return {'median':S.median(values),'ci95':interval(values)}
for experiment in ['statistics','batched']:
 p=root/experiment/'measurements.jsonl'
 if not p.exists():continue
 rows=[json.loads(x) for x in p.read_text().splitlines()];out={}
 for scenario in sorted({r['scenario'] for r in rows}):
  rs=[r for r in rows if r['scenario']==scenario and r['repeat']>=0];variants={v:{r['repeat']:r['result'] for r in rs if r['variant']==v} for v in sorted({r['variant'] for r in rs})};data={}
  for name,r in variants.items():
   data[name]={'repetitions':len(r),'median_ms':{k:S.median(v[k]/1e6 for v in r.values()) for k in ['run_ns','benchmark_run_cpu_ns','growth_computeNs','growth_publicationNs','growth_waitNs']},'capture_ms':S.median(v['ai_pipeline']['extraction_ns']/1e6 for v in r.values()),'snapshot_MB':S.median(v['ai_pipeline']['bytes_copied']/1e6 for v in r.values())}
   if name!='current':
    a=variants['current'];paired=sorted(set(a)&set(r));data[name]['paired_count']=len(paired)
    for k in ['run_ns','benchmark_run_cpu_ns','growth_publicationNs']:
     if not a[paired[0]][k]:continue
     data[name][k]={'reduction':estimate([1-r[i][k]/a[i][k] for i in paired]),'saved_ms':estimate([(a[i][k]-r[i][k])/1e6 for i in paired])}
    data[name]['throughput_gain']=estimate([a[i]['run_ns']/r[i]['run_ns']-1 for i in paired])
  out[scenario]=data
 summary[experiment]=out
for experiment in ['writes','writes-outlined','writes-matched']:
 p=root/(experiment+'.json')
 if not p.exists():continue
 rows=json.load(open(p))['samples'];out={}
 for size in [128,256,512]:
  for scenario in ['sparse','dense','saturated','blocked','multi']:
   rs=[r for r in rows if r['size']==size and r['scenario']==scenario and r['repeat']>=0];a={r['repeat']:r for r in rs if not r['count_only']};b={r['repeat']:r for r in rs if r['count_only']}
   out[f'{size}-{scenario}']={'reduction':estimate([1-b[i]['elapsed_ns']/a[i]['elapsed_ns'] for i in a]),'saved_us_per_batch':estimate([(a[i]['elapsed_ns']-b[i]['elapsed_ns'])/1024e3 for i in a]),'proposals_per_batch':a[0]['proposals']/1024,'full_us_per_batch':S.median(r['elapsed_ns']/1024e3 for r in a.values())}
 summary[experiment]=out
p=root/'capture.json'
if p.exists():
 rows=json.load(open(p))['samples'];out={}
 for size in [256,512]:
  for cadence in [1,4,32]:
   rs=[r for r in rows if r['size']==size and r['existing_cadence']==cadence and r['repeat']>=0];a={r['repeat']:r for r in rs if not r['growth']};b={r['repeat']:r for r in rs if r['growth']};assert all(a[i]['final_food']==b[i]['final_food'] for i in a)
   out[f'{size}-cadence{cadence}']={'extra_ms':estimate([(b[i]['capture_ns']-a[i]['capture_ns'])/1e6 for i in a]),'baseline_ms':S.median(r['capture_ns']/1e6 for r in a.values()),'growth_ms':S.median(r['capture_ns']/1e6 for r in b.values()),'baseline_MB':a[0]['copied_bytes']/1e6,'growth_MB':b[0]['copied_bytes']/1e6}
 summary['capture']=out
p=root/'retention.json'
if p.exists():
 rows=json.load(open(p))['samples'];out={}
 for size in [256,512]:
  rs=[r for r in rows if r['size']==size and r['repeat']>=0];a={r['repeat']:r for r in rs if r['retained']==0}
  for retained in [0,1,4,8]:
   b={r['repeat']:r for r in rs if r['retained']==retained};assert all(a[i]['final_food']==b[i]['final_food'] for i in a)
   out[f'{size}-retain{retained}']={'extra_ms':estimate([(b[i]['capture_ns']-a[i]['capture_ns'])/1e6 for i in a]),'capture_ms':S.median(r['capture_ns']/1e6 for r in b.values()),'copied_MB':b[0]['copied_bytes']/1e6}
 summary['retention']=out
p=root/'stage-runs/measurements.jsonl'
if p.exists():
 rows=[json.loads(x) for x in p.read_text().splitlines()];out={};names=['capture','old_growth','calculate','publish','submit','gradient_seed','gradient_propagate','AI']
 for scenario in sorted({r['scenario'] for r in rows}):
  d={}
  for variant in ['legacy','current']:
   rs=[r for r in rows if r['scenario']==scenario and r['variant']==variant and r['repeat']>=0]
   if not rs:continue
   value={name:S.median(r['stage_cpu']['cpu_ns'][i]/1e6 for r in rs) for i,name in enumerate(names)}
   value['total_cpu']=S.median(r['result']['benchmark_run_cpu_ns']/1e6 for r in rs);value['residual']=S.median((r['result']['benchmark_run_cpu_ns']-sum(r['stage_cpu']['cpu_ns']))/1e6 for r in rs);value['repetitions']=len(rs);d[variant]=value
  out[scenario]=d
 summary['stages']=out
p=root/'fair/measurements.jsonl'
if p.exists():
 rows=[json.loads(x) for x in p.read_text().splitlines()];out={}
 for scenario in sorted({r['scenario'] for r in rows}):
  a={r['repeat']:r['result'] for r in rows if r['scenario']==scenario and r['variant']=='legacy-bulk' and r['repeat']>=0};b={r['repeat']:r['result'] for r in rows if r['scenario']==scenario and r['variant']=='current-bulk' and r['repeat']>=0};pairs=sorted(set(a)&set(b))
  if not pairs:continue
  out[scenario]={'paired_count':len(pairs),'throughput_gain':estimate([a[i]['run_ns']/b[i]['run_ns']-1 for i in pairs]),'cpu_change':estimate([b[i]['benchmark_run_cpu_ns']/a[i]['benchmark_run_cpu_ns']-1 for i in pairs]),'wall_saved_ms':estimate([(a[i]['run_ns']-b[i]['run_ns'])/1e6 for i in pairs]),'legacy_TPS':S.median(1024e9/a[i]['run_ns'] for i in pairs),'current_TPS':S.median(1024e9/b[i]['run_ns'] for i in pairs)}
 summary['fair']=out
p=root/'owner-control/measurements.jsonl'
if p.exists():
 rows=[json.loads(x) for x in p.read_text().splitlines()];out={}
 for scenario in sorted({r['scenario'] for r in rows}):
  variants={v:{r['repeat']:r['result'] for r in rows if r['scenario']==scenario and r['variant']==v and r['repeat']>=0} for v in ['shared','owner-eager','owner-deadline']};d={}
  for v,rs in variants.items():
   if rs:d[v]={'repetitions':len(rs),'wall_ms':S.median(r['run_ns']/1e6 for r in rs.values()),'cpu_ms':S.median(r['benchmark_run_cpu_ns']/1e6 for r in rs.values()),'capture_ms':S.median(r['ai_pipeline']['extraction_ns']/1e6 for r in rs.values()),'tracked_copy_MB':S.median(r['ai_pipeline']['bytes_copied']/1e6 for r in rs.values())}
  for candidate,control in [('shared','owner-eager'),('shared','owner-deadline'),('owner-eager','owner-deadline')]:
   a=variants[control];b=variants[candidate];pairs=sorted(set(a)&set(b))
   if pairs:d[candidate+'-vs-'+control]={'paired_count':len(pairs),'throughput_gain':estimate([a[i]['run_ns']/b[i]['run_ns']-1 for i in pairs]),'cpu_change':estimate([b[i]['benchmark_run_cpu_ns']/a[i]['benchmark_run_cpu_ns']-1 for i in pairs]),'wall_saved_ms':estimate([(a[i]['run_ns']-b[i]['run_ns'])/1e6 for i in pairs])}
  out[scenario]=d
 summary['owner-control']=out
p=root/'direct-control/measurements.jsonl'
if p.exists():
 rows=[json.loads(x) for x in p.read_text().splitlines()];out={}
 for scenario in sorted({r['scenario'] for r in rows}):
  a={r['repeat']:r['result'] for r in rows if r['scenario']==scenario and r['variant']=='owner-direct' and r['repeat']>=0};b={r['repeat']:r['result'] for r in rows if r['scenario']==scenario and r['variant']=='shared' and r['repeat']>=0};pairs=sorted(set(a)&set(b))
  if not pairs:continue
  d={'paired_count':len(pairs),'shared_throughput_gain':estimate([a[i]['run_ns']/b[i]['run_ns']-1 for i in pairs]),'shared_cpu_change':estimate([b[i]['benchmark_run_cpu_ns']/a[i]['benchmark_run_cpu_ns']-1 for i in pairs]),'wall_saved_ms':estimate([(a[i]['run_ns']-b[i]['run_ns'])/1e6 for i in pairs])}
  for mode,rs in [('owner-direct',a),('shared',b)]:d[mode]={'wall_ms':S.median(rs[i]['run_ns']/1e6 for i in pairs),'cpu_ms':S.median(rs[i]['benchmark_run_cpu_ns']/1e6 for i in pairs),'capture_ms':S.median(rs[i]['ai_pipeline']['extraction_ns']/1e6 for i in pairs),'tracked_copy_MB':S.median(rs[i]['ai_pipeline']['bytes_copied']/1e6 for i in pairs)}
  out[scenario]=d
 summary['direct-control']=out
(root/'summary.json').write_text(json.dumps(summary,indent=2));print('Summarized',', '.join(summary))
