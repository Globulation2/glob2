# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared integer regression, grouped splitting and deterministic map generation.

The historical module name is retained by immutable training packages. Production
emission and label contracts belong to adaptive_gradient_model/codegen instead.
"""
from __future__ import annotations
import collections
import hashlib
import json
import math
from pathlib import Path
import subprocess
from tools.resource_gradient_dataset import FEATURES
from tools.resource_gradient_tournament import digest
ROOT=Path(__file__).resolve().parents[1]
HEADS=('positive_probability','negative_probability','positive_mean','negative_mean')
LIMIT=1_000_000
LOG=tuple(round(math.log2(1+i/256)*4096) for i in range(257))
SIG=tuple(round(65536/(1+math.exp(-x/1000))) for x in range(-12000,12001,50))


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()


def log_integer(x):
    n = max(0, min(LIMIT, int(x))) + 1
    b = n.bit_length() - 1
    pos = ((n - (1 << b)) << 16) // (1 << b)
    i, f = pos >> 8, pos & 255
    return (b << 12) + LOG[i] + (LOG[i + 1] - LOG[i]) * f // 256


def truncdiv(n, d):
    return n // d if n >= 0 else -((-n) // d)


def predict(model, raw):
    transforms=model.get('transforms',['log1p_q12']*len(model.get('features',FEATURES)))
    if len(raw)!=len(transforms):raise ValueError('Predictor feature vector length mismatch')
    x = [log_integer(v) if transform=='log1p_q12' else max(0,min(LIMIT,int(v))) for v,transform in zip(raw,transforms)]
    def linear(name):
        head = model[name]
        return max(-10**9, min(10**9, truncdiv(head['bias'] + sum(a*b for a, b in zip(x, head['coef'])), 1 << head['shift'])))
    def sigmoid(z):
        n = max(-12000, min(12000, z)) + 12000
        i = min(n // 50, 479)
        return SIG[i] + (SIG[i + 1] - SIG[i]) * (n - i*50) // 50
    value = truncdiv(sigmoid(linear('positive_probability')) * max(0, linear('positive_mean'))
                     - sigmoid(linear('negative_probability')) * max(0, linear('negative_mean')), 65536)
    return max(-10**9, min(10**9, value))


def validate_heads(model, research=False, expected_threshold=16000):
    features=tuple(model.get('features',()))
    if type(model.get('threshold')) is not int or model['threshold']!=expected_threshold:raise ValueError('Unexpected integer decision threshold')
    if research:
        if model.get('schema')!=2 or not model.get('research_only'):raise ValueError('Expected research-only model schema')
        if not features or len(set(features))!=len(features):raise ValueError('Invalid research features')
    elif model.get('schema')!=1 or features!=FEATURES:raise ValueError('Expected seven-feature baseline model schema')
    transforms=model.get('transforms',['log1p_q12']*len(features))
    if len(transforms)!=len(features) or any(t not in ('log1p_q12','identity_q12') for t in transforms):raise ValueError('Invalid feature transforms')
    if not research and any(t!='log1p_q12' for t in transforms):raise ValueError('Baseline model requires log transforms')
    bounds=[log_integer(LIMIT) if t=='log1p_q12' else LIMIT for t in transforms]
    for name in HEADS:
        h=model[name]
        if len(h['coef'])!=len(features) or type(h['shift']) is not int or h['shift'] not in (28,24,20,16,12,8):raise ValueError('Invalid linear head')
        if not all(type(v) is int for v in [h['bias'],*h['coef']]):raise ValueError('Noninteger coefficients')
        if abs(h['bias'])+sum(abs(c)*bound for c,bound in zip(h['coef'],bounds))>=1<<61:raise ValueError('Unsafe accumulator')


def validate_specs(specs):
    if not isinstance(specs, list) or not specs: raise ValueError('Plan needs a nonempty game list')
    for spec in specs:
        n = spec['players']
        if type(n) is not int or not 2 <= n <= 16: raise ValueError('Plan players must be 2..16')
        for key in ['width', 'height']:
            size = spec.get(key, spec.get('size'))
            if type(size) is not int or size < 1 or size > 512 or size & (size-1):
                raise ValueError('Plan dimensions must be powers of two, at most 512')
        for key in ['map_seed', 'game_seed']:
            if type(spec[key]) is not int or not 0 <= spec[key] <= 2**32-1: raise ValueError('Plan seeds must be uint32')
        if len(spec['alliances']) != n or any(type(v) is not int or not 1 <= v <= n for v in spec['alliances']):
            raise ValueError('Plan needs one valid alliance per player')
        if len(set(spec['alliances'])) < 2: raise ValueError('Plan must have opposing alliances')
        if 'ais' in spec and (len(spec['ais']) != n or any(not isinstance(a, str) or not a for a in spec['ais'])):
            raise ValueError('Plan needs one AI name per player')
        params = spec.get('generator_params', {})
        if not isinstance(params, dict) or set(params) & {'width', 'height', 'teams', 'seed'}:
            raise ValueError('Generator params cannot override explicit map identity')
        if any(not isinstance(k, str) or type(v) is not int or v < 0 for k, v in params.items()):
            raise ValueError('Generator params must be nonnegative integer catalog controls')
        candidates=spec.get('map_candidates', [])
        if not isinstance(candidates,list) or len(candidates)>31:raise ValueError('At most 32 map candidates per match')
        for candidate in candidates:
            if not isinstance(candidate,dict) or set(candidate)-{'generator','width','height','map_seed','generator_params'}:
                raise ValueError('Map candidates may change only generation inputs')
            validate_specs([{**{k:v for k,v in spec.items() if k!='map_candidates'},**candidate}])
    if len({digest(spec) for spec in specs}) != len(specs): raise ValueError('Duplicate starting states in plan')


def generate_map(binary, spec, directory, env):
    attempts=[]
    base={k:v for k,v in spec.items() if k!='map_candidates'}
    for index,candidate in enumerate([{}]+spec.get('map_candidates', [])):
        actual={**base,**candidate};report=directory/f'generation-{index}.json'
        report.unlink(missing_ok=True)
        command=[str(binary),'map','generate',actual['generator'],'--seed',str(actual['map_seed']),
                 '--width',str(actual.get('width',actual.get('size'))),
                 '--height',str(actual.get('height',actual.get('size'))),'--teams',str(actual['players']),
                 '--output',str(directory/'map.map.gz'),'--report-file',str(report)]
        for key,value in sorted(actual.get('generator_params', {}).items()):command+=['--set',f'{key}={value}']
        with (directory/f'generation-{index}.log').open('w') as log:
            result=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600)
        attempts.append(dict(spec=actual,command=command,returncode=result.returncode))
        save(directory/'generation-attempts.json',attempts)
        if result.returncode==0:
            save(directory/'selected-map.json',actual)
            return actual,attempts
        failure=json.loads(report.read_text()) if report.exists() else {}
        # Retry only explicit generation rejections. Crashes, bad CLI options,
        # missing dependencies, script errors and timeouts must remain failures.
        if result.returncode!=2 or failure.get('report_type')!='generation_failure' or failure.get('generation',{}).get('outcome',{}).get('error') not in {'invalid_request','placement_failed','invalid_world'}:
            raise subprocess.CalledProcessError(result.returncode,command)
    raise RuntimeError(f'All map candidates rejected; inspect {directory}/generation-attempts.json')


def dataset(manifests):
    from tools.resource_gradient_dataset import load
    return load(manifests)


def weights(rows):
    from tools.resource_gradient_dataset import balanced_weights
    return balanced_weights(rows)


def fit_heads(rows, features=FEATURES, transforms=None, weighting=None, threshold=16000):
    rows=[r for r in rows if r.get('movement_label_available',1)]
    if not rows:raise ValueError('No observed movement labels to fit')
    import numpy as np
    from sklearn.linear_model import LogisticRegression,Ridge
    if type(threshold)is not int or threshold<0:raise ValueError('Nonnegative integer decision threshold required')
    features=tuple(features);transforms=transforms or ['log1p_q12']*len(features)
    if len(transforms)!=len(features) or any(t not in ('log1p_q12','identity_q12') for t in transforms):raise ValueError('Invalid feature transforms')
    x=np.array([[log_integer(r[k]) if t=='log1p_q12' else max(0,min(LIMIT,int(r[k]))) for k,t in zip(features,transforms)] for r in rows],dtype=np.int64)
    y=np.array([r['defer_extra']-r['refresh_extra'] for r in rows],dtype=float);w=(weighting or weights)(rows)
    mu=np.average(x,axis=0,weights=w);sd=np.sqrt(np.average((x-mu)**2,axis=0,weights=w));sd[sd<1e-12]=1
    z=(x-mu)/sd
    research=features!=FEATURES or any(t!='log1p_q12' for t in transforms)
    model=dict(schema=2 if research else 1,features=list(features),threshold=threshold)
    if research:model.update(research_only=True,transforms=list(transforms))
    bounds=[log_integer(LIMIT) if t=='log1p_q12' else LIMIT for t in transforms]
    def export(beta,intercept):
        for shift in (28,24,20,16,12,8):
            scale=1000*(1<<shift);coef=[round(float(v)*scale) for v in beta];bias=round(float(intercept)*scale)
            bound=abs(bias)+sum(abs(c)*bound for c,bound in zip(coef,bounds))
            if bound<1<<61:return dict(coef=coef,bias=bias,shift=shift,accumulator_bound=bound)
        raise ValueError('Unsafe coefficient accumulator')
    for sign,name in [(1,'positive'),(-1,'negative')]:
        mask=sign*y>0
        # Small campaigns may have only one outcome class. Fit the constant
        # head explicitly instead of inventing labels or dropping examples.
        if mask.all() or not mask.any():
            model[name+'_probability']=export(np.zeros(len(features)),12 if mask.all() else -12)
        else:
            est=LogisticRegression(C=1,max_iter=250,random_state=713).fit(z,mask,sample_weight=w)
            beta=est.coef_.ravel()/sd;model[name+'_probability']=export(beta,float(est.intercept_[0])-float(beta@mu))
        if mask.any():
            est=Ridge(alpha=100).fit(z[mask],sign*y[mask],sample_weight=w[mask])
            beta=est.coef_/sd;model[name+'_mean']=export(beta,float(est.intercept_)-float(beta@mu))
        else:model[name+'_mean']=export(np.zeros(len(features)),0)
    validate_heads(model,research=research,expected_threshold=threshold)
    return model


def metrics(rows,model,weighting=None,prediction=None):
    rows=[r for r in rows if r.get('movement_label_available',1)]
    if not rows:return dict(episodes=0,states=0,note='No observed movement labels')
    import numpy as np
    y=np.array([r['defer_extra']-r['refresh_extra'] for r in rows],dtype=float)
    pred=np.array([(prediction(r) if prediction is not None else predict(model,[r[k] for k in model.get('features',FEATURES)]))/1000 for r in rows]);w=(weighting or weights)(rows)
    threshold=model['threshold']/1000
    take=pred>=threshold;ideal=y>=threshold;positive=np.maximum(y,0)
    # The threshold is the decision's opportunity cost in the same benefit
    # units. This regret scores the exported integer decisions, not fit MAE.
    regret=np.maximum(y-threshold,0)*(~take)+np.maximum(threshold-y,0)*take
    def mean(values):return float(np.average(values,weights=w))
    def quantile(values,q):
        order=np.argsort(values,kind='stable');v=values[order];cw=np.cumsum(w[order])
        return float(v[min(np.searchsorted(cw,q*cw[-1]),len(v)-1)])
    total=float(np.sum(w*positive));captured=float(np.sum(w*positive*take))
    return dict(episodes=len(rows),states=len({r['group'] for r in rows}),
                mae=mean(abs(pred-y)),mse=mean((pred-y)**2),selected_pct=mean(take)*100,
                selected_actual_benefit=float(np.average(y[take],weights=w[take])) if take.any() else None,
                positive_benefit_captured_fraction=captured/total if total else None,
                missed_positive_benefit=mean(positive*(~take)),negative_benefit_selected=mean(np.maximum(-y,0)*take),
                threshold=threshold,decision_regret=mean(regret),decision_regret_p95=quantile(regret,.95),
                decision_regret_p99=quantile(regret,.99),false_refresh_pct=mean(take & ~ideal)*100,
                missed_threshold_decision_pct=mean(~take & ideal)*100,
                absolute_error_p95=quantile(abs(pred-y),.95),absolute_error_p99=quantile(abs(pred-y),.99))


def split_maps(rows):
    """Stratify map/format pairs, never episode counts or individual games.

    One map may appear in several formats. A group splitter assigns it once
    globally; explicit coverage checks prevent small/overlapping strata from
    silently disappearing from holdout or a validation fold.
    """
    import numpy as np
    from sklearn.model_selection import StratifiedGroupKFold
    pairs=sorted({(r['group'],r['format']) for r in rows})
    groups=np.array([g for g,f in pairs]);labels=np.array([f for g,f in pairs])
    x=np.zeros((len(pairs),1))
    formats=sorted(set(labels))
    def counts(selected):
        return {fmt:len({g for i,(g,f) in enumerate(pairs) if i in selected and f==fmt}) for fmt in formats}
    for development, holdout in StratifiedGroupKFold(n_splits=4,shuffle=True,random_state=713).split(x,labels,groups):
        dev_counts,held_counts=counts(set(development)),counts(set(holdout))
        if min(dev_counts.values())>=3 and min(held_counts.values())>=1:
            break
    else:
        raise ValueError('Map overlap prevents format coverage in the holdout; collect more independent maps')
    held=set(groups[holdout]);folds={};coverage=[]
    splitter=StratifiedGroupKFold(n_splits=3,shuffle=True,random_state=714)
    for fold,(training,validation) in enumerate(splitter.split(x[development],labels[development],groups[development])):
        train_indices=set(development[training]);valid_indices=set(development[validation])
        train_counts,valid_counts=counts(train_indices),counts(valid_indices)
        if min(train_counts.values())<2 or min(valid_counts.values())<1:
            raise ValueError('Map overlap prevents format coverage in cross-validation; collect more independent maps')
        for g in groups[list(valid_indices)]:folds[g]=fold
        coverage.append(dict(training_maps=train_counts,validation_maps=valid_counts))
    return held,folds,dict(development_maps=dev_counts,holdout_maps=held_counts,folds=coverage)
