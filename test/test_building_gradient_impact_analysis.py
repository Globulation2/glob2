"""Guard gameplay evidence interpretation, denominators and censoring."""
import csv
import tempfile
import unittest
from pathlib import Path
from analyze_building_gradient_impact import analyze, distribution, market_changes
from report_building_pipeline import trip_observations, resource_volumes, corrected_market_counts
import json
from benchmark_building_pipeline import case_observations


class BuildingGradientImpactAnalysisTest(unittest.TestCase):
    def test_equal_cost_changes_market_choices_and_censored_episodes(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with (directory / 'building-gradient-impact-decisions.csv').open('w') as stream:
                columns = 'tick event kind building unit live_choice fresh_choice live_resource fresh_resource live_score fresh_score changed harm_cost live_age pending live_reason fresh_reason'.split()
                writer = csv.DictWriter(stream, fieldnames=columns)
                writer.writeheader()
                for n in range(25):
                    row = dict(zip(columns, [n, n, 'movement', 1, 2, 0, 1, -1, -1, 3, 3, 1, 0, 140, 1, -1, -1]))
                    writer.writerow(row)
                writer.writerow(dict(zip(columns, [25, 25, 'movement', 1, 2, -1, 1, -1, -1, -1, -1, 1, 0, 140, 1, -1, -1])))
                writer.writerow(dict(zip(columns, [25, 25, 'movement', 1, 2, 8, 1, -1, -1, 3, 3, 1, 0, 140, 1, -1, -1])))
                writer.writerow(dict(zip(columns, [26, 26, 'resource', 1, 2, -1, 4, 0, 0, 30, 20, 1, 0, 140, 1, -1, -1])))
                writer.writerow(dict(zip(columns, [27, 27, 'hiring', 1, -1, -1, 2, -1, 0, 100, 1, 1, 0, 140, 1, -1, -1])))
            (directory / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,kind,building,unit,resource,elapsed_ticks,distance,reversals,censored,x,y\n'
                '28,missed_hire,1,2,0,8,0,0,0,2,2\n'
                '29,missed_hire,1,3,0,100,0,0,1,2,2\n')
            (directory / 'building-gradient-impact-ticks.csv').write_text(
                'tick,tick_ns,buildings,units,unfilled_slots,deliveries_total,construction_completions,hungry_units,deaths_total\n'
                '1,10,2,3,1,0,0,0,4\n2,12,2,3,0,1,1,1,5\n')
            result = analyze(directory)
            movement = result['decisions']['movement']
            self.assertEqual(movement['decisions'], 27)
            self.assertEqual(movement['changed_without_extra_cost'], 25)
            self.assertEqual(movement['worse_step'], 0)
            self.assertEqual(movement['equal_cost_alternatives'], 25)
            self.assertEqual(movement['stale_goal_stop'], 1)
            self.assertEqual(movement['live_failed_fresh_succeeded'], 1)
            self.assertEqual(movement['cost_comparable_decisions'], 26)
            self.assertEqual(movement['cost_unavailable_decisions'], 1)
            self.assertEqual(movement['additional_step_cost']['count'], 26)
            self.assertEqual(len(result['case_inventory']['movement']), 20)
            self.assertEqual(result['decisions']['resource']['live_failed_fresh_succeeded'], 0)
            self.assertEqual(result['decisions']['resource']['market_vs_harvesting_changed'], 1)
            self.assertEqual(result['decisions']['hiring']['live_failed_fresh_succeeded'], 1)
            self.assertEqual(result['outcomes']['missed_hire']['elapsed_ticks']['p95'], 8)
            self.assertEqual(result['censored']['missed_hire'], 1)
            self.assertEqual(result['economy']['deaths'], 1)

    def test_distributions_keep_population_and_use_nearest_rank(self):
        self.assertEqual(distribution([]), {'count': 0})
        self.assertEqual(distribution(range(1, 101))['p95'], 95)

    def test_resource_volume_screen_uses_accepted_quantities(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'flows.json'
            path.write_text(json.dumps({'rows': [dict(workload='example', delay=d,
                final_checksum='abcd', counters=dict(delivered_0=quantity, delivered_1=0,
                harvested_0=110, deaths_0_1=2, deaths_1_0=3))
                for d, quantity in ((0, 100), (4, 97))]}))
            rows = resource_volumes(path)['example']
            self.assertEqual(rows['4']['accepted_throughput_ratio'], .97)
            self.assertTrue(rows['4']['screening_flag'])
            self.assertEqual(rows['4']['starvation_deaths'], 2)

    def test_retained_market_counts_ignore_candidate_rows_and_unavailable_fetches(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'building-gradient-impact-decisions.csv').write_text(
                'kind,live_resource,fresh_resource,live_choice,fresh_choice\n'
                'hiring_candidate,0,0,-1,10\n'
                'resource,-1,0,-1,10\n'
                'resource,0,0,-1,10\n'
                'resource,0,0,10,11\n')
            self.assertEqual(corrected_market_counts(directory),
                             dict(market_vs_harvesting_changed=1, market_identity_changed=1))

    def test_unavailable_fetch_is_not_a_harvesting_choice(self):
        self.assertEqual(market_changes(dict(live_resource=-1, fresh_resource=0,
                                            live_choice=-1, fresh_choice=10)), (False, False))
        self.assertEqual(market_changes(dict(live_resource=0, fresh_resource=0,
                                            live_choice=-1, fresh_choice=10)), (True, False))

    def test_checkpoint_trips_are_left_censored_but_new_hires_are_complete(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'building-gradient-impact-ticks.csv').write_text('tick\n2\n')
            (directory / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,kind,building,building_identity,unit,unit_identity,elapsed_ticks,censored,distance,reversals\n'
                '1,hired,1,1,3,1,0,0,0,0\n'
                '1,hired,1,1,4,1,0,0,0,0\n'
                '1,delivered,1,1,4,1,0,0,0,0\n'
                '4,delivered,1,1,5,1,0,0,0,0\n'
                '6,delivered,1,1,3,1,4,0,0,0\n'
                '8,delivered,1,1,2,1,6,0,0,0\n'
                '15,delivered,1,1,2,1,8,0,0,0\n')
            result = trip_observations(directory)
            self.assertEqual(result['complete_duration_ticks']['count'], 3)
            self.assertEqual(result['complete_duration_ticks']['p95'], 8)
            self.assertEqual(result['left_censored_duration_lower_bounds']['count'], 2)

    def test_intervention_ignores_prior_events_and_later_replacement_trips(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,event,kind,building,building_identity,unit,unit_identity\n'
                '10,4,delivered,1,1,2,1\n'
                '10,6,delivered,1,1,2,2\n'
                '11,7,abandoned,1,1,2,1\n'
                '12,8,hired,1,1,2,1\n'
                '13,9,harvested,1,1,2,1\n'
                '14,10,delivered,1,1,2,1\n')
            case = dict(tick=10, event=5, building=1, building_identity=1,
                        unit=2, unit_identity=1)
            observed = case_observations(directory, 'movement', case)
            self.assertEqual(observed['trip_endpoint']['kind'], 'abandoned')
            self.assertIsNone(observed['delivered'])
            self.assertIsNone(observed['harvested'])
            self.assertIsNone(observed['hired'])
            bounded = case_observations(directory, 'movement', case, horizon=11)
            self.assertEqual(bounded['trip_endpoint']['kind'], 'horizon_censored')
            self.assertIsNone(bounded['delivered'])

    def test_market_pickup_is_acquired_but_is_not_a_map_harvest(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,event,kind,building,building_identity,unit,unit_identity,resource,source_building\n'
                '11,6,market_acquired,1,1,2,1,5,3\n'
                '12,7,delivered,1,1,2,1,5,-1\n')
            case = dict(tick=10, event=5, building=1, building_identity=1,
                        unit=2, unit_identity=1)
            result = case_observations(directory, 'resource', case)
            self.assertIsNone(result['harvested'])
            self.assertEqual(result['acquired']['kind'], 'market_acquired')
            self.assertEqual(result['market_acquired']['source_building'], '3')

    def test_later_hire_after_demand_disappeared_is_not_an_exact_delay(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,event,kind,building,building_identity,unit,unit_identity,resource,elapsed_ticks,censored,censor_reason\n'
                '12,7,missed_hire_candidate,1,1,2,1,0,2,1,staffing_demand_filled\n'
                '15,8,hired,1,1,2,1,0,0,0,\n'
                '20,9,delivered,1,1,2,1,0,5,0,\n')
            case = dict(tick=10, event=5, building=1, building_identity=1,
                        unit=2, unit_identity=1, fresh_resource=0)
            observed = case_observations(directory, 'hiring_candidate', case)
            self.assertIsNone(observed['hired'])
            self.assertIsNone(observed['delivered'])
            self.assertEqual(observed['trip_endpoint']['kind'], 'hiring_opportunity_censored')
            self.assertEqual(observed['hiring_episode_endpoint']['censor_reason'], 'staffing_demand_filled')

    def test_censored_later_episode_does_not_replace_a_completed_original_trip(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'building-gradient-impact-outcomes.csv').write_text(
                'tick,event,kind,building,building_identity,unit,unit_identity,resource,elapsed_ticks,censored\n'
                '11,6,hired,1,1,2,1,0,0,0\n'
                '12,7,delivered,1,1,2,1,0,1,0\n'
                '20,9,missed_hire_candidate,1,1,2,1,0,1,1\n')
            case = dict(tick=10, event=5, building=1, building_identity=1,
                        unit=2, unit_identity=1, fresh_resource=0)
            observed = case_observations(directory, 'hiring_candidate', case)
            self.assertEqual(observed['delivered']['tick'], '12')
            self.assertIsNone(observed['hiring_episode_endpoint'])


if __name__ == '__main__':
    unittest.main()
