#!/usr/bin/env python3
"""Train the development-only selector; export inert trees for stdlib inference.

Training consumes completed CI observations, never code or executable model files.
The last chronological fifth guards publication using observed CI outcomes.
Checks that did not run remain unobserved during this validation.
"""
from __future__ import annotations
import argparse
import bisect
from collections import defaultdict
import gzip
import hashlib
import json
import math
from pathlib import Path
import time

SCHEMA = 1
FEATURE_SCHEMA = 'history54-v1'
PARAMETERS = dict(n_estimators=220, max_depth=12, min_samples_leaf=20,
                  random_state=42, n_jobs=1)


def read_json(path):
    path = Path(path)
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt', encoding='utf-8') as stream:
        return json.load(stream)


def write_json(path, document):
    """Deterministic compressed JSON, replaced atomically."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(document, separators=(',', ':'), sort_keys=True,
                         allow_nan=False).encode()
    if path.suffix == '.gz':
        payload = gzip.compress(payload, mtime=0)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_bytes(payload)
    temporary.replace(path)


def export_model(bundle, *, cutoff, training=None):
    """Convert a fitted vectorizer/scaler/forest to a non-executable schema."""
    vectorizer, scaler, estimator = bundle
    names = [str(name) for name in vectorizer.get_feature_names_out()]
    scales = [float(value) for value in scaler.scale_]
    if not names or len(names) != len(scales) or any(
            not math.isfinite(value) or value <= 0 for value in scales):
        raise ValueError('invalid feature scales')
    classes = list(estimator.classes_)
    if 1 not in classes or 0 not in classes or len(classes) != 2:
        raise ValueError('a binary classifier with both outcomes is required')
    positive = classes.index(1)
    trees = []
    for fitted in estimator.estimators_:
        tree = fitted.tree_
        probabilities = []
        for values in tree.value:
            counts = [float(value) for value in values[0]]
            total = sum(counts)
            probabilities.append(counts[positive] / total if total else 0.)
        trees.append({'left': tree.children_left.tolist(),
                      'right': tree.children_right.tolist(),
                      'feature': tree.feature.tolist(),
                      'threshold': tree.threshold.tolist(),
                      'probability': probabilities})
    if not trees:
        raise ValueError('empty forest')
    core = {'schema': SCHEMA, 'feature_schema': FEATURE_SCHEMA,
            'feature_names': names, 'scales': scales, 'trees': trees,
            'policy': {'prior_failure_discount': .5, 'unknown_first': True}}
    digest = hashlib.sha256(json.dumps(core, sort_keys=True,
                                      separators=(',', ':')).encode()).hexdigest()
    return {**core, 'model_id': 'history-et54-' + digest[:20],
            'created_at': int(time.time()), 'training_cutoff': float(cutoff),
            'training': training or {},
            'parameters': {key: estimator.get_params()[key] for key in PARAMETERS},
            'score_interpretation': 'Reweighted ranking score; not calibrated risk.'}


def make_bundle_manifest(directory, *, model=None):
    directory = Path(directory)
    files = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
             for name in ('model.json.gz', 'history.json.gz')}
    manifest = {'schema': SCHEMA, 'files': files}
    if model:
        manifest.update(model_id=model['model_id'],
                        feature_schema=model['feature_schema'],
                        training_cutoff=model['training_cutoff'])
    return manifest


def prepare_runs(history, cutoff, make_features=None):
    """Causal rows: history ends before prediction, all labels before cutoff."""
    if isinstance(cutoff, bool) or not isinstance(cutoff, (int, float)) or not math.isfinite(cutoff) or cutoff <= 0:
        raise ValueError('cutoff must be a positive finite epoch timestamp')
    runtime = None
    if make_features is None:
        import ci_predictive as runtime
    if history.get('schema') != SCHEMA or not isinstance(history.get('records'), list):
        raise ValueError('history must contain schema 1 records')
    groups = {}
    for record in history['records']:
        if not isinstance(record, dict):
            raise ValueError('invalid observation')
        for name in ('name', 'branch', 'event'):
            if not isinstance(record.get(name), str):
                raise ValueError('missing observation context')
        for name in ('created_at', 'end'):
            value = record.get(name)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError('invalid observation timestamp')
        if not isinstance(record.get('run_id'), (int, str)) or not str(record['run_id']):
            raise ValueError('missing run identity')
        if record.get('failed') not in (0, 1) or record['end'] < record['created_at']:
            raise ValueError('invalid observation outcome')
        groups.setdefault((str(record.get('run_id')), int(record.get('attempt', 1))), []).append(record)
    indexed = defaultdict(list)
    indexed_times = {}
    canonical = {}
    if runtime is not None:
        from ci_check_inventory import inventory
        catalog = inventory()
        # Runtime ranks canonical inventory checks, including reviewed raw-name
        # aliases and renamed reusable-workflow parents. Train on that same view.
        for name in {record['name'] for record in history['records']}:
            matches = [check for check in catalog if runtime._matching(check, [{'name': name}])]
            if len(matches) > 1:
                raise ValueError('ambiguous canonical job identity: ' + name)
            if matches:
                canonical[name] = matches[0]
        prepared, issues = runtime._records(history, cutoff)
        if issues:
            raise ValueError('invalid runtime history records')
        for observation in prepared:
            parts = observation['name'].split(' / ')
            for index in range(len(parts)):
                indexed[' / '.join(parts[index:])].append(observation)
        for check in {value['id']: value for value in canonical.values()}.values():
            indexed[('check', check['id'])] = runtime._matching(check, prepared)
        indexed_times = {name: [record['end'] for record in values]
                         for name, values in indexed.items()}
    result = []
    for key, records in groups.items():
        start = min(record['created_at'] for record in records)
        end = max(record['end'] for record in records)
        if start >= cutoff or end >= cutoff:
            continue
        first = records[0]
        if any(record['event'] != first['event'] or record['branch'] != first['branch']
               or record['created_at'] != start for record in records):
            raise ValueError('inconsistent run context')
        records = sorted(records, key=lambda record: record['name'])
        if len({record['name'] for record in records}) != len(records):
            raise ValueError('duplicate job observation')
        # Pass only available observations to the shared constructor as an additional
        # boundary, even though the constructor itself also checks completion times.
        source_paths = None if first.get('paths_unknown', False) else first.get('paths')
        if runtime is not None:
            features = []
            for record in records:
                name = record['name']
                check = canonical.get(name, name)
                identity = ('check', check['id']) if isinstance(check, dict) else name
                count = bisect.bisect_left(indexed_times.get(identity, []), start)
                available = indexed[identity][:count]
                features.append(runtime._features(check, source_paths, available,
                                                 first['event'], first['branch'], start))
        else:
            available = {'schema': SCHEMA, 'records': [record for record in history['records']
                                                      if record['end'] < start]}
            features = [make_features(record['name'], source_paths, available,
                                      first['event'], first['branch'], start)
                        for record in records]
        for feature in features:
            if not isinstance(feature, dict) or any(
                    not isinstance(value, (int, float)) or not math.isfinite(value)
                    for value in feature.values()):
                raise ValueError('invalid generated features')
        result.append({'id': key, 'start': start, 'end': end,
                       'records': records, 'features': features,
                       'check_ids': [canonical.get(record['name'], {}).get('id', record['name'])
                                     for record in records],
                       'event': first['event'], 'branch': first['branch']})
    return sorted(result, key=lambda run: (run['start'], run['id']))


def fit_model(runs):
    import numpy as np
    from sklearn.ensemble import ExtraTreesClassifier
    from sklearn.feature_extraction import DictVectorizer
    from sklearn.preprocessing import MaxAbsScaler
    rows = [(run, i) for run in runs for i, feature in enumerate(run['features'])
            if not feature['unknown']]
    if len(rows) < 20:
        raise ValueError('too few known-history observations')
    y = [run['records'][i]['failed'] for run, i in rows]
    if len(set(y)) != 2:
        raise ValueError('both passing and failing outcomes are required')
    counts = {run['id']: sum(not feature['unknown'] for feature in run['features'])
              for run in runs}
    weights = [1 / counts[run['id']] for run, _ in rows]
    weights = np.array(weights) * len(weights) / sum(weights)
    vectorizer = DictVectorizer(sparse=False)
    matrix = vectorizer.fit_transform([run['features'][i] for run, i in rows])
    scaler = MaxAbsScaler()
    matrix = scaler.fit_transform(matrix)
    model = ExtraTreesClassifier(**PARAMETERS)
    model.fit(matrix, y, sample_weight=weights)
    return vectorizer, scaler, model


def evaluate(bundle, targets, baseline=False):
    """Rank only observed jobs; this guard cannot validate unobserved outcomes."""
    import numpy as np
    vectorizer, scaler, model = bundle
    totals = {budget: {'fail_hits': 0, 'reg_hits': 0, 'job_hits': 0}
              for budget in (.1, .12, .25, .5)}
    fail_runs = reg_runs = failed_jobs = 0
    for run in targets:
        features = run['features']
        y = [record['failed'] for record in run['records']]
        regression = [int(failed and not feature['unknown'] and not feature['last_failed'])
                      for failed, feature in zip(y, features)]
        fail_runs += bool(sum(y))
        reg_runs += bool(sum(regression))
        failed_jobs += sum(y)
        if baseline:
            scores = [2 * feature['branch_last_failed'] + feature['branch_rate']
                      if feature['branch_known'] else
                      2 * feature['last_failed'] + feature['failure_rate']
                      for feature in features]
        else:
            probabilities = model.predict_proba(scaler.transform(vectorizer.transform(features)))[:, 1]
            scores = [float(value) * (.5 if feature['last_failed'] else 1.)
                      for value, feature in zip(probabilities, features)]
        ranked = sorted(range(len(y)), key=lambda i: (
            int(not features[i]['unknown']),
            -scores[i] if not features[i]['unknown'] else 0,
            run.get('check_ids', [record['name'] for record in run['records']])[i]))
        for budget, count in totals.items():
            selected = ranked[:math.ceil(budget * len(y) - 1e-10)]
            count['fail_hits'] += bool(sum(y[i] for i in selected))
            count['reg_hits'] += bool(sum(regression[i] for i in selected))
            count['job_hits'] += sum(y[i] for i in selected)
    scores = []
    for count in totals.values():
        if fail_runs:
            scores.append(count['fail_hits'] / fail_runs)
        if reg_runs:
            scores.append(count['reg_hits'] / reg_runs)
        if failed_jobs:
            scores.append(count['job_hits'] / failed_jobs)
    return {'runs': len(targets), 'failing_runs': fail_runs,
            'regression_runs': reg_runs, 'failed_jobs': failed_jobs,
            'objective': float(np.mean(scores)) if scores else None,
            'failure_run_recall_12': totals[.12]['fail_hits'] / fail_runs if fail_runs else None,
            'failure_run_recall_25': totals[.25]['fail_hits'] / fail_runs if fail_runs else None,
            'budgets': totals}


def publication_guard(candidate, baseline, margin=.03):
    if candidate['runs'] < 20 or candidate['failing_runs'] < 5:
        return False, 'insufficient chronological validation outcomes'
    for item in (candidate, baseline):
        if (item.get('objective') is None or not math.isfinite(item['objective'])
                or not 0 <= item['objective'] <= 1):
            return False, 'invalid validation metrics'
    for item in (candidate, baseline):
        for percent in (12, 25):
            recall = item.get(f'failure_run_recall_{percent}')
            if recall is None or not math.isfinite(recall) or not 0 <= recall <= 1:
                return False, 'invalid failure-run validation metric'
    if candidate['objective'] < baseline['objective'] - margin:
        return False, 'validation objective regressed against branch history'
    for percent in (12, 25):
        key = f'failure_run_recall_{percent}'
        if candidate[key] < baseline[key] - .05:
            return False, f'failing-run detection regressed at {percent}% budget'
    return True, 'chronological validation guard passed'


def train(history, cutoff):
    runs = prepare_runs(history, cutoff)
    if len(runs) < 40:
        raise ValueError('at least 40 completed runs are required')
    split = runs[max(1, int(len(runs) * .8))]['start']
    before = [run for run in runs if run['start'] < split and run['end'] < split]
    targets = [run for run in runs if run['start'] >= split]
    # PR observations match the deployed decision when enough exist; otherwise
    # the guard still reports explicitly that it used pooled observed inventories.
    pr = [run for run in targets if run['event'] == 'pull_request']
    scope = 'pull_request' if len(pr) >= 20 else 'all_events'
    targets = pr if scope == 'pull_request' else targets
    validation = fit_model(before)
    candidate = evaluate(validation, targets)
    baseline = evaluate(validation, targets, baseline=True)
    accepted, reason = publication_guard(candidate, baseline)
    report = {'accepted': accepted, 'reason': reason, 'scope': scope,
              'split_at': split, 'candidate': candidate, 'baseline': baseline,
              'limitations': 'Observed CI jobs only; missing-job outcomes are unknown.'}
    if not accepted:
        return None, report
    bundle = fit_model(runs)
    metadata = {'runs': len(runs), 'known_jobs': sum(not feature['unknown']
                for run in runs for feature in run['features']),
                'latest_label_completion': max(run['end'] for run in runs),
                'validation': report}
    return export_model(bundle, cutoff=cutoff, training=metadata), report



def refresh_run_ids(history, workflow_runs, requested=None):
    """Backfill the bounded recent window even for a workflow-run trigger.

    An explicit run is always collected again so its latest attempt is observed.
    API workflow_runs arrive newest first; collect missing observations oldest first.
    """
    attempts = {}
    for record in history['records']:
        identity = int(record['run_id'])
        attempts[identity] = max(attempts.get(identity, 0), int(record.get('attempt', 1)))
    chosen = []
    for run in reversed(workflow_runs):
        identity = int(run['id'])
        if (run.get('conclusion') in ('success', 'failure')
                and run.get('event') in ('push', 'schedule', 'workflow_dispatch')
                and attempts.get(identity, 0) < run.get('run_attempt', 1)
                and identity not in chosen):
            chosen.append(identity)
    if requested:
        identity = int(requested)
        if identity not in chosen:
            chosen.append(identity)
    return chosen


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history', required=True, type=Path)
    parser.add_argument('--model', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--cutoff', type=float, default=None)
    args = parser.parse_args(argv)
    cutoff = args.cutoff if args.cutoff is not None else time.time()
    try:
        candidate, report = train(read_json(args.history), cutoff)
    except ValueError as error:
        candidate, report = None, {'accepted': False, 'reason': str(error)}
    if candidate is not None:
        write_json(args.model, candidate)
    elif not args.model.exists():
        raise SystemExit('training declined and no previous model is available')
    from ci_predictive import validate_model
    model = read_json(args.model)
    validate_model(model)
    manifest = make_bundle_manifest(args.model.parent, model=model)
    manifest['training_update'] = report
    manifest['history_updated_at'] = read_json(args.history).get('updated_at')
    write_json(args.manifest, manifest)
    print(json.dumps(report, sort_keys=True, allow_nan=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
