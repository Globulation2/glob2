"""Hospital shortfalls from retained defense samples; one row per game."""
import argparse,collections,csv,gzip,json,math,pathlib,random,re
p=argparse.ArgumentParser();p.add_argument('study',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def fields(line):return {k:int(v) for k,v in re.findall(r'([^\s=]+)=(-?\d+)(?=\s|$)',line)}
def mean(x):return sum(x)/len(x) if x else None
def quantile(x,q):
 if not x:return None
 x=sorted(x);at=(len(x)-1)*q;i=int(at);return x[i]+(x[min(i+1,len(x)-1)]-x[i])*(at-i)
rows=[];samples=[]
for cohort,folder,variants in [('128','confirmation',['baseline']),('128','lazy-towers',['towers-lazy']),('256','large-ffa',['baseline','towers-lazy'])]:
 design=json.loads((a.study/folder/'design.json').read_text())
 for variant in variants:
  for case in design['cases']:
   source=a.study/folder/variant/case['id'];assert json.loads((source/'execution.json').read_text())['exit']==0
   team=case['players'].index('maxima');states={};visible=set()
   for line in gzip.open(source/'stdout.log.gz','rt'):
    if not line.startswith(('GLOB2_DEFENCE ','GLOB2_DEFENCE_CONTACT ')):continue
    d=fields(line)
    if d.get('team')!=team:continue
    if line.startswith('GLOB2_DEFENCE_CONTACT '):
     if d.get('visible') and d.get('colony'):visible.add(d['tick'])
    else:states[d['tick']]=d
   exposed=[states[t] for t in sorted(visible) if t in states]
   deficits=[max(0,d['hurt']-d['hospital_seats']) for d in exposed];positive=[x for x in deficits if x]
   peak=max(exposed,key=lambda d:d['hurt']-d['hospital_seats']) if exposed else None
   episode_peaks=[];last=None
   for d,short in zip(exposed,deficits):
    samples.append(dict(cohort=cohort,variant=variant,case=case['id'],seed=case['seed'],tick=d['tick'],hurt_warriors=d['hurt'],total_beds=d['hospital_seats'],occupied_beds=d['hospital_inside'],missing_beds=short,twice_wounded_gap=max(0,2*d['hurt']-d['hospital_seats'])))
    if short:
     if last is None or d['tick']!=last+512:episode_peaks.append(short)
     else:episode_peaks[-1]=max(episode_peaks[-1],short)
     last=d['tick']
    else:last=None
   rows.append(dict(cohort=cohort,variant=variant,case=case['id'],seed=case['seed'],generator=case['generator'],format='ffa' if len(case['players'])==4 else 'duel',visible_pressure_samples=len(exposed),shortfall_samples=len(positive),shortfall_fraction=len(positive)/len(exposed) if exposed else None,mean_missing_during_shortfall=mean(positive),p90_missing_during_shortfall=quantile(positive,.9),peak_missing=max(deficits) if deficits else None,shortage_episodes=len(episode_peaks),median_episode_peak=quantile(episode_peaks,.5),peak_tick=peak['tick'] if positive else None,peak_hurt=peak['hurt'] if positive else None,peak_beds=peak['hospital_seats'] if positive else None,peak_occupied=peak['hospital_inside'] if positive else None,peak_twice_wounded_gap=max([max(0,2*d['hurt']-d['hospital_seats']) for d in exposed],default=None)))
report={'definition':'At sampled visible colony pressure: max(0, team-wide hurt warriors - completed hospital seats). Not a queue length; includes warriors already inside hospitals.','sampling':'512 ticks; peaks between samples may be larger. One-game summaries avoid treating ticks as independent.','groups':{}}
for cohort in ['128','256']:
 for variant in ['baseline','towers-lazy']:
  group=[r for r in rows if r['cohort']==cohort and r['variant']==variant];exposed=[r for r in group if r['visible_pressure_samples']];short=[r for r in exposed if r['shortfall_samples']];key=cohort+'/'+variant
  g=dict(games=len(group),exposed_games=len(exposed),games_with_shortfall=len(short),mean_game_shortfall_fraction=mean([r['shortfall_fraction'] for r in exposed]),mean_game_missing_when_short=mean([r['mean_missing_during_shortfall'] for r in short]),median_game_mean_missing_when_short=quantile([r['mean_missing_during_shortfall'] for r in short],.5),median_peak_among_short_games=quantile([r['peak_missing'] for r in short],.5),p90_peak_among_short_games=quantile([r['peak_missing'] for r in short],.9),maximum_missing=max([r['peak_missing'] for r in exposed],default=None),median_peak_among_exposed_games=quantile([r['peak_missing'] for r in exposed],.5),median_peak_twice_wounded_gap=quantile([r['peak_twice_wounded_gap'] for r in short],.5))
  # Bootstrap whole numeric seeds, preserving related maps/opponents. Descriptive
  # interval for mean per-game shortage magnitude among games that experienced it.
  blocks=collections.defaultdict(list)
  for r in short:blocks[r['seed']].append(r['mean_missing_during_shortfall'])
  rng=random.Random(20260919);draws=[];block_values=list(blocks.values())
  if block_values:
   for _ in range(10000):draws.append(mean([x for block in rng.choices(block_values,k=len(block_values)) for x in block]))
  g['mean_missing_bootstrap_ci95']=[quantile(draws,.025),quantile(draws,.975)];g['shortfall_seed_blocks']=len(blocks)
  g['largest_shortfalls']=sorted(short,key=lambda r:(-r['peak_missing'],r['case']))[:5]
  report['groups'][key]=g
for name,data in [('games.csv',rows),('pressure-samples.csv',samples)]:
 with (a.output/name).open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]),lineterminator='\n');w.writeheader();w.writerows(data)
(a.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:{a:b for a,b in v.items() if a!='largest_shortfalls'} for k,v in report['groups'].items()},indent=2))
