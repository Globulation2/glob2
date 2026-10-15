"""Balanced paired-seed MonteCarlo bootstrap; no enum8^8, no workloads."""
from pathlib import Path
import json,random,statistics,datetime
O=Path(__file__).resolve().parent;m=json.loads((O/'manifest.json').read_text());rows=[];units=[]
def pct(a,b):return 100*(a/b-1)
def quantile(v,p):
 v=sorted(v);x=(len(v)-1)*p;i=int(x);return v[i]+(v[min(i+1,len(v)-1)]-v[i])*(x-i)
for s in m['scenarios']:
 seed=s['args'][s['args'].index('--game-seed')+1];pair={};digests=set()
 for rep in range(2):
  for v in m['variants']:
   q=O/'timing'/f"{s['id']}-{v['role']}-r{rep}"/'receipt.json';r=json.loads(q.read_text());n=json.loads((q.parent/'native/result.json').read_text());assert r['native']==n and r['status']=='completed' and not r['validation_errors'] and r['returncode']==0
   assert r['case']['scenario']==s and r['case']['variant']==v and r['binary_sha256']==v['sha256'] and r['variant_env']==v['env'] and r['scenario_env']==s['env'] and r['fixture_sha256']==s['fixture_sha256']
   assert r['command']==['taskset','-c',','.join(map(str,s['allocation'])),v['binary'],'game','run',*s['args'],'--output-dir',str(q.parent/'native')]
   assert n['ticks']-s['initial_tick']==4096 and n['resource_plan']['compute_participants']==4 and n['resource_plan']['eligible_threads']==32 and n['resource_plan']['gradient_backend']=='cpu' and n['gradient_backend']=='cpu';assert r['finished_monotonic']>=r['started_monotonic']
   digests.add((n['initialChecksum'],n['finalChecksum']));row={'seed':seed,'role':v['role'],'repeat':rep,'ordinal':r['ordinal'],'started_monotonic':r['started_monotonic'],'finished_monotonic':r['finished_monotonic'],'wall':n['run_ns']/1e9,'cpu':r['total_cpu_seconds'],'sys':r['system_cpu_seconds'],'rss':r['peak_rss_kib'],'cpu_per_tick':r['total_cpu_seconds']/4096,'receipt':str(q)};rows.append(row);pair[v['role'],rep]=row
 assert len(digests)==1
 ordered=sorted(pair.values(),key=lambda x:x['ordinal']);assert [x['role'] for x in ordered]==['baseline','wake-only','wake-only','baseline'];assert all(b['finished_monotonic']<=a['started_monotonic'] for b,a in zip(ordered,ordered[1:]))
 units.append({'seed':seed,'means':{role:{metric:statistics.mean(pair[role,i][metric] for i in [0,1]) for metric in ['wall','cpu','sys','rss','cpu_per_tick']} for role in ['baseline','wake-only']},'repeat_changes':{metric:[pct(pair['wake-only',i][metric],pair['baseline',i][metric]) for i in [0,1]] for metric in ['wall','cpu','sys','rss','cpu_per_tick']}})
assert len(rows)==32 and len(units)==8
rng=random.Random(20261015);draws=[[rng.randrange(8) for _ in range(8)] for _ in range(10000)];metrics={}
for metric in ['wall','cpu','sys','rss','cpu_per_tick']:
 b=[u['means']['baseline'][metric] for u in units];w=[u['means']['wake-only'][metric] for u in units];vals=[pct(sum(w[i] for i in d),sum(b[i] for i in d)) for d in draws];metrics[metric]={'baseline_mean':statistics.mean(b),'wake_mean':statistics.mean(w),'ratio_of_seed_repeat_means_change_percent':pct(sum(w),sum(b)),'paired_independent_seed_MC10000_percentile95':[quantile(vals,.025),quantile(vals,.975)],'per_seed_change_percent':[pct(ww,bb) for ww,bb in zip(w,b)],'mean_of_seed_ratios_percent':statistics.mean(pct(ww,bb) for ww,bb in zip(w,b))}
out={'state':'validated','runs':32,'independent_seeds':8,'nested_repeats':2,'actual_order':'B,W,W,B everyseed; nooverlap, recordedordinal+monotonic/UTC timestamps','metrics':metrics,'units':units,'rows':rows,'bootstrap':{'method':'paired independentseedunits afteraveragingnestedrepeats','draws':10000,'random_seed':20261015,'limits':'Conditional8predeclaredseedssinglemap/budget/version; populationtails/generalpolicy unestablished; no bootstrapoverindividualtimingrepeats.'},'scope':'Latest054 CPU PGmixedN4 all32 only. WholeprocessCPU includesstartup/finalchecksum;wall isinner run. OldBfirstdata unchanged; nocorrectionpooledacrosscampaigns.'};(O/'analysis.json').write_text(json.dumps(out,indent=2)+'\n');print('Validated32balancedruns8seedMC10000')
