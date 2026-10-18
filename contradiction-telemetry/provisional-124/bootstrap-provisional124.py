import sys,json,collections,hashlib
from pathlib import Path
import numpy as np
B=Path('/home/bradley/glob2/artifacts/gradient-contradictions-20261018/postprocess-f65');O=B/'PROVISIONAL-fit-124-20261018';sys.path.insert(0,str(B/'source'))
from tools import resource_gradient_model as m
from tools.resource_gradient_contradictions import prepare
p=json.loads((O/'provenance.json').read_text());d=json.loads((O/'comparison.json').read_text());rows,_=prepare(m.dataset([p['manifest']]));held=set(d['heldout_states']);rows=[r for r in rows if r['group'] in held]
models={k:d['variants'][k]['model'] for k in ['baseline7','counts_fractions']}
by=collections.defaultdict(list);base_by=collections.defaultdict(list);aug_by=collections.defaultdict(list);formats=collections.defaultdict(set)
for row in rows:
 y=row['defer_extra']-row['refresh_extra'];regrets={}
 for k,model in models.items():
  take=m.predict(model,[row[f] for f in model['features']])>=model['threshold']
  regrets[k]=max(y-32,0)*(not take)+max(32-y,0)*take
 by[row['group']].append(regrets['counts_fractions']-regrets['baseline7']);base_by[row['group']].append(regrets['baseline7']);aug_by[row['group']].append(regrets['counts_fractions']);formats[row['format']].add(row['group'])
means={k:float(np.mean(v)) for k,v in by.items()}
rng=np.random.default_rng(713);replicates=np.zeros(10000)
for fmt,groups in sorted(formats.items()):
 a=np.array([means[g] for g in sorted(groups)]);replicates+=a[rng.integers(len(a),size=(len(replicates),len(a)))].mean(axis=1)/len(formats)
estimate=sum(np.mean([means[g] for g in groups]) for groups in formats.values())/len(formats)
expected=d['variants']['counts_fractions']['holdout']['decision_regret']-d['variants']['baseline7']['holdout']['decision_regret']
assert abs(estimate-expected)<1e-9
base_point=sum(np.mean([np.mean(base_by[g]) for g in groups]) for groups in formats.values())/len(formats)
aug_point=sum(np.mean([np.mean(aug_by[g]) for g in groups]) for groups in formats.values())/len(formats)
assert abs(base_point-d['variants']['baseline7']['holdout']['decision_regret'])<1e-9
assert abs(aug_point-d['variants']['counts_fractions']['holdout']['decision_regret'])<1e-9
report=dict(exploratory=True,models_fixed=True,paired_map_bootstrap=True,stratified_by_format=True,replicates=10000,seed=713,
 baseline_aggregate=float(base_point),augmented_aggregate=float(aug_point),estimate_regret_delta=estimate,percentile95_delta=np.quantile(replicates,[.025,.975]).tolist(),
 bootstrap_fraction_delta_negative=float((replicates<0).mean()),heldout_maps=len(means),
 maps_per_format={f:len(g) for f,g in formats.items()},per_map_delta=means,
 note='Paired map resampling withinformat preserves equalformat/equalmap weights. Fixedcoefficients only: does not include training uncertainty. Fewmaps and inspection of17variants make this exploratory; not postselection-adjusted significance or probability of realworld policy benefit.')
(O/'exploratory-paired-map-bootstrap.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='per_map_delta'},indent=2))
