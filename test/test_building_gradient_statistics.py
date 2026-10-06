"""Keep benchmark repetitions and independent gameplay seeds distinct."""
import math
import gzip
import struct
import unittest
import tempfile
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from benchmark_building_pipeline import counterfactual_reference_end, verify_trace_window, verify_optional_checkpoint, resumed_case_event, case_observations, save_case_progress
from report_building_pipeline import mean_interval, paired_statistics, gameplay_statistics, trip_observations, counterfactual_report


class BuildingGradientStatisticsTests(unittest.TestCase):
    def test_normal_reference_covers_all_decisions_and_their_full_horizons(self):
        self.assertEqual(counterfactual_reference_end(dict(case_inventory={
            'movement': [dict(tick='123')], 'hiring': [dict(tick=1090), dict(tick=900)]})), 1602)
        self.assertIsNone(counterfactual_reference_end(dict(case_inventory={})))

    def test_parallel_diagnostic_delays_preserve_each_others_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'counterfactuals.json'
            with ThreadPoolExecutor(max_workers=3) as pool:
                list(pool.map(lambda delay: save_case_progress(path, [dict(delay=delay, case='first')], delay), (2, 4, 8)))
            save_case_progress(path, [dict(delay=4, case='first'), dict(delay=4, case='second')], 4)
            rows = json.loads(path.read_text())
            self.assertEqual(sorted(r['delay'] for r in rows), [2, 4, 4, 8])
            self.assertEqual(sum(r['case'] == 'second' for r in rows), 1)

    def test_diagnostic_rows_do_not_hide_censoring_or_duplicate_interventions(self):
        def case(normal, fresh):
            return dict(delay=4, kind='resource', case=dict(tick='10', event='20'),
                        outcomes={name: dict(normal=normal, fresh=fresh, censored=not (normal and fresh),
                                            additional_ticks=4 if normal and fresh else None,
                                            applicable=name != 'hired')
                                  for name in ('hired', 'harvested', 'market_acquired', 'acquired', 'delivered')},
                        trip_endpoints=dict(normal=normal, fresh=fresh))
        a = dict(kind='delivered', elapsed_ticks='15', distance='7', reversals='2')
        b = dict(kind='delivered', elapsed_ticks='11', distance='8', reversals='1')
        result = counterfactual_report([case(a, b), case(a, None)])['4']
        group = result['categories']['resource']
        self.assertEqual(group['case_rows'], 2)
        self.assertEqual(group['distinct_interventions'], 1)
        self.assertEqual(group['delivered']['censored'], 1)
        self.assertEqual(group['delivered']['completion_pairs']['normal/missing_fresh'], 1)
        self.assertEqual(group['observed_trip_differences']['distance']['sum'], -1)
        self.assertIsNone(result['hiring_screen'])

    def test_small_sample_interval_uses_seed_count_and_student_t(self):
        result = mean_interval([1, 2, 3, 4])
        self.assertEqual(result['count'], 4)
        self.assertEqual(result['mean'], 2.5)
        width = 3.182446 * math.sqrt(5 / 3) / 2
        self.assertAlmostEqual(result['ci95'][0], 2.5 - width)
        self.assertAlmostEqual(result['ci95'][1], 2.5 + width)

    def test_one_observation_has_no_estimated_interval(self):
        self.assertIsNone(mean_interval([5])['ci95'])
        self.assertEqual(mean_interval([])['count'], 0)

    def test_paired_seed_mean_is_distinct_from_volume_weighted_mean(self):
        result = paired_statistics([(100, 90), (1000, 1100)])
        self.assertAlmostEqual(result['relative_change']['mean'], 0)
        self.assertAlmostEqual(result['pooled_relative_change'], 90 / 1100)
        self.assertEqual(result['difference']['count'], 2)

    def test_zero_baseline_is_retained_in_absolute_changes(self):
        result = paired_statistics([(0, 2), (4, 3)])
        self.assertEqual(result['difference']['count'], 2)
        self.assertEqual(result['relative_change']['count'], 1)

    def test_duplicate_seed_cannot_be_counted_as_an_independent_match(self):
        volumes = {name: {str(delay): dict(game_seed=19, accepted_resource_units=100,
                    starvation_deaths=2) for delay in (0, 2, 4, 8)}
                   for name in ('busy', 'small')}
        with self.assertRaisesRegex(ValueError, 'distinct known seeds'):
            gameplay_statistics(volumes, {}, ['busy', 'small'])

    def test_incomplete_audits_do_not_select_only_finished_seeds(self):
        volumes = {name: {str(delay): dict(game_seed=seed, accepted_resource_units=100,
                    starvation_deaths=2, observed_ticks=10000) for delay in (0, 2, 4, 8)}
                   for name, seed in (('a', 19), ('b', 23), ('c', 47), ('d', 83))}
        result = gameplay_statistics(volumes, {}, list(volumes))
        self.assertEqual(result['delays']['4']['audited_seeds_complete'], 0)
        self.assertEqual(result['delays']['4']['metrics']['accepted_resource_units']
                         ['relative_change']['count'], 4)

    def test_checkpoint_trace_window_compares_every_resumed_tick(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def write(name, ticks, changed=None, aggregate=123):
                p = root / name
                p.mkdir()
                with gzip.open(p / 'game.replay.checksums.gz', 'wb') as stream:
                    stream.write(b'GCS1' + struct.pack('<4I', 1, 0, len(ticks), 0))
                    for tick in ticks:
                        stream.write(struct.pack('<5I', tick, aggregate, 999 if tick == changed else 456, 0, 0))
                return p
            reference = write('reference', list(range(100, 107)))
            resumed = write('resumed', list(range(103, 107)), aggregate=789)
            self.assertEqual(verify_trace_window(reference, resumed), 4)
            self.assertEqual(verify_optional_checkpoint(reference, resumed), (4, None))
            changed = write('changed', list(range(103, 107)), 105)
            with self.assertRaisesRegex(RuntimeError, 'changes simulation state'):
                verify_trace_window(reference, changed)
            ticks, rejection = verify_optional_checkpoint(reference, changed)
            self.assertEqual(ticks, 0)
            self.assertEqual(rejection, 'checkpoint continuation changes simulation state')
            (changed / 'game.replay.checksums.gz').write_bytes(b'not a trace')
            with self.assertRaises((OSError, RuntimeError)):
                verify_optional_checkpoint(reference, changed)

    def test_checkpoint_event_mapping_rejects_ambiguous_identical_decisions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            p = root / 'building-gradient-impact-decisions.csv'
            p.write_text('tick,event,kind,unit\n10,4,movement,2\n')
            case = dict(tick='10', event='104', kind='movement', unit='2')
            self.assertEqual(resumed_case_event(root, case), 4)
            p.write_text(p.read_text() + '10,5,movement,2\n')
            self.assertIsNone(resumed_case_event(root, case))

    def test_checkpoint_event_offset_keeps_prior_outcomes_outside_the_case(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,event,kind,building,building_identity,unit,unit_identity\n'
                '10,4,delivered,1,1,2,1\n'
                '11,6,harvested,1,1,2,1\n'
                '12,7,delivered,1,1,2,1\n')
            case = dict(tick=10, event=105, building=1, building_identity=1,
                        unit=2, unit_identity=1)
            result = case_observations(root, 'movement', case, event_offset=100)
            self.assertEqual(result['delivered']['tick'], '12')
            self.assertEqual(case['event'], 105)

    def test_initial_trips_remain_left_censored_for_distance_and_duration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'building-gradient-impact-ticks.csv').write_text('tick\n100\n')
            (root / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,kind,unit,unit_identity,building,building_identity,elapsed_ticks,censored,distance,reversals\n'
                '105,delivered,1,11,2,22,5,0,7,2\n'
                '106,hired,3,33,2,22,0,0,0,0\n'
                '106,delivered,3,33,2,22,0,0,0,0\n'
                '120,delivered,4,44,2,22,10,0,13,1\n')
            result = trip_observations(root)
            self.assertEqual(result['left_censored_duration_lower_bounds']['count'], 1)
            self.assertEqual(result['complete_duration_ticks']['count'], 2)
            self.assertEqual(result['complete_distance_tiles']['sum'], 13)
            self.assertEqual(result['complete_reversals']['sum'], 1)

    def test_unequal_observation_horizons_cannot_be_compared_as_counts(self):
        volumes = {name: {str(delay): dict(game_seed=seed, accepted_resource_units=100,
                    starvation_deaths=2, observed_ticks=ticks) for delay in (0, 2, 4, 8)}
                   for name, seed, ticks in (('a', 19, 10000), ('b', 23, 5000))}
        with self.assertRaisesRegex(ValueError, 'identical known observation horizons'):
            gameplay_statistics(volumes, {}, list(volumes))


if __name__ == '__main__':
    unittest.main()
