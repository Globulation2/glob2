#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit family experts and their combined fallback under one integer contract."""
import argparse,collections,copy,json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:sys.path.insert(0,str(ROOT))
from tools import adaptive_gradient_dataset as d
from tools import resource_gradient_model as integer
from tools.resource_gradient_dataset import registered_split
HEADS=integer.HEADS

DEPLOYMENT_AGE_CAP_TICKS = 1024
DEPLOYMENT_MINIMUM_INTERVAL_TICKS = d.DEFAULT_MINIMUM_INTERVAL_TICKS


def validate_family_experts(model):
    """Expert IDs are stable field kinds, never array positions supplied by a caller."""
    experts = model.get('family_experts')
    if not isinstance(experts, dict) or not experts:
        raise ValueError('Measured family experts required')
    if tuple(model.get('features', ())) != d.FEATURES or tuple(model.get('transforms', ())) != d.TRANSFORMS:
        raise ValueError('Family experts require the exact twelve generic inputs')
    for identifier, expert in experts.items():
        if identifier not in {str(i) for i in range(len(d.FAMILIES))}:
            raise ValueError('Unknown or noncanonical expert family ID')
        if set(expert) != {'family', 'training_support', *HEADS}:
            raise ValueError('Unexpected expert fields')
        if expert['family'] != d.FAMILIES[int(identifier)]:
            raise ValueError('Expert family name disagrees with its stable ID')
        support = expert['training_support']
        if not isinstance(support, dict) or set(support) != {'rows', 'maps', 'positive', 'negative', 'zero'}:
            raise ValueError('Explicit expert class support required')
        if any(type(value) is not int or value < 0 for value in support.values()):
            raise ValueError('Invalid expert class support')
        if not 0 < support['maps'] <= support['rows'] or sum(support[k] for k in ('positive', 'negative', 'zero')) != support['rows']:
            raise ValueError('An unmeasured family cannot claim a fitted expert')
        temporary = dict(schema=2, research_only=True, features=list(d.FEATURES),
                         transforms=list(d.TRANSFORMS), threshold=model['threshold'],
                         **{name: expert[name] for name in HEADS})
        integer.validate_heads(temporary, research=True, expected_threshold=model['threshold'])


def validate(model):
    schema = model.get('schema')
    if schema not in (4, 5) or not d.compatible_contract(model.get('contract')):
        raise ValueError('Expected exact unified deployment contract')
    if tuple(model.get('transforms', ())) != d.selected_transforms(model.get('features', ())):
        raise ValueError('Unexpected feature transforms')
    if type(model.get('trained')) is not bool:
        raise ValueError('Explicit trained status required')
    threshold = d.threshold_milli(model.get('threshold'))
    deployment = model.get('deployment', {})
    if not isinstance(deployment, dict):
        raise ValueError('Deployment metadata must be an object')
    if deployment and deployment.get('threshold_milli') != threshold:
        raise ValueError('Deployment threshold metadata disagrees with model')
    if 'minimum_interval_ticks' in deployment:
        minimum = deployment['minimum_interval_ticks']
        if type(minimum) is not int or not 1 <= minimum <= (1 << 32)-1:
            raise ValueError('Deployment minimum interval must be a positive bounded tick count')
    if 'demanded_age_cap_ticks' in deployment:
        cap = deployment['demanded_age_cap_ticks']
        if type(cap) is not int or not 8 <= cap <= (1 << 32)-1:
            raise ValueError('Deployment demanded-age cap must be a positive bounded tick count')
    integer.validate_heads(dict(model, schema=2, research_only=True), research=True, expected_threshold=threshold)
    if schema == 5:
        if not model['trained'] or deployment.get('strategy') != 'family-experts-with-combined-fallback':
            raise ValueError('Explicit trained family strategy required')
        if type(deployment.get('demanded_age_cap_ticks')) is not int or deployment['demanded_age_cap_ticks'] != DEPLOYMENT_AGE_CAP_TICKS:
            raise ValueError('Deployment demanded-age cap disagrees with the current policy')
        validate_family_experts(model)
    elif 'family_experts' in model or deployment.get('strategy') == 'family-experts-with-combined-fallback':
        raise ValueError('Historical shared schema cannot contain hidden experts')
    if not model['trained'] and any(model[name]['bias'] or any(model[name]['coef']) for name in HEADS):
        raise ValueError('Untrained bootstrap cannot contain fitted resource coefficients')


def inference_views(model):
    """All heads share transforms; an unknown/new family uses the combined fit."""
    combined = {key: model[key] for key in ('features', 'transforms', *HEADS)}
    experts = {int(key): dict(combined, **{name: value[name] for name in HEADS})
               for key, value in model.get('family_experts', {}).items()}
    return combined, experts


def predict(model, base, counts, family=None):
    validate(model)
    if not model['trained']:
        raise ValueError('No unified model has been trained; use explicit fixed-interval research bootstrap')
    if family is not None and (type(family) is not int or not 0 <= family <= (1 << 32)-1):
        raise ValueError('Family must be an unsigned stable field ID')
    combined, experts = inference_views(model)
    selected = experts.get(family, combined)
    return integer.predict(selected, d.prepare(base, counts)[:len(model['features'])])


def model_metrics(rows, model, weighting=d.balanced_weights):
    if not model.get('family_experts'):
        return integer.metrics(rows, model, weighting=weighting)
    combined, experts = inference_views(model)
    def prediction(row):
        selected = experts.get(row['family_id'], combined)
        return integer.predict(selected, [row[key] for key in model['features']])
    return integer.metrics(rows, model, weighting=weighting, prediction=prediction)


def fit_family_experts(rows, threshold):
    """Preserve global relative weights; only rescale each family's total mass.

    Recomputing hierarchy weights inside a family would change the experiment's
    format/cohort/map balance. The scalar mean-one rescaling keeps the existing
    regularization convention without inventing examples for absent families.
    """
    import numpy as np
    global_weights = {id(row): float(weight) for row, weight in zip(rows, d.balanced_weights(rows))}
    experts = {}
    for identifier, family in enumerate(d.FAMILIES):
        subset = [row for row in rows if row['family'] == family]
        if not subset:
            continue
        def weights(actual):
            values = np.asarray([global_weights[id(row)] for row in actual])
            return values * len(values) / sum(values)
        fitted = integer.fit_heads(subset, d.FEATURES, d.TRANSFORMS, weighting=weights, threshold=threshold)
        support = dict(rows=len(subset), maps=len({row['group'] for row in subset}),
                       positive=sum(row['defer_extra'] > row['refresh_extra'] for row in subset),
                       negative=sum(row['defer_extra'] < row['refresh_extra'] for row in subset),
                       zero=sum(row['defer_extra'] == row['refresh_extra'] for row in subset))
        experts[str(identifier)] = dict(family=family, training_support=support,
                                       **{name: fitted[name] for name in HEADS})
    return experts


def slices(rows,model):
 report={}
 for key in ('family','format','generator','dimensions','cohort','movement','age_bucket','demand_bucket','target_changed','route_changed','seed_cost_changed'):
  groups=collections.defaultdict(list)
  for r in rows:
   if key=='age_bucket':value='0-31' if r['age']<32 else '32-127' if r['age']<128 else '128-511' if r['age']<512 else '512-4095' if r['age']<4096 else '4096+'
   elif key=='demand_bucket':value='0-7' if r['q128']<8 else '8-31' if r['q128']<32 else '32+'
   elif key=='target_changed':value=bool(r['target_added'] or r['target_removed'])
   elif key=='route_changed':value=bool(r.get('route_changes',0))
   elif key=='seed_cost_changed':value=bool(r.get('seed_cost_changes',0))
   else:value=r.get(key,'unknown')
   groups[str(value)].append(r)
  report[key]={name:model_metrics(group,model) for name,group in sorted(groups.items())}
 return report

def age_demand_support(rows):
 return {family:dict(episodes=len(subset),maps=len({r['group'] for r in subset}),maximum_age=max((r['age'] for r in subset),default=None),high_demand_age128plus=sum(r['age']>=128 and r['q128']>=32 for r in subset),high_demand_age512plus=sum(r['age']>=512 and r['q128']>=32 for r in subset),high_demand_age4096plus=sum(r['age']>=4096 and r['q128']>=32 for r in subset)) for family in d.FAMILIES for subset in [[r for r in rows if r['family']==family]]}

def outcome_metrics(rows):
 """Separate future episode outcomes, never penalties folded into direction cost.

 Episodes overlap: these are counterfactual episode-query rates, not unique
 whole-game live stall rates. Live totals require their separate stream.
 """
 import numpy as np
 result={}
 for family in d.FAMILIES:
  subset=[r for r in rows if r['family']==family]
  if not subset:result[family]=dict(episodes=0,measured=False);continue
  w=d.balanced_weights(subset);entry=dict(episodes=len(subset),measured=True,scope='possibly-overlapping-counterfactual-episode-queries')
  for denominator,columns in [('queries',list(d.MOVEMENT_REACHABILITY)+['defer_stalls','refresh_stalls','defer_invalid_goals','refresh_invalid_goals','defer_missed_moves','refresh_missed_moves','defer_escape_failures','refresh_escape_failures']),('planning_queries',['defer_planning_false_negative','defer_planning_false_positive','refresh_planning_false_negative','refresh_planning_false_positive'])]:
   den=sum(weight*r.get(denominator,0) for weight,r in zip(w,subset))
   for column in columns:
    entry[column]=dict(events=sum(r.get(column,0) for r in subset),denominator=denominator,weighted_rate=sum(weight*r.get(column,0) for weight,r in zip(w,subset))/den if den else None)
  entry['guard_balance_queries']=sum(r.get('guard_balance_queries',0) for r in subset)
  result[family]=entry
 return result

def expected_family_scope(value):
 """Coverage requirements are declared before fitting; they never filter rows."""
 names=tuple(value.split(','))
 if not names or len(set(names))!=len(names) or set(names)-set(d.FAMILIES):raise ValueError('Expected-family scope must name unique known families')
 return names

def require_family_support(rows,minimum,expected):
 """Missing families outside the campaign scope remain explicitly unmeasured."""
 cov=d.coverage(rows)
 missing={family:cov[family] for family in expected if cov[family]['maps']<minimum}
 if missing:raise ValueError('Incomplete required family support; collect more compatible examples: '+str(missing))
 return cov

def fit(a):
    """Fit a combined fallback and measured experts under one frozen map split."""
    from tools import adaptive_gradient_collection as collection
    import tempfile
    import shutil

    threshold = d.threshold_milli(getattr(a, 'threshold_milli', d.DEFAULT_THRESHOLD_MILLI))
    expected = expected_family_scope(a.expected_families)
    allrows = (collection.load(a.manifest) if a.collector_contract in collection.SUPPORTED_CONTRACT_IDS
               else d.load(a.manifest))
    rows = d.eligible(allrows)
    registry = Path(a.split_registry)
    if not registry.exists():
        raise ValueError('Persistent map splits must be frozen before collection')
    assignments = json.loads(registry.read_text())['assignments']
    if any(row['group'] not in assignments for row in rows):
        raise ValueError('Dataset contains maps absent from the predeclared split registry')
    with tempfile.TemporaryDirectory() as temp:
        validation_copy = Path(temp) / registry.name
        shutil.copy2(registry, validation_copy)
        held, folds, split = registered_split(rows, validation_copy, integer.split_maps)
    split['registry'].update(name=registry.name, sha256=d.file_hash(registry), read_only_during_fit=True)
    development = [row for row in rows if row['group'] not in held]
    heldout = [row for row in rows if row['group'] in held]
    require_family_support(development, 3, expected)
    require_family_support(heldout, 1, expected)

    def fit_models(population):
        combined = integer.fit_heads(population, d.FEATURES, d.TRANSFORMS,
                                     weighting=d.balanced_weights, threshold=threshold)
        combined['family_experts'] = fit_family_experts(population, threshold)
        return combined

    cross_validation = []
    for fold in range(3):
        training = [row for row in development if folds[row['group']] != fold]
        validation = [row for row in development if folds[row['group']] == fold]
        require_family_support(training, 2, expected)
        require_family_support(validation, 1, expected)
        model = fit_models(training)
        cross_validation.append(dict(
            overall=model_metrics(validation, model),
            combined_fallback=integer.metrics(validation, model, weighting=d.balanced_weights),
            slices=slices(validation, model)))

    model = fit_models(development)
    model['candidate_contract'] = d.CONTRACT
    coverage = d.coverage(rows)
    model['provenance'] = dict(
        expected_families=list(expected),
        not_required_families=[family for family in d.FAMILIES if family not in expected],
        unmeasured_families=[family for family, value in coverage.items() if not value['episodes']],
        campaign_scope='Declared family coverage; absent families get no expert and no invented examples.',
        collector_contract=a.collector_contract,
        source_collector_contracts=sorted({row.get('collector_contract_id', a.collector_contract) for row in allrows}),
        source_contract_sha256=sorted({row['source_contract_sha256'] for row in allrows}),
        collection_thresholds_milli=sorted({row['collection_threshold_milli'] for row in allrows
                                          if row['collection_threshold_milli'] is not None}),
        manifests=[dict(path=str(path), sha256=d.file_hash(path)) for path in a.manifest],
        split=split, coverage=coverage, age_demand_support=age_demand_support(rows),
        adaptive_policy=dict(minimum_interval=DEPLOYMENT_MINIMUM_INTERVAL_TICKS, forced_age_cap=None, publication_delay=8, threshold=threshold),
        metric_policy='Model-only threshold decisions on all eligible rows; minimum refresh spacing and demanded-age cap are not applied.',
        unknown_or_planning_rows_excluded=len(allrows)-len(rows),
        development_maps=sorted({row['group'] for row in development}),
        heldout_maps=sorted(held), cross_validation=cross_validation,
        holdout=model_metrics(heldout, model),
        holdout_combined_fallback=integer.metrics(heldout, model, weighting=d.balanced_weights),
        holdout_slices=slices(heldout, model),
        holdout_separate_outcomes=outcome_metrics(heldout),
        weighting='equal format/role/cohort/map/family/collection/episode; expert subsets scalar-normalized',
        threshold=threshold,
        note='Research only. Explicit emission and prospective quality/compute validation required.')
    integer.save(a.output, model)


def compact_deployment(model, training_inputs=None, evidence_url=None):
    """Keep inference data and small hash links; full fit reports remain external."""
    validate(model)
    keep = ('schema', 'contract', 'features', 'transforms', 'threshold', 'trained',
            *HEADS, 'family_experts', 'deployment', 'prospective_validation_required')
    result = {key: copy.deepcopy(model[key]) for key in keep if key in model}
    source = model.get('provenance', {})
    registry = source.get('split', {}).get('registry', {})
    provenance = dict(
        expert_fit_sha256=source.get('expert_fit_sha256'),
        combined_fit_sha256=source.get('combined_fit_sha256'),
        expert_fit_metric_policy=source.get('expert_fit_metric_policy'),
        source_ablation_sha256=source.get('source_ablation_sha256'),
        source_generic12_fit_sha256=source.get('source_generic12_fit_sha256'),
        source_fit_sha256=model.get('deployment', {}).get('source_model_sha256'),
        split_registry_sha256=registry.get('sha256', source.get('split_registry_sha256')),
        eligible_episode_count=source.get('eligible_episode_count', sum(value['episodes'] for value in source.get('coverage', {}).values())),
        development_map_count=source.get('development_map_count', len(source.get('development_maps', []))),
        heldout_map_count=source.get('heldout_map_count', len(source.get('heldout_maps', []))),
        expected_families=source.get('expected_families', []),
        unmeasured_families=source.get('unmeasured_families', []),
        collector_contracts=source.get('source_collector_contracts', source.get('collector_contracts',
                                     [source['collector_contract']] if source.get('collector_contract') else [])),
        collection_thresholds_milli=source.get('collection_thresholds_milli', []))
    for key in ('training_input_sha256', 'training_game_slots', 'terminal_counts',
                'analysis_source_identity_sha256', 'evidence_url'):
        if key in source:
            provenance[key] = copy.deepcopy(source[key])
    if training_inputs:
        path = Path(training_inputs)
        inputs = json.loads(path.read_text())
        slots = inputs.get('expected_slots')
        accounted = sum(inputs.get('slot_counts', {}).get(key, 0) for key in ('completed', 'user-censored'))
        if type(slots) is not int or slots <= 0 or accounted != slots:
            raise ValueError('Training input must account for every declared game slot')
        hashes = sorted(inputs['files'][name] for name in inputs['manifests'])
        if hashes != sorted(item['sha256'] for item in source.get('manifests', [])):
            raise ValueError('Training input manifests differ from source fit')
        if inputs['files'].get('map-splits.json') != registry.get('sha256'):
            raise ValueError('Training input grouped splits differ from source fit')
        provenance.update(
            training_input_sha256=d.file_hash(path), training_game_slots=slots,
            terminal_counts=inputs['slot_counts'],
            analysis_source_identity_sha256=inputs['files'].get('ANALYSIS-SOURCE-A16-FROZEN.json'))
    if evidence_url:
        if not evidence_url.startswith('https://'):
            raise ValueError('Evidence URL must be an HTTPS link')
        provenance['evidence_url'] = evidence_url
    result['provenance'] = {key: value for key, value in provenance.items() if value is not None}
    validate(result)
    return result


def emit(a):
    """Deployment changes metadata, never refits or relabels historical metrics."""
    model = json.loads(Path(a.model).read_text())
    if (a.contract != d.CONTRACT_ID or not d.compatible_contract(model.get('candidate_contract'))
            or model.get('schema') != 2 or not model.get('research_only')):
        raise ValueError('Explicit unified research contract required')
    source_threshold = d.threshold_milli(model.get('threshold'))
    integer.validate_heads(model, research=True, expected_threshold=source_threshold)
    has_experts = 'family_experts' in model
    if has_experts:
        validate_family_experts(model)
    model = copy.deepcopy(model)
    threshold = d.threshold_milli(getattr(a, 'threshold_milli', d.DEFAULT_THRESHOLD_MILLI))
    deployment = dict(
        threshold_milli=threshold, minimum_interval_ticks=DEPLOYMENT_MINIMUM_INTERVAL_TICKS,
        source_fit_minimum_interval_ticks=model.get('provenance', {}).get('adaptive_policy', {}).get('minimum_interval'),
        source_fit_threshold_milli=source_threshold,
        source_model_sha256=d.file_hash(a.model),
        source_candidate_contract_sha256=d.canonical_hash(model['candidate_contract']),
        source_fit_metrics_threshold_milli=source_threshold,
        fit_metrics_recomputed=False, coefficients_refit=False)
    if has_experts:
        deployment.update(
            strategy='family-experts-with-combined-fallback',
            demanded_age_cap_ticks=DEPLOYMENT_AGE_CAP_TICKS,
            source_fit_age_cap_ticks=model.get('provenance', {}).get('adaptive_policy', {}).get('forced_age_cap'),
            source_fit_metric_policy='Model-only threshold decisions on all eligible rows; historical metrics apply neither minimum refresh spacing nor a demanded-age cap.')
    model.update(schema=5 if has_experts else 4, contract=d.CONTRACT, threshold=threshold,
                 trained=True, prospective_validation_required=True, deployment=deployment)
    model.pop('research_only', None)
    validate(model)
    from tools.adaptive_gradient_codegen import render
    header = render(model)
    if getattr(a, 'compact', False):
        model = compact_deployment(model, getattr(a, 'training_inputs', None), getattr(a, 'evidence_url', None))
        if render(model) != header:
            raise ValueError('Metadata compaction changed generated inference')
    integer.save(a.output, model)
    Path(a.header).write_text(header)


def main():
    parser = argparse.ArgumentParser()
    actions = parser.add_subparsers(dest='action', required=True)
    fit_parser = actions.add_parser('fit')
    fit_parser.add_argument('--manifest', action='append', required=True)
    fit_parser.add_argument('--split-registry', required=True)
    fit_parser.add_argument('--output', required=True)
    fit_parser.add_argument('--expected-families', default=','.join(d.FAMILIES),
                            help='Required campaign families; does not filter observed rows')
    fit_parser.add_argument('--collector-contract',
                            choices=['unified-gradient-h128-v2', 'unified-gradient-h128-v4',
                                     'unified-gradient-spatial-h128-v3'],
                            default='unified-gradient-h128-v4')
    fit_parser.add_argument('--threshold-milli', type=d.threshold_milli, default=d.DEFAULT_THRESHOLD_MILLI)
    fit_parser.set_defaults(run=fit)
    emit_parser = actions.add_parser('emit')
    emit_parser.add_argument('--model', required=True)
    emit_parser.add_argument('--contract', choices=[d.CONTRACT_ID], required=True)
    emit_parser.add_argument('--output', required=True)
    emit_parser.add_argument('--header', required=True)
    emit_parser.add_argument('--threshold-milli', type=d.threshold_milli, default=d.DEFAULT_THRESHOLD_MILLI)
    emit_parser.add_argument('--compact', action='store_true',
                             help='Keep deployment and hash-linked provenance; preserve the full fit externally')
    emit_parser.add_argument('--training-inputs', help='Immutable terminal input receipt for compact provenance')
    emit_parser.add_argument('--evidence-url', help='Published full training and validation evidence')
    emit_parser.set_defaults(run=emit)
    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
