"""Separate zero-capacity peaks from shortages while beds remain available."""
import collections,csv,json,pathlib,statistics,sys
root=pathlib.Path(sys.argv[1]);games=list(csv.DictReader((root/'games.csv').open()));samples=list(csv.DictReader((root/'pressure-samples.csv').open()));report={}
for cohort in ['128','256']:
 for variant in ['baseline','towers-lazy']:
  group=[r for r in games if r['cohort']==cohort and r['variant']==variant and int(r['shortfall_samples'])]
  bycase=collections.defaultdict(list)
  for r in samples:
   if r['cohort']==cohort and r['variant']==variant and int(r['total_beds'])>0 and int(r['missing_beds'])>0:bycase[r['case']].append(int(r['missing_beds']))
  report[cohort+'/'+variant]=dict(shortfall_games=len(group),games_with_zero_beds_at_first_maximum_shortfall=sum(int(r['peak_beds'])==0 for r in group),games_with_shortfalls_despite_positive_capacity=len(bycase),mean_game_mean_gap_with_positive_capacity=statistics.mean(statistics.mean(v) for v in bycase.values()),median_game_peak_gap_with_positive_capacity=statistics.median(max(v) for v in bycase.values()))
(root/'capacity-detail.json').write_text(json.dumps(report,indent=2)+'\n')
