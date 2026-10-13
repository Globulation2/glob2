from pathlib import Path
import json,random,collections,hashlib
p=Path(__file__).resolve().parent;primary_rows=json.load(open(p/'match-summary.json'));time_rows={r['id']:r for r in json.load(open(p/'secondary-match-summary.json'))};groups=collections.defaultdict(lambda:collections.defaultdict(list))
# Fifteen reported/collected secondary endpoints and every one of36inspected subgroup endpoints share one conservative correction family.
secondary={'idle_fraction':(0,16),'filling_fraction':(1,16),'food_time_fraction':(2,16),'early_finish_fraction':(3,17),'lost_colonies_per_match':(4,17),'delivered_per_worker_tick':(5,15),'harvested_per_worker_tick':(6,15),'births_per_unit_tick':(7,14),'combat_deaths_per_unit_tick':(8,14),'starvation_deaths_per_match':(9,17),'combat_deaths_per_match':(8,17),'new_buildings_per_match':(10,17),'upgrades_per_match':(11,17),'final_population_mean':(12,17),'match_ticks_mean':(13,17)}
submetrics={'starvation_rate':(9,14),'critical_hunger_fraction':(18,14),'construction_rate':(10,15)}
for r in primary_rows:
 t=time_rows[r['id']];row=[t['labour'][0],sum(t['filling']),sum(t['labour'][1:4]),int(r['finished_before_cap']),r['lost_colonies'],r['delivered'],r['harvested'],r['births'],r['combat_deaths'],r['starvation_deaths'],r['new_buildings'],r['upgrades'],r['population'],r['ticks'],r['unit_ticks'],r['worker_ticks'],t['recorded_worker_time'],1,r['critical_hunger_ticks']];groups[r['stratum']][r['arm']].append(row)
strata=sorted(groups);arms=['original','cached-material']
def total(rows):return [sum(row[i] for row in rows) for i in range(19)]
def difference(a,b,definition):i,j=definition;return b[i]/b[j]-a[i]/a[j]
sums={s:{a:total(groups[s][a]) for a in arms} for s in strata};overall={a:[sum(sums[s][a][i] for s in strata) for i in range(19)] for a in arms}
observed={m:difference(overall[arms[0]],overall[arms[1]],d) for m,d in secondary.items()}
for s in strata:
 for m,d in submetrics.items():observed[s+' | '+m]=difference(sums[s][arms[0]],sums[s][arms[1]],d)
counts={m:0 for m in observed};rng=random.Random(713099);vectors={s:groups[s][arms[0]]+groups[s][arms[1]] for s in strata};whole={s:total(vectors[s]) for s in strata};sizes={s:len(groups[s][arms[0]]) for s in strata}
for draw in range(10000):
 all_a=[0]*19;all_b=[0]*19
 for s in strata:
  v=vectors[s];rng.shuffle(v);a=[0]*19
  for row in v[:sizes[s]]:
   for k in range(19):a[k]+=row[k]
  b=[whole[s][i]-a[i] for i in range(19)]
  for k in range(19):all_a[k]+=a[k];all_b[k]+=b[k]
  for m,d in submetrics.items():name=s+' | '+m;counts[name]+=abs(difference(a,b,d))>=abs(observed[name])
 for m,d in secondary.items():counts[m]+=abs(difference(all_a,all_b,d))>=abs(observed[m])
 if (draw+1)%2000==0:print('Exploratory permutations',draw+1,flush=True)
pv={m:(v+1)/10001 for m,v in counts.items()};adj={};prev=0
for rank,m in enumerate(sorted(pv,key=pv.get)):prev=max(prev,min(1,pv[m]*(len(pv)-rank)));adj[m]=prev
report={'scope':'Post-result exploratory analysis requested after descriptive changes were reported. Primary preregistered analysis unchanged. Two-sided within-stratum whole-match label randomization,10000draws. Holm correction jointly over15secondary endpoints and all36inspected subgroup endpoints(51tests), rather than only selecting the unfavorable river subgroup. Completion-selection caveat remains because one cached game failed. Findings require independent confirmation; no retrospective confirmatory/equivalence claim.','seed':713099,'tests_in_family':51,'input_sha256':hashlib.sha256((p/'match-summary.json').read_bytes()).hexdigest(),'secondary':{m:{'original':overall[arms[0]][d[0]]/overall[arms[0]][d[1]],'cached':overall[arms[1]][d[0]]/overall[arms[1]][d[1]],'difference':observed[m],'randomization_p':pv[m],'holm_p_51':adj[m],'exploratory_significant_0_05':adj[m]<.05} for m,d in secondary.items()},'subgroups':{m:{'difference':observed[m],'randomization_p':pv[m],'holm_p_51':adj[m],'exploratory_significant_0_05':adj[m]<.05} for m in observed if m not in secondary}}
(p/'secondary-tests.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:report['secondary'][k] for k in ['idle_fraction','filling_fraction','early_finish_fraction']},indent=2));print(json.dumps(report['subgroups']['river/four-ai | starvation_rate']))
