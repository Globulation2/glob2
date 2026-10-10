import unittest
from gpu_offload_analysis import paired_interval, summarize, cpu_ceiling, aggregate_cpu


class OffloadAnalysisTest(unittest.TestCase):
    def row(self, variant, n, cpu=100, wait=10):
        return dict(scenario='map-early', variant=variant, round=n, valid=True,
                    result=dict(initialChecksum=123, finalChecksum=456, ticks=10, benchmark_measured_ticks=10, benchmark_run_cpu_ns=cpu,
                                benchmark_run_wall_ns=100, tick_p99_ns=20,
                                benchmark_publication_wait_ns=wait))

    def test_cpu_saved_work_is_primary(self):
        rows = [r for n in range(10) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        result = summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']
        self.assertTrue(result['scenario_gates_pass'])  # unchanged wall time is acceptable
        self.assertFalse(result['qualified']);self.assertFalse(result['qualifying_evidence'])

    def test_missing_or_invalid_samples_cannot_pass(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        self.assertFalse(summarize(rows[:-1], 'cpu', ['gpu'])['map-early']['gpu']['scenario_gates_pass'])
        rows[-1]['valid'] = False
        self.assertFalse(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['scenario_gates_pass'])

    def test_tail_regression_blocks_cpu_win(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        for r in rows:
            if r['variant'] == 'gpu': r['result']['tick_p99_ns'] = 30
        self.assertFalse(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['scenario_gates_pass'])

    def test_outlier_is_retained(self):
        self.assertGreater(paired_interval([.6] * 9 + [10])['upper_one_sided95'], .7)

    def test_contamination_preserves_statistics_but_prevents_acceptance(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        rows[0]['resource_contaminated'] = True
        result = summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']
        self.assertAlmostEqual(result['metrics']['cpu_per_tick']['ratio'], .6)
        self.assertFalse(result['scenario_gates_pass'])
        self.assertTrue(result['resource_contaminated'])

    def test_diagnostic_samples_preserve_statistics_but_never_qualify(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        rows[0]['result']['benchmark_diagnostics_enabled'] = True
        result = summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']
        self.assertAlmostEqual(result['metrics']['cpu_per_tick']['ratio'], .6)
        self.assertFalse(result['scenario_gates_pass'])
        self.assertTrue(result['diagnostic_only'])

    def test_diagnostic_stage_rows_never_qualify_in_standalone_analysis(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        for row in rows: row['diagnostic_stage'] = True
        result = summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']
        self.assertFalse(result['scenario_gates_pass']);self.assertTrue(result['diagnostic_only'])
        roster, rows = self.aggregate_fixture()
        rows[0]['diagnostic_stage'] = True
        self.assertFalse(self.aggregate(roster, rows)['available'])

    def test_duplicate_pair_is_rejected(self):
        with self.assertRaises(ValueError): summarize([self.row('cpu', 0)] * 2, 'cpu', ['gpu'])

    def test_zero_wait_improvement_and_new_wait(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n, wait=0), self.row('gpu', n, 60, wait=0))]
        self.assertTrue(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['scenario_gates_pass'])
        rows[-1]['result']['benchmark_publication_wait_ns'] = 10
        self.assertFalse(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['scenario_gates_pass'])

    def test_wall_time_is_not_an_offload_ceiling(self):
        result = self.row('cpu', 0)['result']
        result['benchmark_gradient_at_start'] = dict(sample_execution_ns=0)
        result['benchmark_gradient_at_end'] = dict(sample_execution_ns=50)
        self.assertFalse(cpu_ceiling(result)['available'])
        result['benchmark_gradient_at_start'] = dict(required_seed_cpu_ns=10, required_propagation_cpu_ns=20, thread_cpu_clock_available=1, cpu_diagnostics_enabled=1)
        result['benchmark_gradient_at_end'] = dict(required_seed_cpu_ns=20, required_propagation_cpu_ns=30, thread_cpu_clock_available=1, cpu_diagnostics_enabled=1)
        ceiling = cpu_ceiling(result)
        self.assertEqual(ceiling['observed_cpu_removal_fraction'], .2)
        self.assertFalse(ceiling['observed_fraction_at_least_30_percent'])
        self.assertFalse(ceiling['exact_window_ceiling_established'])
        self.assertFalse(ceiling['qualifying_evidence'])

    def test_confirmation_rejects_positive_point_latency_regression(self):
        rows = [r for n in range(10) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        for r in rows:
            if r['variant'] == 'gpu': r['result']['benchmark_run_wall_ns'] = 101
        self.assertFalse(summarize(rows, 'cpu', ['gpu'], minimum_pairs=10, confirmation=True)['map-early']['gpu']['scenario_gates_pass'])

    def test_aggregate_counts_maps_not_phases_or_repetitions(self):
        rows = []
        for m in ('map1', 'map2'):
            for phase in ('early', 'middle', 'late'):
                for n in range(5):
                    for v, cpu in (('cpu', 100), ('gpu', 60)):
                        r = self.row(v, n, cpu)
                        r.update(scenario=m+'-'+phase, map_id=m, group='open-512', phase=phase)
                        rows.append(r)
        roster = [dict(id=m+'-'+phase, map_id=m, group='open-512', phase=phase)
                  for m in ('map1', 'map2') for phase in ('early', 'middle', 'late')]
        summary = aggregate_cpu(rows, 'cpu', ['gpu'], expected_scenarios=roster, expected_rounds=range(5))['gpu']
        self.assertEqual(summary['independent_maps'], 2)
        self.assertAlmostEqual(summary['ratio'], .6)
        self.assertAlmostEqual(summary['arithmetic_mean_paired_ratio'], .6)
        self.assertFalse(summary['qualifying_evidence'])
        self.assertTrue(summary['cpu_target_pass'])

    def aggregate_fixture(self):
        roster = [dict(id=f'{group}-{m}-{phase}', map_id=f'{group}-{m}', group=group, phase=phase)
                  for group in ('open', 'corridors') for m in (1, 2)
                  for phase in ('early', 'middle', 'late')]
        rows = []
        for scenario in roster:
            for n in range(5):
                for variant, cpu in (('cpu', 100), ('gpu', 60)):
                    row = self.row(variant, n, cpu)
                    row.update(scenario=scenario['id'], **{k: scenario[k] for k in ('map_id', 'group', 'phase')})
                    rows.append(row)
        return roster, rows

    def aggregate(self, roster, rows, rounds=range(5)):
        return aggregate_cpu(rows, 'cpu', ['gpu'], expected_scenarios=roster,
                             expected_rounds=rounds, draws=100)['gpu']

    def test_geometric_and_arithmetic_summaries_are_distinct_and_labelled(self):
        import math
        paired = paired_interval([.5, 1.], draws=100)
        self.assertAlmostEqual(paired['ratio'], math.sqrt(.5))
        self.assertAlmostEqual(paired['arithmetic_mean_paired_ratio'], .75)
        roster, rows = self.aggregate_fixture()
        for row in rows:
            if row['variant'] == 'gpu':
                row['result']['benchmark_run_cpu_ns'] = {'early': 30, 'middle': 60, 'late': 90}[row['phase']]
        summary = self.aggregate(roster, rows)
        self.assertAlmostEqual(summary['ratio'], (.3 * .6 * .9) ** (1 / 3))
        self.assertAlmostEqual(summary['arithmetic_mean_paired_ratio'], .6)
        self.assertIn('geometric', summary['ci_estimand'])
        self.assertFalse(summary['qualifying_evidence'])

    def test_aggregate_requires_independently_declared_roster(self):
        _, rows = self.aggregate_fixture()
        self.assertFalse(aggregate_cpu(rows, 'cpu', ['gpu'])['gpu']['available'])

    def test_aggregate_duplicate_and_missing_pairs_cannot_pass(self):
        roster, rows = self.aggregate_fixture()
        for broken in (rows + [rows[0]], rows[:-1]):
            self.assertFalse(self.aggregate(roster, broken)['available'])

    def test_aggregate_missing_entire_stratum_or_phase_cannot_pass(self):
        roster, rows = self.aggregate_fixture()
        for broken in ([r for r in rows if r['group'] != 'corridors'],
                       [r for r in rows if r['phase'] != 'late']):
            self.assertFalse(self.aggregate(roster, broken)['available'])

    def test_aggregate_pair_identity_must_match_protocol(self):
        import copy
        roster, rows = self.aggregate_fixture()
        for key, value in (('map_id', 'different-map'), ('group', 'different-stratum'),
                           ('phase', 'late'), ('control', True)):
            broken = copy.deepcopy(rows); broken[1][key] = value
            self.assertFalse(self.aggregate(roster, broken)['available'], key)

    def test_aggregate_rejects_extra_round_and_insufficient_protocol(self):
        roster, rows = self.aggregate_fixture()
        extra = dict(rows[0], round=5)
        self.assertFalse(self.aggregate(roster, rows + [extra])['available'])
        self.assertFalse(self.aggregate(roster, [r for r in rows if r['round'] == 0], range(1))['available'])

    def test_aggregate_controls_are_required_but_not_primary_weight(self):
        roster, rows = self.aggregate_fixture()
        control = dict(id='small', map_id='small', group='small-control', phase='early', control=True)
        roster.append(control)
        for n in range(5):
            for variant in ('cpu', 'gpu'):
                row = self.row(variant, n, 100);row.update(scenario='small', **{k:control[k] for k in ('map_id','group','phase','control')});rows.append(row)
        result = self.aggregate(roster, rows)
        self.assertTrue(result['available']);self.assertAlmostEqual(result['ratio'], .6)
        self.assertEqual(result['independent_maps'], 4)
        self.assertFalse(self.aggregate(roster, rows[:-1])['available'])

    def test_aggregate_fixed_tick_signature_and_cpu_validity(self):
        import copy
        roster, rows = self.aggregate_fixture()
        for key, value in (('finalChecksum', 999), ('benchmark_measured_ticks', 9), ('benchmark_run_cpu_ns', -1)):
            broken = copy.deepcopy(rows);broken[1]['result'][key] = value
            self.assertFalse(self.aggregate(roster, broken)['available'], key)
        broken = copy.deepcopy(rows);broken[0]['result']['benchmark_run_cpu_ns']=-100;broken[1]['result']['benchmark_run_cpu_ns']=-60
        self.assertFalse(self.aggregate(roster, broken)['available'])

    def test_aggregate_exact_phase_coverage_is_predeclared(self):
        roster, rows = self.aggregate_fixture()
        self.assertFalse(self.aggregate(roster + [dict(roster[0])], rows)['available'])
        self.assertFalse(self.aggregate([s for s in roster if s['phase'] != 'late'], [r for r in rows if r['phase'] != 'late'])['available'])
        result = self.aggregate(roster, rows)
        self.assertEqual(result['rounds_per_phase'], 5)
        self.assertIn('phases', result['weighting'])


if __name__ == '__main__': unittest.main()
