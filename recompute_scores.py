#!/usr/bin/env python3
"""Recompute every exported score, pair estimate and hierarchical interval offline."""
import collections,gzip,json,random,statistics,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT/'provenance/worker.pyz'))
from tournaments.analysis import observations,percentile

def read(path):
 with gzip.open(ROOT/path,'rt')as f:return json.load(f)
def cluster(entries,expected):
 cells=collections.defaultdict(list)
 for e in entries:cells[e['generator'],e['map_seed']].append(e['delta'])
 complete={k:statistics.mean(v) for k,v in cells.items() if len(v)==expected};by=collections.defaultdict(list)
 for(g,seed),v in complete.items():by[g].append(v)
 mean=statistics.mean(statistics.mean(v) for v in by.values());rng=random.Random(20261102);estimate=[];conditional=[];keys=sorted(by)
 for _ in range(5000):
  conditional.append(statistics.mean(statistics.mean(rng.choices(v,k=len(v)))for g,v in sorted(by.items())))
  selected=rng.choices(keys,k=len(keys));estimate.append(statistics.mean(statistics.mean(rng.choices(by[g],k=len(by[g])))for g in selected))
 return {'mean':mean,'clusters':len(complete),'hierarchical_95':[percentile(estimate,.025),percentile(estimate,.975)],'fixed_generator_95':[percentile(conditional,.025),percentile(conditional,.975)]}
def main():
 manifest=read('protocol/experiment.json.gz');jobs={j['id']:j for j in manifest['jobs']};report=read('reports/overnight/summary.json.gz');expected={g['id']:g for g in report['games']};scores={}
 with gzip.open(ROOT/'data/score-inputs.jsonl.gz','rt')as f:
  for line in f:
   item=json.loads(line);j=jobs[item['job_id']];row=observations([{'category':'success','job':j,'result':item['result']}],'win_probability')[0];value=row['trajectory_scores'][row['groups'].index(row['subject_group'])];assert value['available'];scores[j['id']]=value['score'];assert abs(value['score']-expected[j['id']]['trajectory_score']['score'])<1e-12
 assert len(scores)==3432
 output={}
 for name,comparison in report['policy_contrasts'].items():
  entries=[]
  for e in comparison['paired_entries']:
   value=scores[e['candidate_job']]-scores[e['control_job']];assert abs(value-e['delta'])<1e-12;entries.append(e|{'delta':value})
  result=cluster(entries,12);reference=comparison['all_formats_equal_weight'];assert abs(result['mean']-reference['mean_delta_equal_generator_then_seed'])<1e-12
  assert all(abs(a-b)<1e-10 for a,b in zip(result['hierarchical_95'],reference['95_percent_interval']))
  for fmt in ['1v1','2v2','ffa']:
   byformat=cluster([e for e in entries if e['format']==fmt],4);ref=comparison['by_format'][fmt];assert abs(byformat['mean']-ref['mean_delta_equal_generator_then_seed'])<1e-12;assert all(abs(a-b)<1e-10 for a,b in zip(byformat['hierarchical_95'],ref['95_percent_interval']))
  output[name]=result
 primary=collections.defaultdict(dict)
 for fmt in ['1v1','2v2','ffa']:
  values=collections.defaultdict(list)
  for e in report['paired_differences']:
   if e['variant']=='inn8-swarm12' and e['format']==fmt:values[e['generator'],e['map_seed']].append(scores[e['candidate_job']]-scores[e['baseline_job']])
  for key,v in sorted(values.items()):
   if len(v)==4:primary[key][fmt]=statistics.mean(v)
 entries=[{'generator':g,'map_seed':seed,'delta':statistics.mean(v.values())} for(g,seed),v in primary.items() if len(v)==3]
 primary_result=cluster(entries,1);reference=report['primary_equal_format_aggregate'];assert abs(primary_result['mean']-reference['mean_delta_equal_generator_then_seed'])<1e-12;assert all(abs(a-b)<1e-10 for a,b in zip(primary_result['hierarchical_95'],reference['95_percent_interval']))
 print(json.dumps({'verified_scores':len(scores),'primary_equal_format_aggregate':primary_result,'policy_contrasts':output,'official_scoring_module':observations.__code__.co_filename},indent=2))
if __name__=='__main__':main()
