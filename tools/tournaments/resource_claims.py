"""Temporary resource-claim campaign planning and game-clustered shadow analysis.

Planning delegates all game draws to the existing ai_comparison Planner. This
module never starts workers. Analysis cannot turn incomplete/missing games into
zero-regret observations and never pools decision counts as independent games.
"""
import argparse
from collections import defaultdict, OrderedDict
import gzip
import io
import json
from pathlib import Path
import shutil
import statistics
import sys

from .bundles import inspect_bundle, package_identity
from .common import atomic_json, digest, file_hash, store_artifact
from .experiments import Planner
from .fairness_statistics import bootstrap
from .model import job, validate_experiment
from .results import Results

ARTIFACT = 'resource-claims-shadow.jsonl.gz'
FARMING_AIS = ('nicowar', 'econo', 'maxima', 'cortex', 'cabino')
COLUMNS = ('pattern', 'policy', 'source', 'access', 'probe', 'weighted_cost',
           'probes', 'new_destination_reads', 'fallback', 'access_weighted_cost')
SCENARIOS = {
    'dense_material': 'At least 1/128 of map cells supply the queried material.',
    'sparse_material': 'At most 1/1024 of map cells supply the queried material.',
    'late_stock_contention': 'At least tick 60000 with at least 100 units in the sampled colony.',
    'protected_farming': 'Food contention next to forbidden crop protection with pooled farm areas disabled (observational proxy).',
    'pooled_farming': 'An actual pooled-farm decision.',
    'exhausted_pool': 'A connected farm with no surplus above its seed reserve.',
    'water_algae': 'An actual algae material decision.',
    'finite_persistent_yield': 'A finite yield on a deposit that persists when empty, including ordinary renewable fruit.',
    'ordinary_fruit': 'Finite renewable cherry/orange/prune material (5/6/7) on a persistent deposit.',
    'compound_yield': 'A deposit with more than one material yield.',
    'destructive_yield': 'An all-stock or deposit-destroying yield.',
}


def scenario_hits(row):
    features = row.get('features', {})
    hits = set()
    size = row['width']*row['height']
    if row['source_count']*128 >= size: hits.add('dense_material')
    if row['source_count']*1024 <= size: hits.add('sparse_material')
    if row['tick'] >= 60000 and features.get('team_units', 0) >= 100: hits.add('late_stock_contention')
    if row['material'] == 1 and features.get('farm_areas_enabled') is False and features.get('nearby_forbidden_count', 0): hits.add('protected_farming')
    if features.get('pooled_farm'): hits.add('pooled_farming')
    if features.get('exhausted_pool'): hits.add('exhausted_pool')
    if row['material'] == 4: hits.add('water_algae')
    if features.get('resource_persists_when_empty') and features.get('infinite') is False:
        hits.add('finite_persistent_yield')
        if row['material'] in (5,6,7) and isinstance(features.get('resource_growth_rate'),int) and features['resource_growth_rate']>0:
            hits.add('ordinary_fruit')
    material_mask=features.get('resource_material_mask')
    if isinstance(material_mask,int) and material_mask.bit_count()>1: hits.add('compound_yield')
    if features.get('destroys_deposit') or features.get('yield_consumption') in (1, 'all'): hits.add('destructive_yield')
    return hits


def coverage_audit(games, *, min_games=5, min_events=100):
    rows, missing = [], []
    for scenario, requirement in SCENARIOS.items():
        counts = [game['scenario_counts'].get(scenario, 0) for _, game in games]
        uncensored = [game['scenario_counts'].get(scenario, 0) for _, game in games if not game['sampling']['censored']]
        observed_games, events = sum(count > 0 for count in counts), sum(counts)
        row = {'scenario': scenario, 'requirement': requirement, 'games': observed_games, 'events': events,
               'minimum_games': min_games, 'minimum_events': min_events,
               'uncensored_games': sum(count > 0 for count in uncensored), 'uncensored_events': sum(uncensored),
               'covered': sum(count > 0 for count in uncensored) >= min_games and sum(uncensored) >= min_events}
        rows.append(row)
        if not row['covered']:
            missing.append({'scenario': scenario, 'minimum_additional_games': max(0, min_games-row['uncensored_games']),
                            'minimum_additional_events': max(0, min_events-row['uncensored_events']),
                            'action': 'Allocate distinct frozen scenario saves; preflight and rerun the coverage audit.'})
    censored_games = sum(game['sampling']['censored'] for _, game in games)
    return {'ready_for_policy_selection': bool(games) and not missing and not censored_games,
            'censored_games': censored_games, 'strata': rows,
            'supplemental_required': missing,
            'not_contested_by_design': ['Infinite yields (ordinary rocks/stone) do not exhaust claim capacity. Verify their source/kernel routing and delivery behavior separately; destructive conflicts are exceptional.'],
            'note': 'Coverage requires uncensored samples. Dropping censored games is not a correction for contention-correlated missingness; rerun calibrated cohorts or supply an explicit weighting design. Labels alone do not establish coverage.'}


def supplemental_plan(bundle, fixtures, artifact_directory):
    """Freeze distinct supplied saves; never pretend repeated identical games add coverage."""
    if bundle['capabilities'].get('resource_claims_shadow_version') != 1:
        raise ValueError('bundle does not advertise resource claim shadow version 1')
    artifact_directory = Path(artifact_directory)
    artifact_directory.mkdir(parents=True, exist_ok=True)
    total, seen, jobs = 0, set(), []
    for fixture in fixtures:
        path = Path(fixture['path'])
        strata = fixture['strata']
        if not isinstance(strata, list) or not strata or set(strata)-SCENARIOS.keys():
            raise ValueError('supplemental fixture needs known scenario strata')
        start, window = fixture['start_tick'], fixture.get('window_ticks', 30000)
        if type(start) is not int or start < 0 or type(window) is not int or window < 1:
            raise ValueError('fixture needs saved start_tick and positive window_ticks')
        size = path.stat().st_size
        total += size
        if size > 256*1024**2 or total > 2*1024**3 or shutil.disk_usage(artifact_directory).free < size+8*1024**3:
            raise ValueError('supplemental fixture storage budget/reserve exceeded')
        raw = file_hash(path)
        if raw in seen:
            raise ValueError('identical saved state is not an independent supplemental game')
        seen.add(raw)
        artifact = store_artifact(path, artifact_directory)
        jobs.append(job('game', bundle['id'], inputs={'save': artifact},
            config={'ticks': start+window, 'resource_claims_shadow': {
                'sample_every': 100, 'max_samples': 512, 'max_bytes': 16*1024**2}},
            outputs={'required': [ARTIFACT], 'result': 'full', 'telemetry': ['team-timeline'], 'log_mode': 'win-probability'},
            limits={'timeout_seconds': 7200, 'artifact_bytes': 64*1024**2, 'log_bytes': 4*1024**2},
            labels={'claim_cohort': 'supplemental', 'scenario_intent': strata, 'start_tick': start,
                    'fixture_sha256': raw, 'material_destinations': 'saved_state', 'preflight_required': True}))
    return validate_experiment({'schema_version': 1, 'id': 'claims-supplemental-'+digest(sorted(seen))[:12],
        'kind': 'ai_comparison', 'jobs': jobs,
        'settings': {'artifact_budget_bytes': 32*1024**3, 'disk_reserve_bytes': 8*1024**3},
        'labels': {'purpose': 'resource-claim-shadow-supplemental', 'preflight_required': True},
        'design': {'fixture_budget_bytes': 2*1024**3, 'sampling': 'distinct frozen saves; thresholds and AI configuration inherited'}})


def plan(bundle, *, games=32, seed=1, generators=None, generator_overrides=None):
    if bundle['capabilities'].get('resource_claims_shadow_version') != 1:
        raise ValueError('bundle does not advertise resource claim shadow version 1')
    if type(games) is not int or games < 4 or games % 4:
        raise ValueError('games must be a positive multiple of four: half broad, quarter protected, quarter pooled')
    available = [a['name'] for a in bundle['capabilities']['ais'] if a['id'] != 0 and a['name'] != 'javascript']
    farming = [name for name in FARMING_AIS if name in available]
    if not available or not farming:
        raise ValueError('bundle requires selectable native and farming AIs')
    configs, games_out = [], []
    settings = {'artifact_budget_bytes': 32*1024**3, 'disk_reserve_bytes': 8*1024**3}
    families = (('broad', 'default', available, seed, games//2, 0.5),
                ('farming_stress', 'protected', farming, seed ^ 0x6A09E667, games//4, 0.25),
                ('farming_stress', 'pooled', farming, seed ^ 0xBB67AE85, games//4, 0.25))
    for cohort, farm_mode, names, cohort_seed, count, family_weight in families:
        stress = cohort == 'farming_stress'
        config = {'id': 'claims-'+cohort+'-'+farm_mode, 'sample_games': count, 'sample_seed': cohort_seed,
                  'ais': names, 'formats': ['ffa'] if stress else ['1v1', '2v2', 'ffa'],
                  'seat_sampling': 'with_replacement',
                  'sizes': [{'width': n, 'height': n} for n in ((8, 9) if stress else (7, 8, 9))],
                  'ticks': 180000 if stress else 90000,
                  'timeout_seconds': 7200 if stress else 3600,
                  'win_probability_permille': 0 if stress else 950,
                  'material_destinations': True,
                  'resource_claims_shadow': {'sample_every': 1000, 'max_samples': 512, 'max_bytes': 16*1024**2},
                  'outputs': {'required': [ARTIFACT], 'result': 'full', 'log_mode': 'win-probability', 'telemetry': ['team-timeline']},
                  'settings': settings, 'generator_overrides': generator_overrides or {}}
        if stress:
            config['ffa_colonies'] = [4, 8, 16]
            config['experiments'] = ['farm-areas'] if farm_mode == 'pooled' else []
        if generators is not None:
            config['generators'] = generators
        drawn = Planner('ai_comparison', config, [bundle]).plan()
        configs.append(config)
        for value in drawn['jobs']:
            labels = value['labels'] | {'claim_cohort': cohort, 'cohort_weight': 0.5,
                'farm_mode': farm_mode, 'family_weight': family_weight, 'material_destinations': True,
                'colonies': len(value['config']['players']), 'preflight_required': True}
            limits = value['limits'] | {'artifact_bytes': 64*1024**2, 'log_bytes': 4*1024**2}
            games_out.append(job(value['type'], value['build'], inputs=value['inputs'], seeds=value['seeds'],
                config=value['config'], outputs=value['outputs'], limits=limits, labels=labels,
                depends_on=value['depends_on']))
    manifest = {'schema_version': 1, 'id': f'claims-{games}-{seed}', 'kind': 'ai_comparison',
                'jobs': games_out, 'settings': settings,
                'labels': {'purpose': 'resource-claim-shadow', 'claims_enforced': False, 'material_destinations': True,
                           'preflight_required': True, 'does_not_launch': True},
                'design': {'cohorts': configs, 'cohort_weights': {'broad': 0.5, 'farming_stress': 0.5},
                    'broad_format_weights': {'1v1': 1/3, '2v2': 1/3, 'ffa': 1/3},
                    'stress_farm_mode_weights': {'protected': 0.5, 'pooled': 0.5},
                    'family_weights': {'broad': 0.5, 'protected_farming': 0.25, 'pooled_farming': 0.25},
                    'stress_strata': [{'farm_mode': mode, 'colonies': n, 'width': 1<<size, 'height': 1<<size, 'weight': 1/12}
                                      for mode in ('protected', 'pooled') for n in (4, 8, 16) for size in (8, 9)],
                    'sampling': 'independent with replacement; strata weights are design weights, not guaranteed realized counts',
                    'analysis': 'report broad/stress separately; game-clustered intervals; no automatic policy selection'}}
    return validate_experiment(manifest)


def map_input_identity(request):
    config=request['config']
    return digest([request['build'], config['generator'], config['params'],
                   config.get('candidates',5), request['seeds']['map']])


def conditional_pool(bundle, *, games=32, seed=1, attempt_multiplier=10, sample_every=256,
                     max_samples=32768, stress_timeout_seconds=21600):
    """Freeze complete tuple draws before preflight, never repair sampled tuples."""
    if type(games) is not int or games<4 or games%4:
        raise ValueError('conditional games must be a positive multiple of four')
    if type(attempt_multiplier) is not int or not 1<=attempt_multiplier<=10:
        raise ValueError('conditional attempt multiplier must be in1..10')
    if type(sample_every) is not int or not 1<=sample_every<=1000000000:
        raise ValueError('invalid conditional sampling rate')
    if type(max_samples) is not int or not 1<=max_samples<=1000000:
        raise ValueError('invalid conditional sample cap')
    if type(stress_timeout_seconds) is not int or not 1<=stress_timeout_seconds<=86400:
        raise ValueError('invalid conditional stress timeout')
    sampling={'sample_every':sample_every,'max_samples':max_samples,'max_bytes':128*1024**2}
    pool=plan(bundle,games=games*attempt_multiplier,seed=seed)
    ordinals=defaultdict(int);requests=[]
    for old in pool['jobs']:
        mode=old['labels']['farm_mode'];ordinal=ordinals[mode];ordinals[mode]+=1
        config=old['config']|{'resource_claims_shadow':dict(sampling)}
        labels=old['labels']|{'conditional_draw':ordinal,'map_input_identity':map_input_identity(old)}
        requests.append(job(old['type'],old['build'],inputs=old['inputs'],seeds=old['seeds'],config=config,
            outputs=old['outputs'],limits=old['limits']|{'artifact_bytes':192*1024**2,
                'timeout_seconds':stress_timeout_seconds if mode!='default' else old['limits']['timeout_seconds']},labels=labels,depends_on=old['depends_on']))
    pool['jobs']=requests;pool['id']=f'claims-conditional-pool-{games}-{seed}'
    for cohort in pool['design']['cohorts']:
        cohort['resource_claims_shadow']=dict(sampling)
        if cohort['id']!='claims-broad-default':cohort['timeout_seconds']=stress_timeout_seconds
    pool['design']['pilot_sampling']=sampling
    pool['labels']['purpose']='preflight-candidate-pool-do-not-dispatch-games'
    pool['design']['distribution_conditioning']='Listed family/format/size weights describe proposals; accepted joint distribution is conditional on map validity and reported empirically.'
    pool['design']['conditional_selection']={'targets':{'default':games//2,'protected':games//4,'pooled':games//4},
        'attempt_multiplier':attempt_multiplier,'maximum_attempts':games*attempt_multiplier,
        'policy':'First valid full-tuple draws in each frozen seeded family sequence; retain invalid draws and reasons; never repair size/count/generator.'}
    return validate_experiment(pool)


def select_conditional_pool(pool, outcomes):
    """Outcomes map frozen map-input identity to category and rejection reason.

    Missing or infrastructure-failed preflights block selection. They are not
    treated as invalid random draws or silently skipped to convenient successes.
    """
    design=pool['design']['conditional_selection'];targets=design['targets']
    selected=[];rejected=[];pending=[];infrastructure=[];exhausted=[];attempted=defaultdict(int)
    for mode,target in targets.items():
        accepted=0
        for request in (j for j in pool['jobs'] if j['labels']['farm_mode']==mode):
            if accepted==target:break
            key=request['labels']['map_input_identity'];outcome=outcomes.get(key)
            if outcome is None:pending.append(key);break
            category=outcome.get('category')
            if category not in ('success','invalid_request','generation_failed'):
                infrastructure.append({'map_input_identity':key,'category':category});break
            attempted[mode]+=1
            if category=='success':selected.append(request);accepted+=1
            else:
                if not outcome.get('diagnostic'):raise ValueError('rejected draw requires retained reason')
                rejected.append({'job':request['id'],'map_input_identity':key,'family':mode,
                    'draw':request['labels']['conditional_draw'],'category':category,'diagnostic':outcome['diagnostic']})
        if accepted<target and not any(j['labels']['farm_mode']==mode and j['labels']['map_input_identity'] in pending for j in pool['jobs']) and not infrastructure:
            exhausted.append(mode)
    ready=not pending and not infrastructure and not exhausted and len(selected)==sum(targets.values())
    report={'ready':ready,'selected':len(selected),'pending_preflights':pending,'infrastructure_failures':infrastructure,
        'attempt_cap_exhausted':exhausted,'rejections':rejected,'attempted_by_family':dict(attempted),
        'rejection_rate':len(rejected)/sum(attempted.values()) if attempted else None,
        'realized_distribution':dict(__import__('collections').Counter(
            str((j['labels']['farm_mode'],j['labels']['format'],j['config']['generator'],j['config']['params']['width'],len(j['config']['players']))) for j in selected))}
    if not ready:return None,report
    manifest=json.loads(json.dumps(pool))
    manifest['jobs']=[job(j['type'],j['build'],inputs=j['inputs'],seeds=j['seeds'],config=j['config'],
        outputs=j['outputs'],limits=j['limits'],labels=j['labels']|{'preflight_required':False},depends_on=j['depends_on']) for j in selected]
    manifest['id']=pool['id'].replace('conditional-pool','conditional-valid')
    manifest['labels']['purpose']='resource-claims-conditional-valid-pilot'
    manifest['design']['preflight_selection']=report
    manifest['design']['paired_inputs']='Reuse this exact valid cohort across policies; no policy-dependent resampling.'
    return validate_experiment(manifest),report


def trace_rows(stream):
    """Validate complete stream framing while yielding decisions and the schema."""
    schema = json.loads(next(stream, 'null'))
    if (not isinstance(schema, dict) or schema.get('kind') != 'schema' or schema.get('version') != 1
            or tuple(schema.get('outcome_columns', ())) != COLUMNS
            or not isinstance(schema.get('patterns'), list) or not schema['patterns']
            or not isinstance(schema.get('policies'), list) or not schema['policies']):
        raise ValueError('unsupported resource claim schema')
    if len(set(schema['patterns'])) != len(schema['patterns']) or len(set(schema['policies'])) != len(schema['policies']):
        raise ValueError('duplicate pattern/policy identities')
    from .claim_journeys import Validator
    journeys = Validator(schema)
    movement_only = schema.get('movement_only', False)
    if type(movement_only) is not bool:
        raise ValueError('invalid movement-only mode')
    if 'decision_sampling_enabled' in schema and schema['decision_sampling_enabled'] is not (not movement_only):
        raise ValueError('inconsistent decision sampling mode')
    if movement_only and (schema.get('movement_audit_version') != 1 or schema.get('decision_sampling_enabled') is not False):
        raise ValueError('movement-only trace requires movement audit and disabled decision sampling')
    yield schema
    count, summary, previous = 0, None, -1
    for line in stream:
        row = json.loads(line)
        if not isinstance(row, dict):
            raise ValueError('claim trace row must be an object')
        if summary is not None:
            raise ValueError('rows after claim summary')
        if row.get('kind') == 'summary':
            summary = row
        elif row.get('kind') == 'journey':
            if movement_only:
                raise ValueError('journey in movement-only trace')
            journeys.row(row)
            yield row
        elif row.get('kind') == 'decision':
            if movement_only:
                raise ValueError('decision in movement-only trace')
            if type(row.get('ordinal')) is not int or row['ordinal'] <= previous:
                raise ValueError('duplicate or unordered claim event')
            previous = row['ordinal']
            if 'claims_enforced' in schema:
                enforced = row.get('claims_enforced', schema['claims_enforced'])
                if type(enforced) is not bool or schema['claims_enforced'] is not None and enforced is not schema['claims_enforced']:
                    raise ValueError('missing or inconsistent per-event claim mode')
                if enforced and row.get('active_policy') not in ('prototype-stationary-v1', 'experimental'):
                    raise ValueError('unknown active claim policy')
                if not enforced and row.get('active_policy', 'disabled') != 'disabled':
                    raise ValueError('inactive event advertises an active policy')
            outcomes = row.get('patterns')
            if not isinstance(outcomes, list) or len(outcomes) != len(schema['patterns'])*len(schema['policies']):
                raise ValueError('missing pattern outcomes')
            seen = set()
            for outcome in outcomes:
                if not isinstance(outcome, list) or len(outcome) != len(COLUMNS):
                    raise ValueError('invalid pattern outcome')
                pattern, policy = outcome[:2]
                if type(pattern) is not int or type(policy) is not int or not 0 <= pattern < len(schema['patterns']) or not 0 <= policy < len(schema['policies']) or (pattern, policy) in seen:
                    raise ValueError('invalid/duplicate pattern policy index')
                seen.add((pattern, policy))
            count += 1
            yield row
        else:
            raise ValueError('unknown claim trace row')
    if not summary or summary.get('complete') is not True or summary.get('submitted') != count or summary.get('completed') != count:
        raise ValueError('incomplete resource claim trace')
    journeys.finish(summary)
    if movement_only and any(summary.get(key) != 0 for key in ('decisions','sampled','submitted','completed','capture_bytes')):
        raise ValueError('movement-only trace contains decision work')
    yield summary


def read_game(stream, labels):
    rows = trace_rows(stream)
    schema = next(rows)
    # Each accumulator belongs to one game, pattern, policy and stratum.
    sums = defaultdict(lambda: defaultdict(float))
    route_sums = defaultdict(lambda: defaultdict(float))
    scenario_counts = defaultdict(int)
    diagnostics = {'events': 0, 'oracle_elapsed_ns': 0, 'static_route_elapsed_ns': 0, 'oracle_timing_missing': 0, 'route_timing_missing': 0, 'timing_clock': schema.get('timing_clock', 'legacy_steady_clock_elapsed'), 'published_xy_events': 0, 'missing_xy_events': 0, 'first_sample_tick': None, 'last_sample_tick': None, 'late_sample_events': 0}
    summary = None
    modes = set()
    journey_digests = set()
    journey_totals = {'rows':0,'steps':0,'live_cost':0,'partial':0,'baseline_missing':0,'terminals':defaultdict(int)}
    for row in rows:
        if row['kind'] == 'summary':
            summary = row
            continue
        if row['kind'] == 'journey':
            journey_digests.add(row['policy_digest'])
            journey_totals['rows'] += 1
            journey_totals['steps'] += row['steps'];journey_totals['live_cost'] += row['live_cost']
            journey_totals['partial'] += row['terminal'] in ('timeout','end_of_segment')
            journey_totals['baseline_missing'] += row['baseline_status'] != 'reached'
            journey_totals['terminals'][row['terminal']] += 1
            continue
        modes.add((row.get('claims_enforced', schema.get('claims_enforced', False)), row.get('active_policy', 'disabled')))
        for scenario in scenario_hits(row):
            scenario_counts[scenario] += 1
        diagnostics['events'] += 1
        diagnostics['first_sample_tick'] = row['tick'] if diagnostics['first_sample_tick'] is None else min(diagnostics['first_sample_tick'], row['tick'])
        diagnostics['last_sample_tick'] = row['tick'] if diagnostics['last_sample_tick'] is None else max(diagnostics['last_sample_tick'], row['tick'])
        diagnostics['late_sample_events'] += row['tick'] >= 60000
        published_xy = row.get('destination_lookup') == 'published-xy' and row.get('destination_xy_missing') is False
        diagnostics['published_xy_events'] += published_xy
        diagnostics['missing_xy_events'] += not published_xy
        for canonical, legacy, missing in (('oracle_elapsed_ns','oracle_ns','oracle_timing_missing'),
                                            ('static_route_elapsed_ns','static_route_cpu_ns','route_timing_missing')):
            value=row.get(canonical,row.get(legacy))
            if value is None:diagnostics[missing]+=1
            elif type(value) is not int or value<0:raise ValueError('invalid elapsed timing')
            else:diagnostics[canonical]+=value
        optimum = row.get('optimal')
        optimum = None if optimum is None else optimum.get('distance')
        if optimum is not None and (type(optimum) is not int or optimum < 0):
            raise ValueError('invalid oracle distance')
        strata = [('all', 'all'), ('cohort', labels.get('claim_cohort', 'unknown')),
                  ('farm_mode', labels.get('farm_mode', 'unknown')),
                  ('material', str(row['material'])), ('movement', str(row['movement'])),
                  ('map', f"{row['width']}x{row['height']}"),
                  ('colonies', str(labels.get('colonies', 'unknown'))),
                  ('decision_phase', row.get('phase', 'unknown')),
                  ('active_policy', row.get('active_policy', 'disabled')),
                  ('decision_reason', row.get('reason', 'unknown')),
                  ('decision_kind', row.get('decision_kind', 'legacy_resource_decision')),
                  ('destination_lookup', row.get('destination_lookup', 'unknown')),
                  ('game_phase', 'early' if row['tick'] < 30000 else 'middle' if row['tick'] < 90000 else 'late'),
                  ('sources', 'sparse' if row['source_count'] < 32 else 'medium' if row['source_count'] < 256 else 'dense')]
        for pattern, policy, source, access, probe, cost, probes, reads, fallback, access_cost in row['patterns']:
            if (type(fallback) is not bool or type(probes) is not int or probes < 0 or
                    any(c is not None and (type(c) is not int or c < 0) for c in (cost, access_cost))):
                raise ValueError('invalid pattern measurement')
            if fallback and (cost is not None or access_cost is not None):
                raise ValueError('fallback must not have a travel cost')
            if cost is not None and optimum is None:
                raise ValueError('reachable policy result without reachable optimum')
            if cost is not None and cost < optimum:
                raise ValueError('policy cost below exact oracle')
            if access_cost is not None and (cost is None or access_cost < cost):
                raise ValueError('access cost cannot be below the source lower bound')
            identity = (schema['patterns'][pattern], schema['policies'][policy])
            for stratum in strata:
                value = sums[identity+stratum]
                value['events'] += 1; value['fallback'] += fallback
                value['unreachable'] += not fallback and cost is None
                value['access_unreachable'] += not fallback and access_cost is None
                value['oracle_missing'] += optimum is None
                value['probes'] += probes
                if cost is not None:
                    value['compared'] += 1; value['regret'] += cost-optimum
                    if optimum > 0:
                        value['ratios'] += 1; value['ratio'] += cost/optimum
                    elif cost == 0:
                        value['ratios'] += 1; value['ratio'] += 1
                    else:
                        value['zero_optimum_positive_cost'] += 1
                if access_cost is not None:
                    value['access_compared'] += 1; value['access_regret'] += access_cost-optimum
                    if optimum>0 or access_cost==0:
                        value['access_ratios']+=1;value['access_ratio']+=access_cost/optimum if optimum else 1
                    else:value['zero_optimum_positive_access_cost']+=1
        for route in row.get('static_routes', []):
            for strategy, optimum_key in (('direct_source_component', 'source_optimal_cost'),
                                          ('via_probe_selected_access', 'access_optimal_cost')):
                measured = route[strategy]
                status, cost, optimum = measured['status'], measured['cost'], route[optimum_key]
                if status not in ('reached', 'unreachable', 'expansion_limit', 'cost_limit', 'contradiction', 'continuation_limit'):
                    raise ValueError('unknown static route status')
                reached = status == 'reached'
                if reached != (cost is not None) or (reached and (optimum is None or cost < optimum)):
                    raise ValueError('static route cost contradicts its oracle/status')
                for stratum in strata:
                    value = route_sums[(strategy, 'static')+stratum]
                    value['events'] += 1; value['failure'] += not reached
                    value['unreachable'] += status == 'unreachable'
                    value['limited'] += status.endswith('_limit')
                    value['contradiction'] += status == 'contradiction'
                    value['expansions'] += measured['expansions']
                    value['continuation_steps'] += measured.get('continuation_steps', 0)
                    if reached:
                        value['compared'] += 1; value['regret'] += cost-optimum
                        if optimum > 0 or cost == 0:
                            value['ratios'] += 1; value['ratio'] += cost/optimum if optimum else 1
                        else:
                            value['zero_optimum_positive_cost'] += 1
    metrics = {}
    for key, value in sums.items():
        metrics[key] = {'events': int(value['events']), 'compared': int(value['compared']),
            'mean_regret': value['regret']/value['compared'] if value['compared'] else None,
            'mean_access_regret': value['access_regret']/value['access_compared'] if value['access_compared'] else None,
            'mean_ratio': value['ratio']/value['ratios'] if value['ratios'] else None,
            'mean_access_ratio': value['access_ratio']/value['access_ratios'] if value['access_ratios'] else None,
            'zero_optimum_positive_access_cost': int(value['zero_optimum_positive_access_cost']),
            'fallback_rate': value['fallback']/value['events'],
            'unreachable_rate': value['unreachable']/value['events'],
            'access_unreachable_rate': value['access_unreachable']/value['events'],
            'oracle_missing_rate': value['oracle_missing']/value['events'],
            'mean_probes': value['probes']/value['events'],
            'zero_optimum_positive_cost': int(value['zero_optimum_positive_cost'])}
    route_metrics = {}
    for key, value in route_sums.items():
        route_metrics[key] = {'events': int(value['events']), 'compared': int(value['compared']),
            'zero_optimum_positive_cost': int(value['zero_optimum_positive_cost']),
            'mean_regret': value['regret']/value['compared'] if value['compared'] else None,
            'mean_ratio': value['ratio']/value['ratios'] if value['ratios'] else None,
            'failure_rate': value['failure']/value['events'], 'unreachable_rate': value['unreachable']/value['events'],
            'limited_rate': value['limited']/value['events'], 'contradiction_rate': value['contradiction']/value['events'],
            'mean_expansions': value['expansions']/value['events'],
            'mean_continuation_steps': value['continuation_steps']/value['events']}
    exclusions = {key: summary.get(key) for key in ('excluded_sample_limit', 'excluded_queue_limit')}
    for count in exclusions.values():
        if count is not None and (type(count) is not int or count < 0):
            raise ValueError('invalid sampling exclusion count')
    reasons = [key for key, count in exclusions.items() if count is None or count > 0]
    sampling = {'censored': bool(reasons), 'reasons': reasons, 'exclusions': exclusions,
        'exclusion_coverage_known': all(count is not None for count in exclusions.values()),
        'complete_framing_is_unbiased_coverage': False}
    return {'schema': schema, 'summary': summary, 'sampling': sampling, 'claim_modes': sorted(modes), 'metrics': metrics, 'scenario_counts': dict(scenario_counts),
            'route_metrics': route_metrics, 'diagnostics': diagnostics,
            'journey_policy_digests': sorted(journey_digests), 'journeys': journey_totals,
            'movement_audit': summary.get('movement_audit'),
            'journey_interpretation': 'Per-game diagnostic totals; partial journeys retain partial costs; acquisition static baseline is not a dynamic optimum; no independent-unit inference.'}


class MeanIntervals:
    """Reuse game-resampling weights across policies/metrics with equal coverage."""
    def __init__(self, draws, seed):
        self.draws, self.seed = draws, seed
        self.weights = OrderedDict()
        self.bytes = 0
        self.max_bytes = 64*1024**2

    def interval(self, samples):
        if len(samples) < 5 or self.draws <= 0:
            return None
        identities = tuple(game_id for game_id, value in samples)
        values = [value for game_id, value in samples]
        seed = int(digest([self.seed, 'game-bootstrap', identities])[:16], 16)
        if len(samples) < 16:
            return bootstrap(values, statistics.fmean, self.draws, seed)
        try:
            import numpy as np
        except ImportError as error:
            raise ValueError('Analysis of 16+ games requires the NumPy pin in requirements-dev.txt; use a contributor development environment') from error
        weights = self.weights.pop(identities, None)
        if weights is None:
            size = self.draws*len(samples)*8
            if size > self.max_bytes:
                raise ValueError('bootstrap matrix exceeds 64 MiB; reduce --draws')
            while self.weights and self.bytes+size > self.max_bytes:
                _, removed = self.weights.popitem(last=False)
                self.bytes -= removed.nbytes
            # A multinomial count row is exactly a sample of N games with
            # replacement. Shared rows preserve paired policy comparisons.
            weights = np.random.default_rng(seed).multinomial(len(samples),
                np.full(len(samples), 1/len(samples)), size=self.draws).astype(np.float64)
            self.bytes += weights.nbytes
        self.weights[identities] = weights
        means = np.sort(weights.dot(np.asarray(values, dtype=np.float64))/len(samples))
        return [float(means[int(0.025*(len(means)-1))]), float(means[int(0.975*(len(means)-1))])]


def summarize(games, *, draws=500, seed=1, routes=False):
    groups = defaultdict(list)
    for game_id, game in games:
        for key, metrics in game['route_metrics' if routes else 'metrics'].items():
            groups[key].append((game_id, metrics))
    output = []
    intervals = MeanIntervals(draws, seed)
    for key, values in sorted(groups.items()):
        row = {'pattern': key[0], 'policy': key[1], 'stratum': key[2], 'value': key[3],
               'games': len(values), 'events': sum(v['events'] for _, v in values),
               'compared': sum(v['compared'] for _, v in values),
               'zero_optimum_positive_cost': sum(v['zero_optimum_positive_cost'] for _, v in values)}
        metrics = (('mean_regret', 'mean_ratio', 'failure_rate', 'unreachable_rate', 'limited_rate',
                    'contradiction_rate', 'mean_expansions', 'mean_continuation_steps') if routes else
                   ('mean_regret', 'mean_access_regret', 'mean_ratio', 'mean_access_ratio', 'fallback_rate', 'unreachable_rate',
                    'access_unreachable_rate', 'oracle_missing_rate', 'mean_probes'))
        for metric in metrics:
            samples = [(game_id, v[metric]) for game_id, v in values if v[metric] is not None]
            row[metric] = {'mean': statistics.fmean(v for _, v in samples) if samples else None,
                'games': len(samples), 'unavailable_games': len(values)-len(samples),
                'ci95': intervals.interval(samples)}
        if routes:
            row['strategy'] = row.pop('pattern'); del row['policy']
        output.append(row)
    return output


def analyze(directory, *, draws=500, seed=1):
    source = Results(directory)
    expected = {j['id']: j for j in source.manifest['jobs'] if j['type'] == 'game'}
    games, coverage, schemas, claim_modes, live_policies = [], [], set(), set(), set()
    for record in source:
        game = record['job']; game_id = game['id']
        if game_id not in expected:
            continue
        del expected[game_id]
        entry = {'game': game_id, 'category': record['category']}
        try:
            if record['category'] != 'success':
                raise ValueError('game execution did not succeed')
            # The artifact itself is gzip, independent of transport encoding.
            with source.open_artifact(record, ARTIFACT, 'rb') as packed:
                with gzip.GzipFile(fileobj=packed, mode='rb') as decompressed:
                    with io.TextIOWrapper(decompressed) as stream:
                        measured = read_game(stream, game.get('labels', {}))
            resolved = (record.get('result') or {}).get('resolved', {})
            from .claim_journeys import attest_digests
            attest_digests(measured['journey_policy_digests'], resolved)
            expected_policy = game.get('config', {}).get('resource_claims_policy')
            experimental = (resolved.get('active_claim_policy') == 'experimental' or expected_policy is not None
                            or any(mode[1] == 'experimental' for mode in measured['claim_modes']))
            if experimental:
                from .claim_policy import attested_policy_digest, policy_digest
                identity = attested_policy_digest(resolved)
                if expected_policy is not None and identity != policy_digest(expected_policy):
                    raise ValueError('experimental claim policy differs from frozen request')
                live_policies.add(identity)
            schemas.add(digest(measured['schema']))
            claim_modes.update(tuple(mode) for mode in measured['claim_modes'])
            if resolved.get('active_claim_policy') in ('disabled', 'prototype-stationary-v1', 'experimental'):
                claim_modes.add((resolved['active_claim_policy'] != 'disabled', resolved['active_claim_policy']))
            games.append((game_id, measured))
            entry.update({'measured': True, 'summary': measured['summary'], 'sampling': measured['sampling'], 'diagnostics': measured['diagnostics'],
                          'journeys': measured['journeys'], 'journey_interpretation': measured['journey_interpretation'],
                          'movement_audit': measured['movement_audit']})
        except (OSError, EOFError, ValueError, StopIteration, KeyError, TypeError) as error:
            entry.update({'measured': False, 'error': str(error)})
        coverage.append(entry)
    coverage.extend({'game': game_id, 'category': 'missing', 'measured': False} for game_id in expected)
    # Differing sample rates/pattern sets/claim modes cannot be pooled silently.
    if len(schemas) > 1 or len(claim_modes) > 1 or len(live_policies) > 1:
        raise ValueError('mixed claim trace schemas; analyze frozen cohorts separately')
    groups = summarize(games, draws=draws, seed=seed)
    route_groups = summarize(games, draws=draws, seed=seed, routes=True)
    return {'schema_version': 1, 'coverage': coverage, 'measured_games': len(games),
            'expected_games': len(coverage), 'interval_unit': 'independent game, mean of per-game means',
            'interval_method': 'deterministic game bootstrap; shared NumPy multinomial weights at 16+ games; unavailable below five games',
            'bootstrap_draws': draws, 'groups': groups, 'tool_package_identity': package_identity(),
            'static_route_groups': route_groups,
            'canonical_xy_coverage': {'published_events': sum(g['diagnostics']['published_xy_events'] for _, g in games),
                'missing_events': sum(g['diagnostics']['missing_xy_events'] for _, g in games),
                'eligible_for_policy_selection': bool(games) and all(g['diagnostics']['events'] > 0 and not g['diagnostics']['missing_xy_events'] and g['summary'].get('excluded_missing_field') == 0 for _, g in games),
                'missing_field_exclusions': [{'game': game_id, 'count': g['summary'].get('excluded_missing_field')} for game_id,g in games],
                'note': 'Scalar-projection traces support throughput calibration only; keep destination mode equal in shadow parity comparisons.'},
            'analysis_environment': {'python': sys.version, 'numpy': getattr(sys.modules.get('numpy'), '__version__', None)},
            'scenario_coverage': coverage_audit(games),
            'sampling_coverage': {'censored_games': sum(g['sampling']['censored'] for _, g in games),
                'uncensored_games': sum(not g['sampling']['censored'] for _, g in games),
                'eligible_for_policy_selection': bool(games) and len(games) == len(coverage) and all(not g['sampling']['censored'] for _, g in games),
                'action': 'Tune sample_every and sample/output budgets from pilot volume and late-tick retention before freezing a full round. Prefix caps and queue drops can correlate with contention; no automatic weighting correction is applied.'},
            'primary_travel_metric': 'mean_access_regret (and mean_access_ratio) for the selected-access route prototype; inspect access-unreachable and fallback rates alongside it',
            'measurement_semantics': {
                'mean_regret': 'Component-best source cost lower bound; not actual selected-access travel for pooled farms.',
                'mean_access_regret': 'Chosen access cost minus globally optimal available source cost; current selected-access route travel proxy.',
                'pool_source_identity': 'Offline evaluator chooses smallest available source index; live harvest picker chooses ripest-first. Availability/access/path labels agree within a component, but physical stock IDs need not.',
                'mean_probes': 'Raw pattern entries examined; independent of cross-pattern cache history.',
                'new_destination_reads': 'Marginal shared-event cache misses, dependent on evaluation order; instrumentation amortization only, never standalone policy runtime.',
                'static_route_elapsed_ns': 'Steady-clock elapsed nanoseconds, not thread CPU; accepts historical static_route_cpu_ns. Event aggregate counted once; per-route elapsed_ns (historical cpu_ns) includes cached attribution and must not be summed or used as isolated policy timing.',
                'policy_timing': 'Requires later isolated native measurements; shared-event CPU cannot rank individual policies.'},
            'limits': 'Shadow labels do not measure downstream economy or live path cost; no automatic winner selection.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    command = sub.add_parser('plan')
    command.add_argument('--bundle', required=True)
    command.add_argument('--games', type=int, choices=(32, 1000), default=32)
    command.add_argument('--seed', type=int, default=1)
    command.add_argument('--generators', nargs='+', type=int)
    command.add_argument('--generator-overrides', type=Path)
    command.add_argument('--output', type=Path, required=True)
    command = sub.add_parser('supplement')
    command.add_argument('--bundle', required=True)
    command.add_argument('--fixtures', type=Path, required=True)
    command.add_argument('--artifacts', type=Path, required=True, help='eventual coordinator RESULTS/artifacts directory')
    command.add_argument('--output', type=Path, required=True)
    command = sub.add_parser('analyze')
    command.add_argument('directory')
    command.add_argument('--draws', type=int, default=500)
    command.add_argument('--seed', type=int, default=1)
    command.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output already exists; retain frozen manifests/reports')
    if args.command == 'plan':
        overrides = json.loads(args.generator_overrides.read_text()) if args.generator_overrides else None
        result = plan(inspect_bundle(args.bundle), games=args.games, seed=args.seed,
                      generators=args.generators, generator_overrides=overrides)
    elif args.command == 'supplement':
        result = supplemental_plan(inspect_bundle(args.bundle), json.loads(args.fixtures.read_text()), args.artifacts)
    else:
        if args.draws < 0:
            parser.error('draws must be nonnegative')
        result = analyze(args.directory, draws=args.draws, seed=args.seed)
    atomic_json(args.output, result)


if __name__ == '__main__':
    main()
