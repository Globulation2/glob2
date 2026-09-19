"""Normalize the adopted tower cohort's hospital pressure by live army size."""
import argparse,collections,csv,gzip,json,pathlib,re
p=argparse.ArgumentParser();p.add_argument('study',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir(exist_ok=True,parents=True)
source=list(csv.DictReader((a.study/'hospital-spikes/pressure-samples.csv').open()));wanted=collections.defaultdict(dict)
for r in source:
 if r['variant']=='towers-lazy':wanted[(r['cohort'],r['case'])][int(r['tick'])]=r
all_samples=[];games=[]
def mean(v):return sum(v)/len(v) if v else None
def median(v):
 v=sorted(v);n=len(v);return (v[(n-1)//2]+v[n//2])/2 if n else None
for cohort,folder in [('128','lazy-towers'),('256','large-ffa')]:
 design=json.loads((a.study/folder/'design.json').read_text())
 for case in design['cases']:
  team=case['players'].index('maxima');defense={};hospitals={};ticks=wanted.get((cohort,case['id']),{})
  for line in gzip.open(a.study/folder/'towers-lazy'/case['id']/'stdout.log.gz','rt'):
   if line.startswith(f'GLOB2_DEFENCE team={team} '):
    d={k:int(v) for k,v in re.findall(r'([^\s=]+)=(-?\d+)(?=\s|$)',line)}
    if d['tick'] in ticks:defense[d['tick']]=d
   elif line.startswith(f'GLOB2_MEASURE team={team} '):
    tick=int(re.search(r'\btick=(\d+)',line)[1])
    if tick in ticks:hospitals[tick]={int(k):int(v) for k,v in re.findall(r'\bbuildings_2_(\d+)=(\d+)',line)}
  records=[]
  for tick,r in sorted(ticks.items()):
   d=defense[tick];army=d['home']+d['away']+d['field'];assert d['hurt']==int(r['hurt_warriors']);assert d['hospital_seats']==int(r['total_beds'])
   h=hospitals.get(tick);assert h is not None,(case['id'],tick)
   assert sum(h.keys())==15 and len(h)==6,(case['id'],h)
   total=sum(h.values());complete=sum(v for k,v in h.items() if k%2);sites=total-complete;floor=min(8,(army+7)//8)
   rec=dict(cohort=cohort,case=case['id'],seed=case['seed'],tick=tick,warriors=army,hurt=d['hurt'],beds=d['hospital_seats'],missing=int(r['missing_beds']),hospitals_complete=complete,hospitals_sites=sites,hospitals_total=total,demographic_floor=floor,beds_per_warrior=d['hospital_seats']/army if army else None,hurt_per_warrior=d['hurt']/army if army else None,missing_per_warrior=int(r['missing_beds'])/army if army else None)
   records.append(rec);all_samples.append(rec)
  valid=[r for r in records if r['warriors']];short=[r for r in valid if r['missing']];positive=[r for r in short if r['beds']>0];peak=max(short,key=lambda r:r['missing']) if short else None
  row=dict(cohort=cohort,case=case['id'],seed=case['seed'],pressure_samples=len(valid),short_samples=len(short),mean_beds_per_warrior=mean([r['beds_per_warrior'] for r in valid]),mean_hurt_per_warrior=mean([r['hurt_per_warrior'] for r in valid]),mean_missing_per_warrior_when_short=mean([r['missing_per_warrior'] for r in short]),mean_beds_per_warrior_when_short=mean([r['beds_per_warrior'] for r in short]),mean_hurt_per_warrior_when_short=mean([r['hurt_per_warrior'] for r in short]),mean_missing_per_warrior_when_short_with_beds=mean([r['missing_per_warrior'] for r in positive]),short_fraction_below_demographic_floor=mean([r['hospitals_total']<r['demographic_floor'] for r in short]),short_fraction_at_hospital_cap=mean([r['hospitals_total']>=8 for r in short]),short_fraction_with_hospital_sites=mean([r['hospitals_sites']>0 for r in short]),peak_warriors=peak['warriors'] if peak else None,peak_missing_fraction=peak['missing_per_warrior'] if peak else None,peak_hospitals=peak['hospitals_total'] if peak else None,peak_beds=peak['beds'] if peak else None)
  games.append(row)
report={'definition':'Equal-weight per-game ratios for adopted towers only. Army = live home + away + field warriors. Shortage-conditioned metrics use affected games only. Building totals include construction/upgrade sites; not unissued reservations.','groups':{}}
for cohort in ['128','256']:
 rs=[r for r in games if r['cohort']==cohort];g={'games':len(rs),'games_with_pressure':sum(r['pressure_samples']>0 for r in rs),'games_with_shortages':sum(r['short_samples']>0 for r in rs)}
 for k in ['mean_beds_per_warrior','mean_hurt_per_warrior','mean_missing_per_warrior_when_short','mean_beds_per_warrior_when_short','mean_hurt_per_warrior_when_short','mean_missing_per_warrior_when_short_with_beds','short_fraction_below_demographic_floor','short_fraction_at_hospital_cap','short_fraction_with_hospital_sites']:
  g[k]=mean([r[k] for r in rs if r[k] is not None])
 for k in ['peak_warriors','peak_missing_fraction','peak_hospitals','peak_beds']:g['median_'+k]=median(r[k] for r in rs if r[k] is not None)
 report['groups'][cohort]=g
for name,rows in [('per-warrior-games.csv',games),('per-warrior-samples.csv',all_samples)]:
 with (a.output/name).open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)
(a.output/'per-warrior-summary.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
