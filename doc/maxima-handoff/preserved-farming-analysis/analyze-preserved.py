import json,gzip,sys,time
from pathlib import Path
b=Path(__file__).resolve().parent;sys.path.insert(0,str(b/'runtime'))
import maxima_win_report as report,maxima_win_experiment as e
p=json.loads((b/'protocol.json').read_text());m=json.loads((b/'manifest.json').read_text());results={}
for f in (b/'results').glob('*/result.json.gz'):
 with gzip.open(f,'rt') as s:r=json.load(s)
 assert r['execution_id']==f.parent.name;results[r['execution_id']]=r
missing={j['execution_id'] for j in m['jobs']}-results.keys();known=set(json.loads((b/'known-conversion-failures.json').read_text()));assert missing==known and len(results)==9516
pairs=report.assemble(m,results)
# Existing exact interval takes an outer envelope over every completion of missing pairs.
v=report.report(p,m,results,confirmatory=True)
v.update(status='recovered_missing_outcome_sensitivity_analysis',formal_original_gate_satisfied=False,interpretation='Fixed 1000-pair samples, original multiplicity-adjusted alpha; existing worst-case missing-pair bounds. No failed result imputed or rerun. Original every-execution gate was not satisfied; this is explicitly labeled recovery analysis.',missing_executions=sorted(missing),completed_executions=len(results),analysis_input_id=e.identity({'manifest':m,'results':results}),generated=time.time())
for row in v['results']:
 ps=pairs[row['switch']];lo=hi=0
 for pair in ps:
  a,c=pair['on'],pair['off'];lo+=(0 if a is None else a)-(1 if c is None else c);hi+=(1 if a is None else a)-(0 if c is None else c)
 row['feasible_effect_bounds']=[lo/len(ps),hi/len(ps)]
e.atomic(b/'RECOVERED_SENSITIVITY_RESULT.json',v)
print(json.dumps([{'switch':r['switch'],'missing_pairs':r['overall']['missing_pairs'],'effect_bounds':r['feasible_effect_bounds'],'adjusted_ci':r['overall']['ci'],'direction':r['overall']['direction']} for r in v['results']],indent=2))
