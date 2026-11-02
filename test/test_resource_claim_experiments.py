#!/usr/bin/env python3
"""Frozen claim campaign and clustered missing-data analysis contracts."""
import gzip
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
    read_game, scenario_hits, supplemental_plan, summarize, trace_rows)


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
            'resource_claims_shadow_version': 1,
            'ais': [{'id': i+1, 'name': name} for i, name in enumerate(
                ['numbi', 'castor', 'warrush', 'nicowar', 'econo', 'maxima', 'cortex', 'cabino'])],
            'generators': [{'method': n, 'editorOnly': False} for n in (15, 21)]}}

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
        self.assertEqual(len(manifest['design']['stress_strata']), 6)
        for job in manifest['jobs']:
            validate_job(job)
            command = EngineJob('game').command(job, {'directory': '/bundle', 'executable': 'glob2'}, '/attempt', {})
            self.assertIn('--resource-claims-shadow', command)
            self.assertEqual(command[command.index('--resource-claims-max-samples')+1], '512')
            self.assertGreater(job['limits']['artifact_bytes'], job['config']['resource_claims_shadow']['max_bytes'])
        large = plan(self.bundle(), games=1000, seed=19)
        self.assertEqual(len(large['jobs']), 1000)
        self.assertEqual(sum(j['labels']['claim_cohort'] == 'farming_stress' for j in large['jobs']), 500)

    def test_game_means_not_decision_counts_are_interval_units(self):
        first = read_game(stream(trace(10, cost=10, access_cost=10)), {'claim_cohort': 'broad'})
        second = read_game(stream(trace(1, cost=110, access_cost=120)), {'claim_cohort': 'broad'})
        rows = summarize([('a', first), ('b', second)], draws=10)
        overall = next(r for r in rows if r['stratum'] == 'all')
        self.assertEqual(overall['events'], 11)
        self.assertEqual(overall['mean_regret']['mean'], 50)
        self.assertEqual(overall['mean_access_regret']['mean'], 55)
        self.assertIsNone(overall['mean_regret']['ci95'])
        many = [(str(i), first if i < 3 else second) for i in range(6)]
        self.assertEqual(summarize(many, draws=20, seed=7), summarize(many, draws=20, seed=7))
        self.assertIsNotNone(summarize(many, draws=20)[0]['mean_regret']['ci95'])

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
            result = {'schema_version': 1, 'status': 'completed', 'resource_claims_shadow': {
                'schema_version': 1, 'enforced': False, 'submitted': 1, 'completed': 1}}
            (output/'result.json').write_text(json.dumps(result))
            with gzip.open(output/ARTIFACT, 'wt') as out:
                out.write(stream(trace()).getvalue())
            self.assertEqual(EngineJob('game').collect(job, directory), result)
            with gzip.open(output/ARTIFACT, 'wt') as out:
                out.write(stream(trace()[:-1]).getvalue())
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                EngineJob('game').collect(job, directory)

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
            self.assertEqual(overall['games'], 1)
            self.assertFalse(report['scenario_coverage']['ready_for_policy_selection'])


if __name__ == '__main__':
    unittest.main()
