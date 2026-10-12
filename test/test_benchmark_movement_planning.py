import unittest
from benchmark_movement_planning import DELAYS, WORKLOADS, evaluate


class MovementGateTest(unittest.TestCase):
    def corpus(self):
        scenarios = [dict(id=w, workload=w, qualified=True, modes=['idle']) for w in sorted(WORKLOADS)]
        records = {}
        for s in scenarios:
            for delay in (0, *DELAYS):
                records[(s['id'], delay)] = dict(finalChecksum=17,
                    ai_pipeline=dict(extraction_ns=10), movement_shadow=dict(delay=delay,
                    workers_settled=1, maximum_batches=delay+1, admission_ns=10,
                    team_0_idle_stable_opportunities=1000, team_0_idle_valid=970,
                    team_0_idle_reference_phase_2_ns=1000))
        return scenarios, records

    def test_complete_evidence_passes_and_missing_corpus_is_inconclusive(self):
        scenarios, records = self.corpus()
        self.assertEqual(evaluate(records, scenarios)['status'], 'pass')
        self.assertEqual(evaluate(records, scenarios[:-1])['status'], 'inconclusive')
        self.assertEqual(evaluate({}, scenarios)['status'], 'inconclusive')

    def test_each_delay_is_independent_and_low_coverage_blocks_live(self):
        scenarios, records = self.corpus()
        for delay in DELAYS:
            records[(scenarios[0]['id'], delay)]['movement_shadow']['team_0_idle_valid'] = 949
        decision = evaluate(records, scenarios)
        self.assertEqual(decision['status'], 'fail')
        self.assertFalse(decision['live_implementation_allowed'])
        records[(scenarios[0]['id'], 1)]['movement_shadow']['team_0_idle_valid'] = 950
        self.assertEqual(evaluate(records, scenarios)['delays']['1']['status'], 'pass')

    def test_no_observations_or_cost_data_cannot_pass(self):
        scenarios, records = self.corpus()
        for delay in DELAYS:
            row = records[(scenarios[0]['id'], delay)]
            row['movement_shadow']['team_0_idle_stable_opportunities'] = 0
            row['ai_pipeline'] = {}
        self.assertEqual(evaluate(records, scenarios)['status'], 'inconclusive')

    def test_overlap_and_admission_are_not_reported_as_saving(self):
        scenarios, records = self.corpus()
        for delay in DELAYS:
            metrics = records[(scenarios[0]['id'], delay)]['movement_shadow']
            metrics['admission_ns'] = 1000
            metrics['reference_search_ns'] = 1000000  # inclusive, must not be added
        self.assertEqual(evaluate(records, scenarios)['status'], 'fail')

    def test_a_checksum_change_fails(self):
        scenarios, records = self.corpus()
        for delay in DELAYS:
            records[(scenarios[0]['id'], delay)]['finalChecksum'] = 18
        self.assertEqual(evaluate(records, scenarios)['status'], 'fail')


if __name__ == '__main__':
    unittest.main()
