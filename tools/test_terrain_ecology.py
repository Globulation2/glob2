#!/usr/bin/env python3
"""Contract tests for ecology acceptance evidence and sampling diagnostics."""
import unittest
from terrain_ecology import compare


def sample(map_name='a',seed=1,variant='baseline',added=10,resource=0,scenario='unattended',tick=4096):
    row=dict(map=map_name,seed=seed,variant=variant,scenario=scenario,tick=tick)
    for name in ('stock','occupied','added','removed','new_tiles','harvested'):
        row[name]=[0]*8
    row['stock'][resource]=100+added
    row['occupied'][resource]=50
    row['added'][resource]=added
    return row


def paired(n=16,baseline=10,candidate=10,**kwargs):
    return [sample(seed=seed,variant=variant,added=value(seed) if callable(value) else value,**kwargs)
            for seed in range(1,n+1) for variant,value in [('baseline',baseline),('candidate',candidate)]]


def metric(report,resource='wood',name='net_growth'):
    return next(r for r in report['comparisons'] if r['resource']==resource and r['metric']==name)


class EcologyAnalysisTest(unittest.TestCase):
    def test_balanced_equal_assay_passes(self):
        report=compare(paired())
        self.assertTrue(report['passes'])
        self.assertTrue(report['conservation_verified'])
        self.assertFalse(report['inventory_verified'])

    def test_two_seed_pilot_cannot_certify_success(self):
        report=compare(paired(2))
        self.assertTrue(report['raw_passes'])
        self.assertFalse(report['passes'])
        self.assertEqual(report['primary_status'],'inconclusive')

    def test_noise_is_not_treated_as_poisson_event_precision(self):
        report=compare(paired(baseline=lambda s:100000 if s%2 else 1,
                              candidate=lambda s:100000 if s%2 else 1))
        self.assertTrue(report['raw_passes'])
        self.assertEqual(metric(report)['adequately_sampled_maps'],0)

    def test_candidate_variance_cannot_silently_pass(self):
        report=compare(paired(candidate=lambda s:20 if s%2 else 0))
        self.assertTrue(metric(report)['raw_passes'])
        self.assertEqual(metric(report)['status'],'inconclusive')

    def test_additional_sparse_map_seeds_do_not_change_corpus_weights(self):
        first=compare(paired(16,10,20,map_name='a')+paired(16,90,90,map_name='b'))
        extra=compare(paired(64,10,20,map_name='a')+paired(16,90,90,map_name='b'))
        self.assertEqual(metric(first)['corpus_relative_error'],.1)
        self.assertEqual(metric(first)['corpus_relative_error'],metric(extra)['corpus_relative_error'])

    def test_resource_ids_and_zero_baseline_emergence(self):
        report=compare(paired(16,0,1,resource=4))
        self.assertFalse(metric(report,'algae')['raw_passes'])
        self.assertIsNone(metric(report,'algae')['corpus_relative_error'])
        self.assertTrue(metric(report,'papyrus')['raw_passes'])

    def test_missing_pairs_duplicates_and_conservation_fail_closed(self):
        rows=paired()
        for invalid in (rows[:-1],rows+[rows[0]]):
            with self.assertRaises(ValueError):compare(invalid)
        rows=paired();rows[-1]['stock'][0]+=1
        with self.assertRaisesRegex(ValueError,'conservation'):compare(rows)

    def test_manifest_seed_and_snapshot_inventory(self):
        rows=[]
        for name in ('a','b'):
            for scenario in ('unattended','replenishment'):
                for tick in (1024,4096):
                    rows+=paired(map_name=name,scenario=scenario,tick=tick)
        self.assertTrue(compare(rows,['a','b'],range(1,17),[1024,4096])['inventory_verified'])
        for maps,seeds,ticks in [(['a'],range(1,17),[1024,4096]),
                                 (['a','b'],range(1,18),[1024,4096]),
                                 (['a','b'],range(1,17),[1024,4096,16384])]:
            with self.assertRaises(ValueError):compare(rows,maps,seeds,ticks)


if __name__=='__main__':
    unittest.main()
