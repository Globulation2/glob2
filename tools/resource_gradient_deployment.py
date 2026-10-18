# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit research-to-engine boundary for the selected integer model.

Emitting a validation candidate is deliberate and reviewable. It does not
establish prospective quality/performance or authorize production promotion.
"""
import copy
import json
from pathlib import Path
from tools import resource_gradient_features as f
from tools.resource_gradient_dataset import canonical_hash

LEGACY_SOURCE_SHA256 = 'f954f80f1108a8c64adb6d95e8c018bbad62ad9fb3e3bdbbe0d8d17204578217'
LEGACY_HEADS_SHA256 = 'b8bb9e0a6c7aa5bae79a73a66bc9264d2ab37373d496a2c24d64750064551e25'


def legacy_core(model):
    from tools import resource_gradient_model as m
    if model.get('schema') == 3:
        model = model['legacy']
    # Validate transform metadata before extracting the immutable head core;
    # silently discarding an identity transform would diverge from Python.
    m.validate_heads(model)
    core = {key: copy.deepcopy(model[key]) for key in ('schema', 'features', 'threshold', *m.HEADS)}
    if canonical_hash(core) != LEGACY_HEADS_SHA256:
        raise ValueError('Legacy fallback must retain the exact incumbent integer heads')
    return core


def validate_candidate(model):
    from tools import resource_gradient_model as m
    if type(model.get('schema')) is not int or model['schema'] != 2:
        raise ValueError('Research candidate schema2 required')
    m.validate_heads(model, research=True)
    if (tuple(model['features']) != f.FEATURES or tuple(model.get('transforms', ())) != f.TRANSFORMS
            or model.get('candidate_contract') != f.CONTRACT):
        raise ValueError('Candidate differs from the selected production feature contract')


def validate(model):
    from tools import resource_gradient_model as m
    if (type(model.get('schema')) is not int or model['schema'] != 3
            or model.get('deployment_contract') != f.CONTRACT or 'research_only' in model):
        raise ValueError('Explicit deployment schema3 required; research models cannot emit')
    research = dict(model, schema=2, research_only=True, candidate_contract=f.CONTRACT)
    validate_candidate(research)
    if model.get('legacy_source_sha256') != LEGACY_SOURCE_SHA256:
        raise ValueError('Unknown legacy fallback identity')
    legacy_core(model)
    if model.get('prospective_validation_required') is not True:
        raise ValueError('Deployment candidate must preserve the prospective validation requirement')


def promote_model(candidate, incumbent):
    validate_candidate(candidate)
    model = copy.deepcopy(candidate)
    model.pop('candidate_contract')
    model.pop('research_only')
    model.update(schema=3, deployment_contract=copy.deepcopy(f.CONTRACT),
                 legacy=legacy_core(incumbent), legacy_source_sha256=LEGACY_SOURCE_SHA256,
                 prospective_validation_required=True)
    validate(model)
    return model


def fit_candidate(args):
    from tools import resource_gradient_model as m
    from tools.resource_gradient_dataset import registered_split, support_audit
    if args.feature_set != f.CONTRACT_ID:
        raise ValueError('Unknown selected feature set')
    all_rows = m.dataset(args.manifest)
    rows, excluded = f.prepare_rows(all_rows)
    # Preserve expected formats even if all their labels were filtered away.
    expected = set()
    game_ids = set()
    from tools.resource_gradient_dataset import manifest_records
    for path in args.manifest:
        records, _ = manifest_records(path)
        expected.update(record['format'] for record in records)
        game_ids.update((record.get('group'), record.get('binary_sha256'),
                         record.get('episode_sha256')) for record in records)
    coverage = {fmt: len({r['group'] for r in rows if r['format'] == fmt}) for fmt in expected}
    if any(n < 4 for n in coverage.values()):
        raise ValueError(f'Insufficient known-capture movement map coverage: {coverage}')
    held, folds, split = registered_split(rows, args.split_registry, m.split_maps)
    development = [r for r in rows if r['group'] not in held]
    test = [r for r in rows if r['group'] in held]
    cv = []
    for fold in range(3):
        training = [r for r in development if folds[r['group']] != fold]
        validation = [r for r in development if folds[r['group']] == fold]
        cv.append(m.metrics(validation, m.fit_heads(training, f.FEATURES, f.TRANSFORMS)))
    model = m.fit_heads(development, f.FEATURES, f.TRANSFORMS)
    model['candidate_contract'] = copy.deepcopy(f.CONTRACT)
    model['provenance'] = dict(
        manifests=[dict(name=Path(p).name, sha256=m.sha(p)) for p in args.manifest],
        training_games=len(game_ids), eligible_movement_episodes=len(rows),
        development_states=sorted({r['group'] for r in development}), heldout_states=sorted(held),
        cross_validation=cv, holdout=m.metrics(test, model), holdout_slices=m.metric_slices(test, model),
        split_coverage=split, support=support_audit(all_rows), excluded=excluded,
        weighting='equal format, role, cohort, map, collection, episode',
        regularization=dict(logistic_C=1, ridge_alpha=100),
        note='Research candidate only; explicit promotion and prospective engine validation required.')
    validate_candidate(model)
    m.save(args.output, model)
    print(json.dumps(model['provenance'], indent=2))


def promote(args):
    from tools import resource_gradient_model as m
    if args.contract != f.CONTRACT_ID:
        raise ValueError('Explicit selected contract acknowledgment required')
    candidate = json.loads(Path(args.model).read_text())
    incumbent = json.loads(Path(args.incumbent).read_text())
    model = promote_model(candidate, incumbent)
    # Validate everything before writing either destination.
    from tools.resource_gradient_codegen import render
    header = render(model)
    m.save(args.output, model)
    if args.header:
        Path(args.header).write_text(header)


def add_commands(sub):
    from tools import resource_gradient_model as m
    p = sub.add_parser('fit-candidate', help='Fit selected features; produces researchschema2, never C++')
    p.add_argument('--manifest', action='append', required=True)
    p.add_argument('--feature-set', choices=[f.CONTRACT_ID], required=True)
    p.add_argument('--split-registry', required=True)
    p.add_argument('--output', required=True)
    p.set_defaults(action=fit_candidate)
    p = sub.add_parser('promote', help='Explicitly emit a selected engine candidate; prospective validation remains required')
    p.add_argument('--model', required=True)
    p.add_argument('--contract', choices=[f.CONTRACT_ID], required=True)
    p.add_argument('--incumbent', default=str(m.DEFAULT_MODEL))
    p.add_argument('--output', required=True)
    p.add_argument('--header')
    p.set_defaults(action=promote)
