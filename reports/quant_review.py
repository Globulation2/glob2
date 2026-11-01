"""Independent completed-cohort audit; no refit/tuning/new matches."""
import bisect,json,math,random,shutil,statistics
from collections import defaultdict,Counter
from pathlib import Path
from tools.tournaments.results import Results
ROOT=Path(__file__).resolve().parent
assert shutil.disk_usage(ROOT).free>2*1024**3
source=Results(ROOT/'results');report=json.loads((ROOT/'assessment.json').read_text());records=list(source);assert len(records)==288 and all(r['category']=='success' for r in records)
idx={(r['job']['labels']['pair'],r['job']['labels']['variant']):r for r in records}
rows=[];checks=Counter()
def mean(r):
 t=next(t for t in r['result']['teams'] if t['team']==0);p=t['win_probability'];return p['area_twice_permille_ticks']/(2000*(p['end_tick']-p['start_tick']))
def prefix(r,end):
 h=r['result']['teams'][0]['win_probability_history'];area=0.
 for (ta,pa),(tb,pb) in zip(h,h[1:]):
  if ta>=end:break
  stop=min(end,tb);pstop=pa+(pb-pa)*(stop-ta)/(tb-ta);area+=(pa+pstop)*(stop-ta)/2000
 return area/end
for r in records:
 for t in r['result']['teams']:
  h=t['win_probability_history'];p=t['win_probability'];assert h[0][0]==0 and h[-1][0]==r['result']['ticks'];assert all(a[0]<b[0] for a,b in zip(h,h[1:]));assert sum((a[1]+b[1])*(b[0]-a[0]) for a,b in zip(h,h[1:]))==p['area_twice_permille_ticks'];checks['exact_complete_team_histories']+=1
for pair in dict.fromkeys(p for p,a in idx):
 row={'pair':pair,**{k:idx[pair,'baseline']['job']['labels'][k] for k in ('format','block')}}
 for a,b in [('adaptive','baseline'),('single','baseline'),('adaptive','single')]:row[a+'_minus_'+b]=mean(idx[pair,a])-mean(idx[pair,b])
 rows.append(row)
def pct(x,p):
 x=sorted(x);pos=p*(len(x)-1);lo=math.floor(pos);hi=math.ceil(pos);return x[lo]+(x[hi]-x[lo])*(pos-lo)
def ci(rr,key):
 blocks=defaultdict(list)
 for r in rr:blocks[r['block']].append(r[key])
 assert len(blocks)==16 and len({len(x) for x in blocks.values()})==1
 keys=sorted(blocks);blockmeans={k:statistics.mean(blocks[k]) for k in keys};rng=random.Random(19)
 boot=[statistics.mean(blockmeans[rng.choice(keys)] for _ in range(16)) for _ in range(10000)]
 return {'mean':statistics.mean(r[key] for r in rr),'interval95':[pct(boot,.025),pct(boot,.975)],'seed_blocks':16,'draws':10000,'seed':19}
out={'scope':'all fresh corrected v2 games only; no aborted v1 outcome used','checks':dict(checks),'contrasts':{},'equality':{},'higher_capacity':{},'changed_adaptive_pairs':[],'cancel_records':[]}
for f in ('1v1','ffa'):
 rr=[r for r in rows if r['format']==f];out['contrasts'][f]={k:ci(rr,k) for k in ('adaptive_minus_baseline','single_minus_baseline','adaptive_minus_single')}
 for k,c in out['contrasts'][f].items():
  canonical=report['by_format'][f]['contrasts'][k];assert abs(c['mean']-canonical['mean'])<1e-14 and all(abs(a-b)<1e-14 for a,b in zip(c['interval95'],canonical['interval95']))
 e=Counter();chosen=Counter();exposed=[]
 for row in rr:
  pair=row['pair'];a=idx[pair,'adaptive']['result'];b=idx[pair,'single']['result'];same=all(t['win_probability_history']==next(x for x in b['teams'] if x['team']==t['team'])['win_probability_history'] for t in a['teams'])
  e['pairs']+=1;e['same_initial_checksum']+=a['initialChecksum']==b['initialChecksum'];e['same_final_checksum']+=a['finalChecksum']==b['finalChecksum'];e['same_ticks']+=a['ticks']==b['ticks'];e['same_all_team_probability_histories']+=same
  if not same:
   H=min(a['ticks'],b['ticks']);common=prefix(idx[pair,'adaptive'],H)-prefix(idx[pair,'single'],H)
   out['changed_adaptive_pairs'].append({'pair':pair,'adaptive_minus_single':row['adaptive_minus_single'],'common_horizon_ticks':H,'common_horizon_effect':common,'tail_normalization_component':row['adaptive_minus_single']-common,'single_ticks':b['ticks'],'adaptive_ticks':a['ticks']})
 for r in report['telemetry']:
  if r['format']==f and r['arm']=='adaptive':
   c=r['subject']['chosen_capacity_observations'];chosen.update(c)
   if any(int(k)>1 and v>0 for k,v in c.items()):exposed.append({'pair':r['pair'],'chosen_capacity_observations':c,'director_inferred':r['subject']['inferred_capacity_director_observations']})
 out['equality'][f]=dict(e);out['higher_capacity'][f]={'chosen_counts':dict(chosen),'exposed_games':exposed,'games':len(rr),'higher_capacity_events':sum(v for k,v in chosen.items() if int(k)>1)}
for r in report['telemetry']:
 if r['subject']['counts'].get('cancel_issued'):
  rec=idx[r['pair'],r['arm']]
  with source.open_artifact(rec,'stdout.log') as log:
   events=[line.rstrip('\n') for line in log if '\tprestige_cancel_' in line]
  out['cancel_records'].append({'pair':r['pair'],'arm':r['arm'],'job':r['job'],'events':events})
out['coverage']={'team_streams':sum(len(r['all_teams']) for r in report['telemetry']),'start_gap_ticks':sorted({r['subject']['director_start_gap_ticks'] for r in report['telemetry']}),'maximum_subject_director_tail_hold_ticks':max(r['subject']['director_tail_gap_ticks'] for r in report['telemetry']),'race_alignment_missing_records':sum(t['counts'].get('director_school_checks_unaligned_race',0) for r in report['telemetry'] for t in r['all_teams']),'helper_reconstruction_observations':report['helper_facts_available_records'],'helper_fields_unavailable_records':report['helper_facts_unavailable_records'],'terminal_safety_flags':sum(t['potential_terminal_unsafe_own_gain'] for r in report['telemetry'] for t in r['all_teams']),'unresolved_cancellation_gids':sum(r['subject']['unresolved_cancellation_gids'] for r in report['telemetry']),'contract_violations':report['telemetry_contract_violations']}
text=json.dumps(out,indent=2)+'\n';assert len(text.encode())<2*1024**2
(ROOT/'independent-review.json').write_text(text);print(text)
