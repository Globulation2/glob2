"""Statistical unit of replication and missing-evidence contracts."""
import unittest
import json
from pathlib import Path
import tempfile
from unittest.mock import patch
from analyze_warrior_tactics import paired_interval, summarize, medical_contact_summary, STATES


class PairedCampaignTests(unittest.TestCase):
    def rows(self, copies=1):
        rows = []
        for cluster, change in [('a', -.2), ('b', .2)]:
            for seat in range(copies):
                for arm, value in [('baseline', .4), ('candidate', .4+change)]:
                    rows.append(dict(id=f'{arm}-{cluster}-{seat}', pair=f'{cluster}-{seat}',
                                     arm=arm, cluster=cluster, value=value))
        return rows

    def test_rotations_do_not_create_independent_terrain_evidence(self):
        one = paired_interval(self.rows(), 'baseline', 'candidate', 'value', samples=2000)
        many = paired_interval(self.rows(20), 'baseline', 'candidate', 'value', samples=2000)
        self.assertEqual(one['clusters'], many['clusters'])
        self.assertAlmostEqual(one['estimate'], many['estimate'])
        for a, b in zip(one['ci95'], many['ci95']):
            self.assertAlmostEqual(a, b)
        self.assertLess(one['ci95'][0], 0)
        self.assertGreater(one['ci95'][1], 0)

    def test_missing_arm_is_an_error(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            paired_interval(self.rows()[:-1], 'baseline', 'candidate', 'value')

    def test_unavailable_metric_is_not_zero(self):
        rows = self.rows()
        rows[0]['value'] = None
        summary = paired_interval(rows, 'baseline', 'candidate', 'value')
        self.assertEqual(summary['unavailable_pairs'], 1)
        self.assertEqual(summary['pairs'], 1)
        self.assertIsNone(summary['ci95'])

    def test_pair_cannot_cross_clusters(self):
        rows = self.rows()
        rows[0]['cluster'] = 'other'
        with self.assertRaisesRegex(ValueError, 'crosses'):
            paired_interval(rows, 'baseline', 'candidate', 'value')

    def test_all_ties_are_explicit_and_do_not_imply_equivalence(self):
        rows = self.rows()
        for row in rows:
            row['value'] = 1
        report = paired_interval(rows,'baseline','candidate','value',samples=500)
        self.assertEqual(report['tied_pairs'],2)
        self.assertEqual(report['positive_pairs'],0)
        self.assertEqual(report['negative_pairs'],0)
        self.assertTrue(report['bootstrap_degenerate'])


class ObservationSummaryTests(unittest.TestCase):
    def test_medical_sorties_follow_actual_departure_return_and_censoring(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            (root/'result.json').write_text(json.dumps(dict(ticks=12,unresolved=True,
                resolved={'tick_limit':12},teams=[dict(team=0,outcome='unresolved',eliminated_tick=-1)])))
            window={state:'0' for state in STATES}
            window.update(team='0',start='0',end='12',alive='1',warriors='1',combat_power='1',
                          to_flag='6',to_food='2',inside_service='2',idle='2',moved_ticks='0',
                          experienced_ticks='0',melee_building_damage='0')
            def episode(start,end,state,flag='65535',reason='changed'):
                return dict(team='0',gid='0',generation='1',start=str(start),end=str(end),
                    ticks=str(end-start),state=state,flag_gid=flag,flag_generation='1',
                    previous_flag_gid='3',previous_flag_generation='1',service_gid='7',
                    service_generation='1',service_purpose='18',end_reason=reason)
            episodes=[episode(0,2,'to_flag','3'),episode(2,4,'to_food'),
                      episode(4,6,'inside_service'),episode(6,8,'idle'),
                      episode(8,12,'to_flag','4','right_censored')]
            tables={'warrior-windows.csv':[window],'warrior-episodes.csv':episodes,'warrior-journeys.csv':[]}
            events=[(0,'offense_flag',dict(flag_gid=str(n),flag_generation='1')) for n in (3,4)]
            with patch('analyze_warrior_tactics.read_csv',side_effect=lambda p:iter(tables[p.name])), \
                 patch('analyze_warrior_tactics.maxima_events',return_value=events):
                report=summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['medical_sortie_outcomes'],{'returned_to_offense':1})
                self.assertEqual(report['completed_medical_sortie_median'],6)
                self.assertEqual(report['medical_return_fraction'],1)
                self.assertEqual(sum(report['medical_sortie_duty'].values()),6)
                episodes[-1].update(state='to_food',flag_gid='65535',end_reason='disappeared')
                window.update(to_flag='2',to_food='6')
                report=summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['medical_sortie_outcomes'],{'disappeared':1})
                self.assertEqual(report['medical_return_fraction'],0)
                self.assertIsNone(report['completed_medical_sortie_median'])
                episodes[-1]['end_reason']='changed'
                report=summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['medical_sortie_outcomes'],{'right_censored':1})
                # A remembered flag alone cannot establish a medical departure.
                episodes[0].update(state='idle',flag_gid='65535')
                window.update(to_flag='0',idle='4')
                report=summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['medical_sortie_outcomes'],{})

    def test_forward_use_joins_generations_and_continuations_do_not_claim_past_damage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/'result.json').write_text(json.dumps(dict(
                ticks=12, unresolved=False, resolved={'tick_limit': 12},
                teams=[dict(team=0,outcome='lost',eliminated_tick=12)])))
            window = {state: '0' for state in STATES}
            window.update(team='0',start='10',end='12',alive='1',warriors='1',combat_power='1',
                          inside_service='2',moved_ticks='0',experienced_ticks='0',melee_building_damage='999')
            episode = dict(team='0',gid='0',generation='1',start='10',end='12',ticks='2',
                           state='inside_service',flag_gid='65535',flag_generation='0',
                           previous_flag_gid='65535',previous_flag_generation='0',
                           service_gid='7',service_generation='2',service_purpose='18')
            tables = {'warrior-windows.csv': [window], 'warrior-episodes.csv': [episode],
                      'warrior-journeys.csv': []}
            events = [(10,'forward_support_provider',dict(building_gid='7',building_generation='1'))]
            with patch('analyze_warrior_tactics.read_csv',side_effect=lambda p: iter(tables[p.name])), \
                 patch('analyze_warrior_tactics.maxima_events',return_value=events):
                report = summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['used_forward_providers'],0)
                self.assertIsNone(report['melee_building_damage'])
                self.assertIsNone(report['damage_per_warrior_tick'])
                self.assertEqual(report['unresolved'],0)
                self.assertIsNone(report['melee_class_starvation_deaths'])
                # A cumulative final count cannot be assigned to this interval
                # without a real initial sample, even when its column exists.
                window['melee_class_starvation_deaths']='9'
                report=summarize(dict(id='a',team=0,directory='.'),root)
                self.assertIsNone(report['melee_class_starvation_deaths'])
                initial=dict(window,start='10',end='10',inside_service='0',
                             melee_class_starvation_deaths='7')
                tables['warrior-windows.csv'].insert(0,initial)
                report=summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['melee_class_starvation_deaths'],2)
                self.assertEqual(report['melee_class_starvation_deaths_per_warrior_tick'],1)
                window['melee_class_starvation_deaths']='6'
                with self.assertRaisesRegex(ValueError,'Decreasing cumulative'):
                    summarize(dict(id='a',team=0,directory='.'),root)
                window['melee_class_starvation_deaths']='9'
                episode['service_generation'] = '1'
                report = summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['used_forward_providers'],1)
                self.assertEqual(report['forward_recipients'],1)
                self.assertEqual(report['forward_service_ticks'],{'feeding_inside_service': 2})
                episode['state'] = 'training'
                episode['service_purpose'] = '17'
                window['inside_service'] = '0'
                window['training'] = '2'
                report = summarize(dict(id='a',team=0,directory='.'),root)
                self.assertEqual(report['duty']['training'],0)
                self.assertEqual(report['duty']['to_heal'],2)
                self.assertEqual(report['medical_travel_fraction'],1)
                self.assertEqual(report['forward_service_ticks'],{'healing_to_heal': 2})
                self.assertEqual(report['assigned_forward_providers'],1)
                self.assertEqual(report['forward_assignees'],1)
                self.assertEqual(report['used_forward_providers'],0)
                self.assertEqual(report['forward_recipients'],0)
                window['end'] = '11'
                with self.assertRaisesRegex(ValueError,'terminal tick'):
                    summarize(dict(id='a',team=0,directory='.'),root)


class ClinicalContactTests(unittest.TestCase):
    flags = {('3', '1')}

    def episode(self, start, state, flag='65535', purpose='18', reason='changed'):
        return dict(gid='1', generation='1', start=str(start), end=str(start+1),
                    state=state, flag_gid=flag, flag_generation='1' if flag=='3' else '0',
                    service_purpose=purpose, end_reason=reason)

    def test_reassignment_is_not_combat_contact(self):
        e = self.episode
        result = medical_contact_summary([e(0,'to_flag','3'),e(10,'to_food'),
            e(20,'to_flag','3'),e(30,'fighting','3')], self.flags)
        self.assertEqual(result['strict_medical_contact_median'],20)
        self.assertEqual(result['strict_medical_contact_fraction'],1)

    def test_upgrade_only_departure_is_not_clinical(self):
        e = self.episode
        result = medical_contact_summary([e(0,'to_flag','3'),
            e(10,'training',purpose='4')], self.flags)
        self.assertEqual(result['strict_medical_departures'],0)
        self.assertEqual(result['nonclinical_service_departures'],1)

    def test_repeated_visits_before_contact_keep_original_clock(self):
        e = self.episode
        result = medical_contact_summary([e(0,'to_flag','3'),e(10,'to_food'),
            e(20,'to_flag','3'),e(25,'to_heal'),e(30,'to_flag','3'),
            e(40,'fighting','3')], self.flags)
        self.assertEqual(result['strict_medical_departures'],1)
        self.assertEqual(result['strict_medical_contact_median'],30)

    def test_loss_and_censoring_are_not_fast_returns(self):
        e = self.episode
        for reason in ('disappeared','converted','right_censored'):
            with self.subTest(reason=reason):
                result = medical_contact_summary([e(0,'to_flag','3'),
                    e(10,'to_food',reason=reason)], self.flags)
                self.assertEqual(result['strict_medical_contact_fraction'],0)
                self.assertIsNone(result['strict_medical_contact_median'])
                self.assertEqual(result['strict_medical_contact_outcomes'][reason],1)

    def test_service_purpose_recovers_legacy_medical_training_label(self):
        e = self.episode
        result = medical_contact_summary([e(0,'to_flag','3'),
            e(10,'training',purpose='17'),e(20,'fighting','3')], self.flags)
        self.assertEqual(result['strict_medical_departures'],1)
        self.assertEqual(result['strict_medical_contact_median'],10)

    def test_other_flag_ends_episode_before_later_offense(self):
        e = self.episode
        result = medical_contact_summary([e(0,'to_flag','3'),e(10,'to_food'),
            e(20,'to_flag','4'),e(30,'fighting','3')], self.flags)
        self.assertEqual(result['strict_medical_contact_outcomes']['assigned_other_flag'],1)
        self.assertEqual(result['strict_medical_contact_fraction'],0)

    def test_recycled_unit_identity_cannot_complete_previous_departure(self):
        e = self.episode
        replacement = dict(e(30,'fighting','3'), generation='2')
        result = medical_contact_summary([e(0,'to_flag','3'),
            e(10,'to_food',reason='disappeared'),replacement], self.flags)
        self.assertEqual(result['strict_medical_contact_outcomes']['disappeared'],1)
        self.assertEqual(result['strict_medical_contact_fraction'],0)


if __name__ == '__main__':
    unittest.main()
