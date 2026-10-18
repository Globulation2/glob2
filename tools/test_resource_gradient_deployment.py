# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import resource_gradient_model as m
from tools import resource_gradient_features as f
from tools import resource_gradient_deployment as deployment
from tools.resource_gradient_dataset import canonical_hash, CONTRADICTION_COUNTERS, CONTRADICTION_KNOWN


class ResourceGradientDeploymentTest(unittest.TestCase):
    def setUp(self):
        self.model = json.loads(m.DEFAULT_MODEL.read_text())
        self.candidate = copy.deepcopy(self.model)
        for key in ('deployment_contract', 'legacy', 'legacy_source_sha256', 'prospective_validation_required'):
            self.candidate.pop(key)
        self.candidate.update(schema=2, research_only=True, candidate_contract=copy.deepcopy(f.CONTRACT))

    def row(self, group='map', fmt='duel'):
        return dict(group=group, format=fmt, movement_label_available=1,
                    contradiction_contract=canonical_hash(f.CONTRACT['diagnostic_contract']),
                    **dict.fromkeys(m.BASE_FEATURES, 0),
                    **dict.fromkeys(CONTRADICTION_COUNTERS, 0),
                    **{CONTRADICTION_KNOWN: 1})

    def test_base_collection_contract_stays_seven_columns(self):
        self.assertEqual(m.FEATURES, m.BASE_FEATURES)
        self.assertEqual(len(m.BASE_FEATURES), 7)
        self.assertEqual(len(f.FEATURES), 12)
        self.assertEqual(f.COUNTER_IDS, (0, 4, 5))

    def test_uncapped_rates_precede_count_clamp_and_raw_rows_remain_raw(self):
        self.assertEqual(f.prepare([0]*7, [1_000_001, 0, 2_000_002])[-5:],
                         [1_000_000, 0, 1_000_000, 2048, 0])
        row = self.row()
        row['since_capture_empty_goal'] = 1_000_001
        row['since_capture_movement_queries'] = 2_000_002
        rows, excluded = f.prepare_rows([row])
        self.assertFalse(excluded)
        self.assertEqual(rows[0]['since_capture_movement_queries'], 2_000_002)
        self.assertEqual(rows[0]['since_capture_empty_goal_frac_q12'], 2048)
        self.assertNotIn('since_capture_empty_goal_frac_q12', row)

    def test_uint64_fraction_boundaries_and_corrupt_inference_clamp(self):
        maximum = f.UINT64_MAX
        for events, queries in [(0,0), (0,maximum), (maximum,maximum),
                                (maximum-1,maximum), (maximum//2,maximum),
                                (maximum//4096,maximum), (maximum//4096+1,maximum),
                                (maximum,1), (maximum,0)]:
            self.assertEqual(f.q12_fraction(events, queries), min(events,queries)*4096//max(1,queries))
        for value in (-1, maximum+1, 1.0, True):
            with self.assertRaises(ValueError): f.q12_fraction(value, 1)
            with self.assertRaises(ValueError): f.q12_fraction(0, value)
        # Defensive inference clamping must not make malformed training rows valid.
        row = self.row()
        row['since_capture_empty_goal'] = 1
        with self.assertRaises(ValueError): f.prepare_rows([row])

    def test_research_cannot_emit_without_explicit_exact_contract_promotion(self):
        deployment.validate_candidate(self.candidate)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)/'model.h'
            with self.assertRaises(ValueError): m.emit(self.candidate, target)
            self.assertFalse(target.exists())
        promoted = deployment.promote_model(self.candidate, self.model)
        deployment.validate(promoted)
        self.assertEqual(promoted['legacy'], self.model['legacy'])
        self.assertTrue(promoted['prospective_validation_required'])
        self.assertNotIn('research_only', promoted)

    def test_contract_order_transforms_and_legacy_identity_fail_closed(self):
        for mutate in (
            lambda x: x['features'].reverse(),
            lambda x: x['transforms'].__setitem__(0, 'identity_q12'),
            lambda x: x['deployment_contract'].__setitem__('counter_ids', [0,1,5]),
            lambda x: x.__setitem__('legacy_source_sha256', '0'*64),
            lambda x: x['legacy']['positive_mean']['coef'].__setitem__(0, 0),
            lambda x: x['legacy'].__setitem__('transforms', ['identity_q12']*7),
            lambda x: x.__setitem__('prospective_validation_required', False),
            lambda x: x.__setitem__('schema', 3.0),
            lambda x: x.__setitem__('threshold', 32000.0),
            lambda x: x['positive_mean'].__setitem__('shift', 28.0),
        ):
            broken = copy.deepcopy(self.model)
            mutate(broken)
            with self.assertRaises(ValueError): deployment.validate(broken)
        broken = copy.deepcopy(self.candidate)
        broken.pop('candidate_contract')
        with self.assertRaises(ValueError): deployment.promote_model(broken, self.model)

    def test_filtered_format_cannot_disappear_from_candidate_fit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            m.save(root/'manifest.json', [dict(format='duel'), dict(format='ffa')])
            args = argparse.Namespace(manifest=[root/'manifest.json'], output=root/'out.json',
                                      split_registry=root/'split.json', feature_set=f.CONTRACT_ID)
            rows = [self.row(str(i)) for i in range(4)]
            with mock.patch.object(m, 'dataset', return_value=rows), \
                    self.assertRaisesRegex(ValueError, "'ffa': 0"):
                deployment.fit_candidate(args)
            self.assertFalse((root/'split.json').exists())
            self.assertFalse((root/'out.json').exists())

    def test_baseline_fit_does_not_emit_cpp(self):
        with self.assertRaisesRegex(ValueError, 'explicit promote'):
            m.fit(argparse.Namespace(header='unwanted.h'))


if __name__ == '__main__': unittest.main()
