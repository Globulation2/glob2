from pathlib import Path
import json,math,sys
import numpy as np
root=Path(sys.argv[1] if len(sys.argv)>1 else 'artifacts/resource-growth/remaining/generated-delay')
B=100000;delays=[1,2,3,4,8,12,16];materials={'wheat':1,'wood':0,'algae':4};rng=np.random.default_rng(713)
results=[];boot_groups={};failures=[];coverage=[];aggregate_exclusions=[]
# Shared seed IDs are resampled jointly across generators to retain any correlation.
draws=rng.integers(0,20,size=(B,20));joint_weights=np.zeros((B,20),dtype=np.int16)
for column in range(20):np.add.at(joint_weights,(np.arange(B),draws[:,column]),1)
expected={'river','swamp','crater-lakes','islands','rain-shadow','old-growth','braided-river','fjord-continent','stone-highlands','tidal-flats','canals','continents'}
assert {p.name for p in (root/'branch').iterdir() if (p/'results.json').exists()}==expected
def endpoint(row,tick,flush=False):
 if flush:return row['terminal_flush']
 if tick==4096:return row['final']
 return next(c['counts'] for c in row['checkpoints'] if c['tick']==tick)
def sign(d):
 neg=int(np.count_nonzero(d<0));pos=int(np.count_nonzero(d>0));n=neg+pos
 p=min(1.,2*sum(math.comb(n,k) for k in range(min(neg,pos)+1))/2**n) if n else 1.
 return neg,pos,p
for directory in sorted((root/'branch').iterdir()):
 if not directory.is_dir() or not (directory/'results.json').exists():continue
 name=directory.name;m=json.loads((root/'master-verified'/name/'results.json').read_text());d=json.loads((directory/'results.json').read_text());
 assert m['ticks']==d['ticks']==4096;assert d['delays']==delays
 failures.extend(m['generation_failures']);by={(r['delay'],r['variant'],r['seed']):r for r in d['samples']};master={r['seed']:r for r in m['samples']};seeds=sorted(master)
 assert len(by)==len(seeds)*2*len(delays)
 refused={r['seed'] for r in m['generation_failures']}
 assert set(seeds)|refused==set(range(1,21));assert not(set(seeds)&refused)
 assert {r['seed'] for r in d['generation_failures']}==refused
 for delay in delays:
  for seed in seeds:
   a=by[delay,'owner',seed];b=by[delay,'shared',seed]
   assert a['initial']==b['initial']==master[seed]['initial'];assert a['final']==b['final'];assert a['checkpoints']==b['checkpoints'];assert a['terminal_flush']==b['terminal_flush'];assert a['statistics']==b['statistics']
 # Resample whole seed pairs, preserving associations across delays and materials.
 weights=joint_weights[:,np.array(seeds)-1];weight_sum=weights.sum(axis=1);assert np.all(weight_sum>0)
 coverage.append({'generator':name,'pairs':len(seeds),'width':m['samples'][0]['width'],'height':m['samples'][0]['height'],'runs':len(by)+len(master)})
 for tick in [512,4096]:
  for flush in ([False,True] if tick==4096 else [False]):
   for comparator in ['delay8','master']:
    for material,slot in materials.items():
     initial=np.array([master[s]['initial'][0][slot] for s in seeds],float)
     baseline_rows=[by[8,'owner',s] if comparator=='delay8' else master[s] for s in seeds]
     a=np.array([endpoint(r,tick,flush and comparator=='delay8')[0][slot] for r in baseline_rows],float)
     for delay in delays:
      arm=[by[delay,'owner',s] for s in seeds];b=np.array([endpoint(r,tick,flush)[0][slot] for r in arm],float)
      am=(weights@a)/weight_sum;bm=(weights@b)/weight_sum;im=(weights@initial)/weight_sum
      valid=am>0;boot=(bm[valid]/am[valid]-1)*100 if valid.any() else np.array([])
      growth_valid=(am-im)>0;growthboot=((bm[growth_valid]-im[growth_valid])/(am[growth_valid]-im[growth_valid])-1)*100
      neg,pos,p=sign(b-a)
      row={'generator':name,'ticks':tick,'endpoint':'flushed' if flush else 'normal','comparator':comparator,'material':material,'delay':delay,'pairs':len(seeds),'initial_mean':float(initial.mean()),'baseline_final_mean':float(a.mean()),'candidate_final_mean':float(b.mean()),'absolute_stock_change':float((b-a).mean()),'stock_percent':float((b.mean()/a.mean()-1)*100) if a.mean() else None,'stock_ci95':np.quantile(boot,[.025,.975]).tolist() if len(boot) else None,'baseline_growth_mean':float((a-initial).mean()),'candidate_growth_mean':float((b-initial).mean()),'growth_percent':float(((b-initial).mean()/(a-initial).mean()-1)*100) if (a-initial).mean()>0 else None,'growth_ci95':np.quantile(growthboot,[.025,.975]).tolist() if len(growthboot) else None,'lower_pairs':neg,'higher_pairs':pos,'sign_p':p,'baseline_tiles_mean':float(np.mean([endpoint(r,tick,flush and comparator=='delay8')[1][slot] for r in baseline_rows])),'candidate_tiles_mean':float(np.mean([endpoint(r,tick,flush)[1][slot] for r in arm]))}
      results.append(row)
      if len(boot)==B:
       key=(tick,flush,comparator,material,delay);boot_groups.setdefault(key,[]).append((row['stock_percent'],boot))
      else:aggregate_exclusions.append({'generator':name,'ticks':tick,'flush':flush,'comparator':comparator,'material':material,'delay':delay,'reason':'Zero stock denominator in at least one bootstrap resample','valid_resamples':len(boot)})
# Treat each horizon/endpoint/comparator as a separate declared family.
for tick in [512,4096]:
 for ep in ['normal','flushed']:
  for comp in ['delay8','master']:
   family=[r for r in results if r['ticks']==tick and r['endpoint']==ep and r['comparator']==comp and not(comp=='delay8' and r['delay']==8)]
   prior=0
   for rank,r in enumerate(sorted(family,key=lambda r:r['sign_p'])):
    prior=max(prior,min(1,r['sign_p']*(len(family)-rank)));r['holm_sign_p']=prior
aggregates=[];aggregate_boots={}
for (tick,flush,comp,material,delay),rows in boot_groups.items():
 estimates=[x[0] for x in rows];boot=np.mean([x[1] for x in rows],axis=0);aggregate_boots[tick,flush,comp,material,delay]=boot
 aggregates.append({'ticks':tick,'endpoint':'flushed' if flush else 'normal','comparator':comp,'material':material,'delay':delay,'generators':len(rows),'equal_generator_mean_percent':float(np.mean(estimates)),'ci95':np.quantile(boot,[.025,.975]).tolist(),'generator_range_percent':[min(estimates),max(estimates)]})
# Approximate simultaneous95% bands: bootstrap maximum absolute standardized
# deviation across all materials/non-reference delays within each aggregate row.
for tick in [512,4096]:
 for flush in [False,True]:
  for comp in ['delay8','master']:
   family=[r for r in aggregates if r['ticks']==tick and r['endpoint']==('flushed' if flush else 'normal') and r['comparator']==comp]
   maxz=np.zeros(B)
   for r in family:
    b=aggregate_boots[tick,flush,comp,r['material'],r['delay']];sd=float(b.std(ddof=1));r['bootstrap_se']=sd
    if sd>0:maxz=np.maximum(maxz,np.abs((b-r['equal_generator_mean_percent'])/sd))
   critical=float(np.quantile(maxz,.95))
   for r in family:
    width=critical*r['bootstrap_se'];mean=r['equal_generator_mean_percent'];r['simultaneous_ci95']=[mean-width,mean+width];r['simultaneous_critical']=critical
report={'resamples':B,'seed':713,'coverage':coverage,'generation_failures':failures,'aggregate_exclusions':aggregate_exclusions,'method':'Paired seed bootstrap,100000 joint seed-ID draws across fixed generators; refused seeds are omitted and remaining weights renormalized within that generator. Normal4096-tick comparisons versus delay8 are primary;512 checkpoints and terminal flush are secondary/diagnostic. Stock/growth intervals are percentile95%, unadjusted. Exact paired directional sign tests receive Holm correction across generator/material/delay comparisons within each horizon/endpoint/comparator. Aggregate simultaneous95% bands use the bootstrap maximum absolute standardized deviation over all materials and non-reference delays within each horizon/endpoint/comparator family. Equal-generator aggregate gives each represented generator equal weight; generators themselves are fixed, not sampled. Owner/shared are verification copies, never independent samples.','results':results,'aggregates':aggregates}
(root/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
lines=['# Delay ablation on current-master generated worlds','', '| Delay | Wheat vs delay8 | Wood vs delay8 | Algae vs delay8 |','|---:|---:|---:|---:|']
for delay in delays:
 row=[r for r in aggregates if r['ticks']==4096 and r['endpoint']=='normal' and r['comparator']=='delay8' and r['delay']==delay]
 values={r['material']:r for r in row};lines.append('| '+str(delay)+' | '+' | '.join(f"{values[m]['equal_generator_mean_percent']:+.3f}% [{values[m]['ci95'][0]:+.3f}, {values[m]['ci95'][1]:+.3f}]" for m in materials)+' |')
lines+=['','Equal weight per generator;100000 paired-bootstrap95% intervals. Final stocks,4096 ticks.','', '| Delay | Wheat vs master | Wood vs master | Algae vs master |','|---:|---:|---:|---:|']
for delay in delays:
 values={r['material']:r for r in aggregates if r['ticks']==4096 and r['endpoint']=='normal' and r['comparator']=='master' and r['delay']==delay}
 lines.append('| '+str(delay)+' | '+' | '.join(f"{values[m]['equal_generator_mean_percent']:+.3f}% [{values[m]['ci95'][0]:+.3f}, {values[m]['ci95'][1]:+.3f}]" for m in materials)+' |')
(root/'tables.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
