#!/usr/bin/env python3
"""Read retained timing receipts only; no native workload/build/profiler.
Supports original5-role and new6-role manifests without rewriting old receipts.
"""
import argparse,collections,html,itertools,json,re,statistics
from pathlib import Path
p=Path(__file__).resolve().parent;ap=argparse.ArgumentParser();ap.add_argument('--manifest',type=Path,action='append');ap.add_argument('--output-dir',type=Path);args=ap.parse_args();output=args.output_dir or p;output.mkdir(parents=True,exist_ok=True);manifests=args.manifest or[p/'harness-pilot-manifest.json',p/'native-pilot-manifest.json']
report={'scope':'Paired ratio of cohort means. Fresh game seeds are independent units; timing repetitions nested. One-save/one-seed uncertainty conditional on timing only. Synthetic two repeats are not a workload population sample and have no population CI. Preserve negative skew/N1 results and failed contracts.','cohorts':{},'comparisons':[],'validation_errors':[],'missing_manifests':[]}
def pct(a,b):return 100*(a/b-1) if b else None
def quantile(v,f):
 v=sorted(x for x in v if x is not None);
 if not v:return None
 t=(len(v)-1)*f;i=int(t);return v[i]+(v[min(i+1,len(v)-1)]-v[i])*(t-i)
def flag(args,name):return args[args.index(name)+1] if name in args else None
for manifestfile in manifests:
 if not manifestfile.exists():report['missing_manifests'].append(str(manifestfile));continue
 manifest=json.loads(manifestfile.read_text());variants={v['role']:v for v in manifest['variants']};allrows=[];missing=[]
 for s in manifest['scenarios']:
  for rep in range(s.get('repeats',2)):
   for role in variants:
    f=manifestfile.parent/'timing'/(s['id']+'-'+role+'-r'+str(rep))/'receipt.json'
    if not f.exists():missing.append(str(f));continue
    r=json.loads(f.read_text());r['_receipt']=str(f);r['_scenario']=s;r['_role']=role
    if r['binary_sha256']!=variants[role]['sha256']:report['validation_errors'].append({'receipt':str(f),'error':'binary hash differs from manifest'})
    if r.get('variant_env')!=variants[role].get('env',{}):report['validation_errors'].append({'receipt':str(f),'error':'variant env differs or unavailable'})
    if r.get('validation_errors'):report['validation_errors'].append({'receipt':str(f),'errors':r['validation_errors']})
    if r['status']!='completed':report['validation_errors'].append({'receipt':str(f),'error':'Retained failed/timed-out workload'});allrows.append(r);continue
    if s['kind']=='harness':
     if len(r['harness'])!=1:report['validation_errors'].append({'receipt':str(f),'error':'missing/excess harness result'})
     else:
      h=r['harness'][0]
      required=['wall_ns','digest','jobs_checked','rounds','jobs_per_round','participants','pattern','iterations']
      if any(k not in h for k in required) or not isinstance(h.get('wall_ns'),int) or h['wall_ns']<=0:
       report['validation_errors'].append({'receipt':str(f),'error':'Invalid micro result schema'});allrows.append(r);continue
      r['inner_wall']=h['wall_ns']/1e9;r['digest']=h['digest']
      if h['participants']!=s.get('resolved_participants',s['budget']) or h['pattern']!=s['args'][2] or h['iterations']!=int(s['args'][5]):report['validation_errors'].append({'receipt':str(f),'error':'Micro requested/actual participants or shape differ'})
      if h['jobs_checked']!=int(s['args'][3])*int(s['args'][4])*(8 if s['args'][2]=='lanes' else 1) or h['jobs_checked']!=h['rounds']*h['jobs_per_round']:report['validation_errors'].append({'receipt':str(f),'error':'job count differs'})
    else:
     n=r.get('native')
     if not n or any(k not in n for k in ['ticks','initialChecksum','finalChecksum']) or not isinstance(n.get('run_ns'),int) or n['run_ns']<=0:
      report['validation_errors'].append({'receipt':str(f),'error':'Missing/invalid native result'});allrows.append(r);continue
     target=int(flag(s['args'],'--ticks'));initial=s.get('initial_tick',0);resumed=n['ticks']-initial
     if n['ticks']!=target or resumed!=target-initial or resumed<=0:report['validation_errors'].append({'receipt':str(f),'error':'Requested versus actual resumed ticks differ'});allrows.append(r);continue
     r['inner_wall']=n['run_ns']/1e9;r['digest']=(n['initialChecksum'],n['finalChecksum']);r['process_cpu_per_tick']=r['total_cpu_seconds']/resumed;r['inner_ticks_per_second']=resumed/r['inner_wall']
     if '--benchmark-warmup' in s['args'] or '--telemetry' in s['args']:report['validation_errors'].append({'receipt':str(f),'error':'wrong timing loop/telemetry'})
    allrows.append(r)
 label=manifestfile.stem;report['cohorts'][label]={'expected':sum(s.get('repeats',2) for s in manifest['scenarios'])*len(variants),'retained':len(allrows),'missing':missing,'complete':not missing,'roles':list(variants),'role_restrictions':{k:v.get('design_claims',{}) for k,v in variants.items()},'statuses':dict(collections.Counter(r['status'] for r in allrows))}
 groups=collections.defaultdict(list)
 for r in allrows:
  s=r['_scenario'];seed=flag(s['args'],'--game-seed');save=flag(s['args'],'--load-game')
  family=re.sub(r'-'+re.escape(seed)+r'(?=-n)', '',s['id']) if seed else s['id'];unit=seed or save or s['id'];groups[family].append((unit,r))
 for family,entries in groups.items():
  lookup=collections.defaultdict(list)
  for unit,r in entries:lookup[r['_role'],unit].append(r)
  pairs=[('baseline',role) for role in variants if role!='baseline']+[('wake-only',role) for role in variants if role not in ['baseline','wake-only']]
  if 'local-completion-steal' in variants and 'steal' in variants:pairs.append(('steal','local-completion-steal'))
  for ref,cand in pairs:
   units=sorted(u for role,u in lookup if role==ref and (cand,u) in lookup);good=[];excluded=[]
   for u in units:
    aa=lookup[cand,u];bb=lookup[ref,u]
    if len(aa)!=len(bb) or len(aa)!=aa[0]['_scenario'].get('repeats',2) or any(r['status']!='completed' or 'inner_wall' not in r for r in aa+bb):excluded.append(u);continue
    if len({str(r['digest']) for r in aa+bb})!=1:report['validation_errors'].append({'family':family,'unit':u,'error':'digest/initial-final checksums differ across compared variants'});continue
    good.append(u)
   if not good:continue
   sample=lookup[ref,good[0]][0];kind=sample['_scenario']['kind'];r={'cohort':label,'family':family,'kind':kind,'reference':ref,'candidate':cand,'paired_units':good,'excluded_units':excluded,'metrics':{}}
   for metric in ['inner_wall','total_cpu_seconds','system_cpu_seconds','voluntary_context_switches','involuntary_context_switches','peak_rss_kib']+(['process_cpu_per_tick','inner_ticks_per_second'] if kind=='game' else []):
    a=[statistics.mean(x[metric] for x in lookup[cand,u]) for u in good];b=[statistics.mean(x[metric] for x in lookup[ref,u]) for u in good]
    m={'reference_mean':statistics.mean(b),'candidate_mean':statistics.mean(a),'ratio_of_means_change_percent':pct(sum(a),sum(b))}
    if kind=='game' and len(good)>1:
     vals=[pct(sum(a[i] for i in ix),sum(b[i] for i in ix)) for ix in itertools.product(range(len(good)),repeat=len(good))];m['paired_independent_seed_bootstrap95']=[quantile(vals,.025),quantile(vals,.975)]
    elif kind=='game':
     aa=sorted(lookup[cand,good[0]],key=lambda x:x['case']['repeat']);bb=sorted(lookup[ref,good[0]],key=lambda x:x['case']['repeat']);a=[x[metric] for x in aa];b=[x[metric] for x in bb];n=len(a);vals=[pct(sum(a[i] for i in ix),sum(b[i] for i in ix)) for ix in itertools.product(range(n),repeat=n)];m['conditional_timing_repeat_bootstrap95']=[quantile(vals,.025),quantile(vals,.975)]
    else:
     aa=sorted(lookup[cand,good[0]],key=lambda x:x['case']['repeat']);bb=sorted(lookup[ref,good[0]],key=lambda x:x['case']['repeat']);m['paired_repeat_changes']=[pct(x[metric],y[metric]) for x,y in zip(aa,bb)];m['uncertainty']='No population CI: two timing repeats of one fixed micro workload.'
    r['metrics'][metric]=m
   r['qualification']='Independent seeds are the units only for generated game-seed families; mature/small64 one unit has timing-only uncertainty. Population precision at two seeds is weak. Micro gains do not establish game improvement. Process CPU/tick includes startup/load/final checksum; inner TPS uses resumed ticks. RSS is wait4 process high-water mark, potentially censored by inherited Python parent floor for tiny micro workloads.';report['comparisons'].append(r)
report['finalized']=bool(report['cohorts']) and not report['missing_manifests'] and all(c['complete'] for c in report['cohorts'].values());report['valid']=not report['validation_errors']
(output/'pilot-analysis.json').write_text(json.dumps(report,indent=2)+'\n');parts=['<!doctype html><meta charset="utf-8"><title>Coordination pilot audit</title><style>body{font:16px system-ui;margin:30px}table{border-collapse:collapse}td,th{border:1px solid #bbb;padding:6px}</style><h1>Coordination pilot</h1><p>'+html.escape(report['scope'])+'</p><table><tr><th>Family<th>Pair<th>Units<th>Innerwallchange<th>ProcessCPUchange']
for r in report['comparisons']:parts.append('<tr><td>'+r['family']+'<td>'+r['reference']+'→'+r['candidate']+'<td>'+str(len(r['paired_units']))+''.join(f"<td>{r['metrics'][k]['ratio_of_means_change_percent']:+.2f}%" for k in ['inner_wall','total_cpu_seconds']))
parts.append('</table><pre>'+html.escape(json.dumps({'cohorts':report['cohorts'],'errors':report['validation_errors'],'finalized':report['finalized']},indent=2))+'</pre>');(output/'pilot-analysis.html').write_text('\n'.join(parts));print('Cohorts',len(report['cohorts']),'comparisons',len(report['comparisons']),'finalized',report['finalized'])

if report['validation_errors']:raise SystemExit(1)
