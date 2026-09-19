"""Analyze the complete frozen hospital ablation; no tick-level pseudo-replication."""
import argparse,collections,csv,gzip,json,math,pathlib,re
p=argparse.ArgumentParser();p.add_argument('batch',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
design=json.loads((a.batch/'design.json').read_text());rows=[];samples=[]
def mean(x):return sum(x)/len(x) if x else None
def fields(l):return {k:int(v) for k,v in re.findall(r'([^\s=]+)=(-?\d+)(?=\s|$)',l)}
for v in design['variants']:
 for case in design['cases']:
  source=a.batch/v/case['id'];execution=json.loads((source/'execution.json').read_text());assert execution['exit']==0
  result=json.loads((source/'result.json').read_text());assert result['status']=='completed';assert result['ticks']<=case['ticks'];assert result['game_seed']==case['game_seed'];assert [p['ai'] for p in result['players']]==case['players'];team=case['players'].index('maxima');states={};visible=set();last={};measurements={};labour={};configuration=None
  for l in gzip.open(source/'stdout.log.gz','rt'):
   if l.startswith('Maxima strategy:'):configuration=re.findall(r'military.hospital_[^,\s]+',l)
   if not l.startswith(('GLOB2_DEFENCE ','GLOB2_DEFENCE_CONTACT ','GLOB2_MEASURE ','GLOB2_LABOUR ')):continue
   d=fields(l)
   if d.get('team')!=team:continue
   if l.startswith('GLOB2_DEFENCE_CONTACT '):
    if d.get('visible') and d.get('colony'):visible.add(d['tick'])
   elif l.startswith('GLOB2_DEFENCE '):states[d['tick']]=d
   elif l.startswith('GLOB2_LABOUR '):labour=d
   else:last=d;measurements[d['tick']]=d
  assert configuration and last
  if v=='control':assert sorted(configuration)==sorted(['military.hospital_warrior_min=10','military.hospital_cap=8','military.hospital_units_per_building=15'])
  else:assert configuration==[f'military.hospital_beds_per_warrior_percent={v[4:]}']
  pressure=[]
  for t in sorted(visible):
   if t not in states:continue
   d=states[t];army=d['home']+d['away']+d['field'];missing=max(0,d['hurt']-d['hospital_seats'])
   rec=dict(variant=v,case=case['id'],stratum=case['stratum'],tick=t,warriors=army,hurt=d['hurt'],beds=d['hospital_seats'],missing=missing,missing_per_warrior=missing/army if army else 0)
   pressure.append(rec);samples.append(rec)
  eligible=[r for r in pressure if r['warriors']>0]
  new=sum(last.get(f'completed_0_2_{i}',0) for i in range(4));up1=last['completed_1_2_1'];up2=last['completed_1_2_2']
  outcome=result['teams'][team]['outcome']
  labour_measure=measurements[labour['tick']];worker_ticks=labour['b27']
  rows.append(dict(variant=v,case=case['id'],platform=execution.get('platform','Linux x86_64'),stratum=case['stratum'],seed=case['seed'],generator=case['generator'],outcome=outcome,win=int(outcome=='won'),loss=int(outcome=='lost'),ticks=result['ticks'],pressure_samples=len(eligible),shortage_fraction=mean([r['missing']>0 for r in eligible]),missing_per_warrior=mean([r['missing_per_warrior'] for r in eligible]),beds_per_warrior=mean([r['beds']/r['warriors'] for r in eligible]),peak_missing=max([r['missing'] for r in pressure],default=0),worker_deaths=last['deaths_0_0'],warrior_deaths=last['deaths_2_0'],buildings_destroyed=sum(n for k,n in last.items() if k.startswith('removed_0_')),hospital_new=new,hospital_upgrades=up1+up2,hospital_completed_resource_units=3*new+8*up1+8*up2,hp_restored=last['hpRestored'],worker_ticks=worker_ticks,worker_deaths_per_million_ticks=1e6*labour_measure['deaths_0_0']/worker_ticks if worker_ticks else None,worker_no_hospital_fraction=labour['b6']/worker_ticks if worker_ticks else None))
for case in design['cases']:
 assert len({r['platform'] for r in rows if r['case']==case['id']})==1
# Paired t intervals use the independent case as the observation. For secondary
# conditional pressure metrics use only cases exposed under both compared policies.
# scipy is used only for t quantiles; raw per-game rows remain dependency-free.
from scipy.stats import t as student_t
metrics=['win','loss','shortage_fraction','missing_per_warrior','beds_per_warrior','peak_missing','worker_deaths','warrior_deaths','buildings_destroyed','hospital_new','hospital_upgrades','hospital_completed_resource_units','worker_deaths_per_million_ticks','worker_no_hospital_fraction']
def interval(values):
 n=len(values);m=mean(values)
 if n<2:return dict(n=n,mean=m,ci95=None)
 se=math.sqrt(sum((x-m)**2 for x in values)/(n-1)/n);radius=float(student_t.ppf(.975,n-1))*se
 return dict(n=n,mean=m,ci95=[m-radius,m+radius])
report={'design':design,'groups':{}}
for stratum in ['all','small','large']:
 group=[r for r in rows if stratum=='all' or r['stratum']==stratum];summary={};comparisons={}
 for v in design['variants']:
  rs=[r for r in group if r['variant']==v]
  summary[v]=dict(games=len(rs),outcomes=dict(collections.Counter(r['outcome'] for r in rs)),exposed_games=sum(r['pressure_samples']>0 for r in rs),**{k:mean([r[k] for r in rs if r[k] is not None]) for k in metrics})
 control={r['case']:r for r in group if r['variant']=='control'}
 for v in design['variants'][1:]:
  rs=[r for r in group if r['variant']==v];comparison={}
  for k in metrics:comparison[k]=interval([r[k]-control[r['case']][k] for r in rs if r[k] is not None and control[r['case']][k] is not None])
  gains=sum(r['win']>control[r['case']]['win'] for r in rs);losses=sum(r['win']<control[r['case']]['win'] for r in rs);n=gains+losses
  comparison['win_discordance']=dict(gains=gains,losses=losses,p_exact=min(1,2*sum(math.comb(n,i) for i in range(min(gains,losses)+1))/2**n) if n else 1)
  comparisons[v]=comparison
 ordered=sorted(comparisons,key=lambda v:comparisons[v]['win_discordance']['p_exact']);adjusted=0
 for i,v in enumerate(ordered):
  adjusted=max(adjusted,min(1,(len(ordered)-i)*comparisons[v]['win_discordance']['p_exact']));comparisons[v]['win_discordance']['p_holm']=adjusted
 adjacent={}
 for low,high in zip(design['variants'][1:-1],design['variants'][2:]):
  base={r['case']:r for r in group if r['variant']==low};rs=[r for r in group if r['variant']==high]
  adjacent[high+'-'+low]={k:interval([r[k]-base[r['case']][k] for r in rs if r[k] is not None and base[r['case']][k] is not None]) for k in metrics}
 report['groups'][stratum]=dict(summary=summary,paired_vs_control=comparisons,exploratory_adjacent_ratios=adjacent)
for name,data in [('games.csv',rows),('pressure-samples.csv',samples)]:
 with (a.output/name).open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]),lineterminator='\n');w.writeheader();w.writerows(data)
(a.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['groups']['all'],indent=2))
