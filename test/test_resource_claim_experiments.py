#!/usr/bin/env python3
"""Frozen claim campaign and clustered missing-data analysis contracts."""
import gzip
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.jobs import EngineJob
from tools.tournaments.common import store_artifact
from tools.tournaments.model import validate_job
from tools.tournaments.resource_claims import (ARTIFACT, COLUMNS, analyze, coverage_audit, plan,
    MeanIntervals, read_game, scenario_hits, supplemental_plan, summarize, trace_rows, conditional_pool, select_conditional_pool)


def trace(count=1, *, optimum=10, cost=15, access_cost=15, fallback=False):
    rows = [{'kind': 'schema', 'version': 1, 'patterns': ['spiral:k4'], 'policies': ['first_available'],
             'sample_every': 1000, 'max_samples': 512, 'max_output_bytes': 16*1024**2,
             'outcome_columns': list(COLUMNS)}]
    for ordinal in range(count):
        rows.append({'kind': 'decision', 'ordinal': ordinal, 'tick': 100000, 'phase': 'pickup',
            'material': 1, 'movement': 0, 'width': 256, 'height': 256, 'source_count': 3,
            'optimal': None if optimum is None else {'distance': optimum},
            'patterns': [[0, 0, 42, 42, 3, cost, 8, 8, fallback, access_cost]]})
    rows.append({'kind': 'summary', 'complete': True, 'submitted': count, 'completed': count,
                 'excluded_sample_limit': 0, 'excluded_queue_limit': 0})
    return rows


def stream(rows):
    return io.StringIO(''.join(json.dumps(row)+'\n' for row in rows))


class ResourceClaimExperiments(unittest.TestCase):
    def bundle(self):
        return {'id': 'a'*64, 'capabilities': {
            'resource_claims_shadow_version': 1, 'material_destinations_version': 1,
            'experiments': ['farm-areas', 'markets-v2'],
            'ais': [{'id': i+1, 'name': name} for i, name in enumerate(
                ['numbi', 'castor', 'warrush', 'nicowar', 'econo', 'maxima', 'cortex', 'cabino'])],
            'generators': [{'method': n, 'editorOnly': False} for n in (15, 21)]}}

    def test_conditional_valid_draws_retain_rejections_and_block_missing_preflight(self):
        pool=conditional_pool(self.bundle(),games=4,attempt_multiplier=2)
        self.assertEqual(len(pool['jobs']),8)
        self.assertEqual(pool['jobs'][0]['config']['resource_claims_shadow']['max_samples'],32768)
        self.assertTrue(all(j['limits']['timeout_seconds']==21600 for j in pool['jobs'] if j['labels']['farm_mode']!='default'))
        outcomes={j['labels']['map_input_identity']:{'category':'success'} for j in pool['jobs']}
        first=pool['jobs'][0];key=first['labels']['map_input_identity']
        outcomes[key]={'category':'invalid_request','diagnostic':'too crowded'}
        valid,report=select_conditional_pool(pool,outcomes)
        self.assertTrue(report['ready']);self.assertEqual(len(valid['jobs']),4)
        self.assertEqual(len(report['rejections']),1)
        self.assertNotIn(first['id'],[j['id'] for j in valid['jobs']])
        self.assertEqual(valid['jobs'][0]['config']['params'],pool['jobs'][1]['config']['params'])
        del outcomes[key]
        valid,report=select_conditional_pool(pool,outcomes)
        self.assertIsNone(valid);self.assertIn(key,report['pending_preflights'])
        outcomes[key]={'category':'artifact_failure'}
        self.assertTrue(select_conditional_pool(pool,outcomes)[1]['infrastructure_failures'])
        failed={j['labels']['map_input_identity']:{'category':'invalid_request','diagnostic':'unsupported'} for j in pool['jobs']}
        self.assertEqual(set(select_conditional_pool(pool,failed)[1]['attempt_cap_exhausted']),{'default','protected','pooled'})

    def test_manifest_freezes_equal_cohorts_and_all_cli_limits(self):
        old_bundle = self.bundle()
        del old_bundle['capabilities']['resource_claims_shadow_version']
        with self.assertRaisesRegex(ValueError, 'does not advertise'):
            plan(old_bundle)
        manifest = plan(self.bundle(), seed=19)
        self.assertEqual(len(manifest['jobs']), 32)
        self.assertEqual(manifest, plan(self.bundle(), seed=19))
        broad = [j for j in manifest['jobs'] if j['labels']['claim_cohort'] == 'broad']
        stress = [j for j in manifest['jobs'] if j['labels']['claim_cohort'] == 'farming_stress']
        self.assertEqual((len(broad), len(stress)), (16, 16))
        self.assertTrue(all(j['config']['win_probability_permille'] == 0 for j in stress))
        self.assertTrue(all(j['config']['win_probability_permille'] == 950 for j in broad))
        self.assertFalse({'numbi', 'castor', 'warrush'} & {p for j in stress for p in j['config']['players']})
        self.assertEqual(manifest['design']['cohort_weights'], {'broad': 0.5, 'farming_stress': 0.5})
        self.assertEqual(len(manifest['design']['stress_strata']), 12)
        self.assertEqual(manifest['design']['family_weights'], {'broad': 0.5, 'protected_farming': 0.25, 'pooled_farming': 0.25})
        self.assertEqual(sum(j['labels']['farm_mode'] == 'pooled' for j in stress), 8)
        self.assertEqual(sum(j['labels']['farm_mode'] == 'protected' for j in stress), 8)
        for job in manifest['jobs']:
            validate_job(job)
            command = EngineJob('game').command(job, {'directory': '/bundle', 'executable': 'glob2'}, '/attempt', {})
            self.assertIn('--resource-claims-shadow', command)
            self.assertIn('--material-destinations', command)
            self.assertIs(job['labels']['material_destinations'], True)
            self.assertEqual(command[command.index('--resource-claims-max-samples')+1], '512')
            self.assertGreater(job['limits']['artifact_bytes'], job['config']['resource_claims_shadow']['max_bytes'])
            if job['labels']['farm_mode'] == 'pooled':
                self.assertEqual(job['config']['experiments'], ['farm-areas'])
                self.assertEqual(command[command.index('--experiment')+1], 'farm-areas')
            else:
                self.assertNotIn('--experiment', command)
        large = plan(self.bundle(), games=1000, seed=19)
        self.assertEqual(len(large['jobs']), 1000)
        self.assertEqual(sum(j['labels']['claim_cohort'] == 'farming_stress' for j in large['jobs']), 500)
        self.assertEqual(sum(j['labels']['farm_mode'] == 'pooled' for j in large['jobs']), 250)

    def test_experiment_keys_are_catalog_checked_and_saves_cannot_override_them(self):
        from tools.tournaments.experiments import Planner
        missing = self.bundle(); missing['capabilities']['experiments'] = []
        with self.assertRaisesRegex(ValueError, 'pinned build catalog'):
            plan(missing)
        for keys in ('farm-areas', ['unknown']):
            with self.assertRaises(ValueError):
                Planner('ai_comparison', {'id': 'bad', 'sample_games': 1, 'experiments': keys}, [self.bundle()]).plan()
        request = plan(self.bundle())['jobs'][0]
        for keys in ('farm-areas', ['farm-areas', 'farm-areas'], [''], [1]):
            with self.assertRaisesRegex(ValueError, 'experiments'):
                validate_job(request | {'config': request['config'] | {'experiments': keys}})
        saved = request | {'inputs': {'save': {'job': 'frozen', 'artifact': 'save.game.gz'}}, 'seeds': {},
                           'depends_on': ['frozen'], 'config': {'ticks': 1000, 'experiments': []}}
        with self.assertRaisesRegex(ValueError, 'saved game'):
            validate_job(saved)

    def test_deep_context_forwarding_requires_observer_and_manifest(self):
        item = plan(self.bundle())['jobs'][0]
        context = {'source_revision':'source','bundle_id':item['build'],'cohort_id':'cohort','game_id':'game'}
        item['config']['resource_claims_deep_context'] = context
        with self.assertRaisesRegex(ValueError, 'manifest'):
            EngineJob('game').validate(item)
        item['outputs']['required'].append('resource-claims-deep/manifest.json')
        EngineJob('game').validate(item)
        command = EngineJob('game').command(item, {'directory':'/bundle','executable':'glob2','source_revision':'source'}, '/attempt', {})
        self.assertEqual(json.loads(command[command.index('--resource-claims-deep-context')+1]),context)
        item['config']['resource_claims_deep_context'] = context | {'game_id':''}
        with self.assertRaisesRegex(ValueError, 'bounded'):
            EngineJob('game').validate(item)

    def test_deep_collector_requires_complete_uncensored_context(self):
        from tools.resource_claim_deep import DESIGN
        item=plan(self.bundle())['jobs'][0]
        context={'source_revision':'source','bundle_id':item['build'],'cohort_id':'cohort','game_id':'game'}
        item['config']['resource_claims_deep_context']=context
        item['outputs']['required'].append('resource-claims-deep/manifest.json')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'output';deep=output/'resource-claims-deep';deep.mkdir(parents=True)
            result={'schema_version':1,'status':'completed','resolved':{'material_destinations_enabled':True},
                    'resource_claims_shadow':{'schema_version':1,'enforced':False,'submitted':1,'completed':1}}
            (output/'result.json').write_text(json.dumps(result))
            rows=trace();rows[0].update(sample_every=item['config']['resource_claims_shadow']['sample_every'],
                max_samples=item['config']['resource_claims_shadow']['max_samples'],
                max_output_bytes=item['config']['resource_claims_shadow']['max_bytes'])
            with gzip.open(output/ARTIFACT,'wt') as f:f.write(stream(rows).getvalue())
            manifest={'version':1,'complete':True,'censored':False,'design':DESIGN,'context':context,'eligible':0,
                'ordinal':0,'captured_ordinal':0,'replacements':0,'file_sha256':'','excluded_eligible_admission':0,
                'excluded_eligible_sample_cap':0,'excluded_eligible_queue_cap':0}
            for change in ({'complete':False},{'censored':True},{'context':{}},{'eligible':-1},{'captured_ordinal':1},
                           {'excluded_eligible_sample_cap':1,'excluded_eligible_admission':1}):
                (deep/'manifest.json').write_text(json.dumps(manifest|change))
                with self.assertRaises(ValueError):EngineJob('game').collect(item,directory)
            (deep/'manifest.json').write_text(json.dumps(manifest))
            self.assertEqual(EngineJob('game').collect(item,directory),result)
            (deep/'event.bin').write_bytes(b'unexpected')
            with self.assertRaisesRegex(ValueError,'zero-eligible'):EngineJob('game').collect(item,directory)

    def test_active_probe_config_and_per_event_mode_attestation(self):
        item = plan(self.bundle())['jobs'][0]
        item['config']['resource_claims_probe'] = item['config'].pop('resource_claims_shadow')
        with self.assertRaisesRegex(ValueError, 'requires active'):
            EngineJob('game').validate(item)
        item['config']['resource_claims_active'] = 'prototype-stationary-v1'
        EngineJob('game').validate(item)
        command = EngineJob('game').command(item, {'directory':'/bundle','executable':'glob2','source_revision':'source'}, '/attempt', {})
        self.assertIn('--resource-claims-probe', command)
        self.assertIn('--resource-claims-active', command)
        self.assertNotIn('--resource-claims-shadow', command)
        rows = trace(); rows[0]['claims_enforced'] = None
        with self.assertRaisesRegex(ValueError, 'per-event'):
            read_game(stream(rows), {})
        rows[1].update(claims_enforced=True, active_policy='prototype-stationary-v1', decision_kind='new_claim_acquisition')
        active = read_game(stream(rows), {})
        self.assertEqual(active['claim_modes'], [(True, 'prototype-stationary-v1')])
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'output'; output.mkdir()
            result={'schema_version':1, 'status':'completed',
                'resolved': {'material_destinations_enabled':True,'active_claim_policy':'prototype-stationary-v1'},
                'resource_claims_shadow': {'schema_version':1,'enforced':True,'submitted':1,'completed':1}}
            (output/'result.json').write_text(json.dumps(result))
            with gzip.open(output/ARTIFACT,'wt') as f:f.write(stream(rows).getvalue())
            self.assertEqual(EngineJob('game').collect(item,directory),result)
            rows[1]['claims_enforced']=False
            rows[1]['active_policy']='disabled'
            with gzip.open(output/ARTIFACT,'wt') as f:f.write(stream(rows).getvalue())
            with self.assertRaisesRegex(ValueError, 'enforcement'):
                EngineJob('game').collect(item,directory)

    def test_material_destinations_are_frozen_boolean_new_game_only(self):
        bundle = self.bundle()
        del bundle['capabilities']['material_destinations_version']
        with self.assertRaisesRegex(ValueError, 'material destinations'):
            plan(bundle)
        item = plan(self.bundle())['jobs'][0]
        item['config']['material_destinations'] = 1
        with self.assertRaisesRegex(ValueError, 'boolean'):
            EngineJob('game').validate(item)
        item['config']['material_destinations'] = True
        item['outputs']['replay'] = True
        with self.assertRaisesRegex(ValueError, 'replay'):
            EngineJob('game').validate(item)
        item['outputs']['replay'] = False
        item['inputs'] = {'save': {'sha256': 'a'*64}}
        item['seeds'] = {}
        item['config'] = {'material_destinations': False}
        with self.assertRaisesRegex(ValueError, 'saved game'):
            EngineJob('game').validate(item)
        rows = trace()
        old = read_game(stream(rows), {})
        self.assertEqual(old['diagnostics']['missing_xy_events'], 1)
        rows[1].update(destination_lookup='published-xy', destination_xy_missing=False)
        new = read_game(stream(rows), {})
        self.assertEqual(new['diagnostics']['published_xy_events'], 1)
        self.assertEqual(new['diagnostics']['missing_xy_events'], 0)

    def test_game_means_not_decision_counts_are_interval_units(self):
        first = read_game(stream(trace(10, cost=10, access_cost=10)), {'claim_cohort': 'broad'})
        second = read_game(stream(trace(1, cost=110, access_cost=120)), {'claim_cohort': 'broad'})
        rows = summarize([('a', first), ('b', second)], draws=10)
        overall = next(r for r in rows if r['stratum'] == 'all')
        self.assertEqual(overall['events'], 11)
        self.assertEqual(overall['mean_regret']['mean'], 50)
        self.assertEqual(overall['mean_access_regret']['mean'], 55)
        self.assertEqual(overall['mean_access_ratio']['mean'], 6.5)
        self.assertIsNone(overall['mean_regret']['ci95'])
        many = [(str(i), first if i < 3 else second) for i in range(6)]
        self.assertEqual(summarize(many, draws=20, seed=7), summarize(many, draws=20, seed=7))
        self.assertIsNotNone(summarize(many, draws=20)[0]['mean_regret']['ci95'])

    @unittest.skipUnless(importlib.util.find_spec('numpy'), 'NumPy is a contributor development dependency')
    def test_static_routes_use_strategy_optimum_and_count_event_cpu_once(self):
        rows = trace()
        rows[1].update(oracle_ns=123, static_route_cpu_ns=456, static_routes=[{
            'source_optimal_cost': 10, 'access_optimal_cost': 30,
            'direct_source_component': {'status': 'reached', 'cost': 10, 'expansions': 5, 'cpu_ns': 999},
            'via_probe_selected_access': {'status': 'reached', 'cost': 40, 'expansions': 8, 'cpu_ns': 999},
        }])
        game = read_game(stream(rows), {})
        self.assertEqual(game['diagnostics']['static_route_elapsed_ns'], 456)
        rows[1]['static_route_elapsed_ns']=rows[1].pop('static_route_cpu_ns')
        rows[1]['oracle_elapsed_ns']=rows[1].pop('oracle_ns')
        self.assertEqual(read_game(stream(rows), {})['diagnostics']['static_route_elapsed_ns'],456)
        summary = {x['strategy']: x for x in summarize([('game', game)], routes=True) if x['stratum'] == 'all'}
        self.assertEqual(summary['direct_source_component']['mean_regret']['mean'], 0)
        self.assertEqual(summary['via_probe_selected_access']['mean_regret']['mean'], 10)
        rows[1]['static_routes'][0]['via_probe_selected_access'].update(status='continuation_limit', cost=None)
        failed = read_game(stream(rows), {})['route_metrics'][('via_probe_selected_access', 'static', 'all', 'all')]
        self.assertIsNone(failed['mean_regret'])
        self.assertEqual(failed['limited_rate'], 1)

    def test_vectorized_game_bootstrap_reuses_paired_weights_and_is_bounded(self):
        intervals = MeanIntervals(100, 7)
        samples = [(str(i), float(i)) for i in range(32)]
        lower, upper = intervals.interval(samples)
        shifted = intervals.interval([(identity, value+100) for identity, value in samples])
        self.assertEqual(shifted, [lower+100, upper+100])
        self.assertEqual(len(intervals.weights), 1)
        self.assertEqual(intervals.interval([(identity, 7.0) for identity, value in samples]), [7, 7])
        limited = MeanIntervals(100, 7); limited.max_bytes = 100
        with self.assertRaisesRegex(ValueError, '64 MiB'):
            limited.interval(samples)

    def test_missing_optimum_and_zero_denominator_are_never_zero_regret(self):
        missing = read_game(stream(trace(optimum=None, cost=None, access_cost=None, fallback=True)), {})
        row = next(iter(missing['metrics'].values()))
        self.assertIsNone(row['mean_regret']); self.assertIsNone(row['mean_ratio'])
        self.assertEqual(row['oracle_missing_rate'], 1)
        zero = read_game(stream(trace(optimum=0, cost=10, access_cost=20)), {})
        row = next(iter(zero['metrics'].values()))
        self.assertEqual(row['mean_regret'], 10); self.assertIsNone(row['mean_ratio'])
        self.assertEqual(row['zero_optimum_positive_cost'], 1)
        no_events = read_game(stream(trace(0)), {})
        self.assertEqual(no_events['metrics'], {})

    def test_truncation_duplicates_and_impossible_costs_fail(self):
        bad = trace()[:-1]
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            read_game(stream(bad), {})
        bad = trace(); bad.insert(2, bad[1])
        with self.assertRaisesRegex(ValueError, 'unordered'):
            list(trace_rows(stream(bad)))
        bad = trace(); bad[1]['patterns'] = []
        with self.assertRaisesRegex(ValueError, 'missing pattern'):
            list(trace_rows(stream(bad)))
        with self.assertRaisesRegex(ValueError, 'below exact'):
            read_game(stream(trace(cost=5)), {})
        with self.assertRaisesRegex(ValueError, 'source lower bound'):
            read_game(stream(trace(cost=15, access_cost=10)), {})

    def test_collector_requires_complete_attested_matching_artifact(self):
        job = plan(self.bundle())['jobs'][0]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'output'; output.mkdir()
            result = {'schema_version': 1, 'status': 'completed', 'resolved': {'material_destinations_enabled': True}, 'resource_claims_shadow': {
                'schema_version': 1, 'enforced': False, 'submitted': 1, 'completed': 1}}
            (output/'result.json').write_text(json.dumps(result))
            with gzip.open(output/ARTIFACT, 'wt') as out:
                out.write(stream(trace()).getvalue())
            self.assertEqual(EngineJob('game').collect(job, directory), result)
            result['resolved']['material_destinations_enabled'] = False
            (output/'result.json').write_text(json.dumps(result))
            with self.assertRaisesRegex(ValueError, 'mode differs'):
                EngineJob('game').collect(job, directory)
            result['resolved']['material_destinations_enabled'] = True
            (output/'result.json').write_text(json.dumps(result))
            with gzip.open(output/ARTIFACT, 'wt') as out:
                out.write(stream(trace()[:-1]).getvalue())
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                EngineJob('game').collect(job, directory)

    def test_complete_capped_or_queue_excluded_traces_are_censored(self):
        rows = trace()
        for excluded in ('excluded_sample_limit', 'excluded_queue_limit'):
            rows[-1][excluded] = 3
            measured = read_game(stream(rows), {})
            self.assertTrue(measured['sampling']['censored'])
            self.assertIn(excluded, measured['sampling']['reasons'])
            self.assertEqual(measured['diagnostics']['last_sample_tick'], 100000)
            self.assertEqual(measured['diagnostics']['late_sample_events'], 1)
            audit = coverage_audit([('game', measured)], min_games=1, min_events=1)
            sparse = next(r for r in audit['strata'] if r['scenario'] == 'sparse_material')
            self.assertEqual(sparse['games'], 1)
            self.assertEqual(sparse['uncensored_games'], 0)
            self.assertFalse(sparse['covered'])
            self.assertFalse(audit['ready_for_policy_selection'])
            rows[-1][excluded] = 0
        del rows[-1]['excluded_queue_limit']
        missing = read_game(stream(rows), {})
        self.assertTrue(missing['sampling']['censored'])
        self.assertFalse(missing['sampling']['exclusion_coverage_known'])

    def test_builtin_fruit_is_finite_renewable_and_stone_is_infinite(self):
        catalog={r['key']:r for r in json.loads((Path(__file__).resolve().parents[1]/'data/resources/registry.json').read_text())['resources']}
        for material,key in ((5,'cherry-tree'),(6,'orange-tree'),(7,'prune-tree')):
            resource=catalog[key];yield_spec=next(iter(resource['yields'].values()))
            self.assertEqual(yield_spec['consumption'],'one')
            row=trace()[1]|{'material':material,'features':{'infinite':False,
                'resource_persists_when_empty':resource['properties']['persistsWhenEmpty'],
                'resource_growth_rate':resource['properties']['growthRate']}}
            self.assertTrue({'ordinary_fruit','finite_persistent_yield'}<=scenario_hits(row))
        self.assertEqual(catalog['rocks']['yields']['stone']['consumption'],'infinite')
        stone=trace()[1]|{'material':3,'features':{'infinite':True,'resource_persists_when_empty':True}}
        self.assertNotIn('finite_persistent_yield',scenario_hits(stone))
        self.assertTrue(catalog['trees']['yields']['wood']['destroysDeposit'])
        self.assertGreater(catalog['trees']['properties']['spreadRate'],0)
        stale=trace()[1]|{'reason':'stale_goal','features':{'resource_material_mask':None,'resource_growth_rate':None,'infinite':None,'resource_persists_when_empty':None}}
        self.assertNotIn('compound_yield',scenario_hits(stale))

    def test_actual_sample_coverage_demands_missing_supplementals(self):
        rows = trace()
        rows[1]['features'] = {'team_units': 150, 'pooled_farm': True, 'exhausted_pool': True,
                              'resource_material_mask': 3, 'destroys_deposit': True}
        measured = read_game(stream(rows), {'scenario_intent': ['water_algae']})
        audit = coverage_audit([('game', measured)], min_games=1, min_events=1)
        self.assertFalse(audit['ready_for_policy_selection'])
        missing = {r['scenario'] for r in audit['supplemental_required']}
        self.assertIn('water_algae', missing) # A fixture label alone is no evidence.
        self.assertNotIn('compound_yield', missing)
        self.assertNotIn('late_stock_contention', missing)
        self.assertNotIn('exhausted_pool', missing)
        self.assertIn('water_algae', scenario_hits(rows[1] | {'material': 4}))

    def test_supplemental_save_inputs_are_frozen_and_never_duplicate_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            save = root/'fixture.game.gz'; save.write_bytes(b'fixture bytes for the transport contract')
            fixture = {'path': str(save), 'strata': ['water_algae'], 'start_tick': 150000, 'window_ticks': 30000}
            manifest = supplemental_plan(self.bundle(), [fixture], root/'artifacts')
            request = manifest['jobs'][0]
            self.assertEqual(request['config']['ticks'], 180000)
            self.assertEqual(request['seeds'], {})
            self.assertIn('save', request['inputs'])
            self.assertNotIn('players', request['config'])
            self.assertTrue((root/'artifacts'/request['inputs']['save']['sha256']).exists())
            with self.assertRaisesRegex(ValueError, 'not an independent'):
                supplemental_plan(self.bundle(), [fixture, fixture], root/'artifacts')

    def test_results_transport_and_missing_games_keep_coverage_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = plan(self.bundle())
            manifest['jobs'] = manifest['jobs'][:2]
            (root/'experiment.json').write_text(json.dumps(manifest))
            (root/'results').mkdir()
            payload = root/'trace.gz'
            with gzip.open(payload, 'wt') as out:
                out.write(stream(trace()).getvalue())
            artifact = store_artifact(payload, root/'artifacts') | {'path': ARTIFACT}
            request = manifest['jobs'][0]
            (root/'results'/(request['id']+'.json')).write_text(json.dumps({
                'job': request, 'category': 'success', 'artifacts': [artifact]}))
            report = analyze(root, draws=0)
            self.assertEqual((report['expected_games'], report['measured_games']), (2, 1))
            self.assertEqual(report['coverage'][1]['category'], 'missing')
            overall = next(r for r in report['groups'] if r['stratum'] == 'all')
            self.assertEqual(overall['mean_regret']['mean'], 5)
            self.assertIn('evaluation order', report['measurement_semantics']['new_destination_reads'])
            self.assertEqual(overall['games'], 1)
            self.assertFalse(report['scenario_coverage']['ready_for_policy_selection'])


if __name__ == '__main__':
    unittest.main()
