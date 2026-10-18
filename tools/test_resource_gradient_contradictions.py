# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from tools import resource_gradient_dataset as d
from tools import resource_gradient_model as m
from tools import resource_gradient_contradictions as c


class ContradictionTests(unittest.TestCase):
    def rows(self):
        rows=[]
        for group in range(16):
            for i in range(12):
                values=dict(zip(d.CONTRADICTION_COUNTERS,[i,0,0,i//2,i//3,20]))
                rows.append(dict(format='duel',group=f'map-{group}',collection=str(group),cohort='core',cohort_role='exploration',
                    generator='river',dimensions='128x128',queries=4,defer_stalls=0,refresh_stalls=0,movement_label_available=1,contradiction_capture_known=1,
                    contradiction_contract=d.canonical_hash(d.CONTRADICTION_CONTRACT),defer_extra=5*i,refresh_extra=2,
                    **dict.fromkeys(m.FEATURES,2),**values))
        return rows

    def test_fraction_integer_preparation_and_exclusion_are_paired(self):
        rows=self.rows();rows[0]['contradiction_capture_known']=0;rows[1]['movement_label_available']=0
        prepared,excluded=c.prepare(rows)
        self.assertEqual(len(prepared),len(rows)-2)
        self.assertEqual(excluded,{'unknown_capture':1,'planning_only':1})
        self.assertEqual(prepared[0][c.FRACTIONS[0]],2*4096//20)
        self.assertNotIn(c.FRACTIONS[0],rows[2])
        rows[2]['contradiction_contract']=None
        with self.assertRaisesRegex(ValueError,'explicit contradiction contract'):c.prepare(rows)

    def test_augmented_integer_models_cannot_be_emitted_as_production(self):
        try:import sklearn
        except ImportError:self.skipTest('scikit-learn required')
        rows,_=c.prepare(self.rows());spec=c.variants(False)['counts_fractions'];model=m.fit_heads(rows,**spec)
        self.assertTrue(model['research_only']);m.validate_heads(model,research=True)
        self.assertIsInstance(m.predict(model,[rows[0][k] for k in model['features']]),int)
        with tempfile.TemporaryDirectory() as temp:
            target=Path(temp)/'production.h'
            with self.assertRaises(ValueError):m.emit(model,target)
            self.assertFalse(target.exists())
        with self.assertRaisesRegex(ValueError,'length mismatch'):m.predict(model,[0]*7)
        baseline=json.loads(m.DEFAULT_MODEL.read_text());baseline['transforms']=['identity_q12']*7
        with self.assertRaisesRegex(ValueError,'log transforms'):m.validate_model(baseline)

    def test_contract_columns_and_unknown_old_capture_are_not_imputed(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);episode=root/'e.csv';manifest=root/'manifest.json'
            header=['complete','queries','defer_extra','refresh_extra',*m.FEATURES,*d.CONTRADICTION_COUNTERS,d.CONTRADICTION_KNOWN,*d.CONTRADICTION_STALL_COLUMNS]
            episode.write_text(','.join(header)+'\n'+','.join(map(str,[1,1,0,0,*([0]*7),0,0,0,0,0,1,0,0,0]))+'\n')
            contract=dict(features=list(m.FEATURES),label='movement-direction-extra-cost',future_offsets=[8,31],benefit_unit='direction-cost',request_type='movement',contradictions=d.CONTRADICTION_CONTRACT)
            record=dict(group='one',format='duel',episodes='e.csv',episode_sha256=d.file_hash(episode),binary_sha256='a',
                        cohort=dict(id='new',policy={'name':'explore'},role='exploration',contract=contract))
            manifest.write_text(json.dumps([record]));rows=d.load([manifest])
            with self.assertRaisesRegex(ValueError,'No known-capture'):c.prepare(rows)
            episode.write_text('complete,defer_extra,refresh_extra,'+','.join(m.FEATURES)+'\n1,0,0,'+','.join(['0']*7)+'\n')
            record['episode_sha256']=d.file_hash(episode);manifest.write_text(json.dumps([record]))
            with self.assertRaisesRegex(ValueError,'Missing contradiction'):d.load([manifest])

    def test_comparison_uses_same_rows_splits_and_does_not_promote_coefficients(self):
        try:import sklearn
        except ImportError:self.skipTest('scikit-learn required')
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);manifest=root/'manifest.json';manifest.write_text('[]')
            output=root/'comparison.json';rows=self.rows();rows[0]['contradiction_capture_known']=0
            args=argparse.Namespace(manifest=[manifest],split_registry=root/'splits.json',output=output,no_ablations=True)
            production=m.DEFAULT_MODEL.read_bytes();header=m.HEADER.read_bytes()
            with patch.object(m,'dataset',return_value=rows),contextlib.redirect_stdout(io.StringIO()):report=c.compare(args)
            self.assertEqual(report['rows'],191);self.assertEqual(report['excluded'],{'unknown_capture':1})
            self.assertEqual(set(report['variants']),set(c.variants(False)))
            self.assertTrue(all(v['holdout']['episodes']==report['variants']['baseline7']['holdout']['episodes'] for v in report['variants'].values()))
            self.assertFalse(set(report['development_states'])&set(report['heldout_states']))
            self.assertEqual(production,m.DEFAULT_MODEL.read_bytes());self.assertEqual(header,m.HEADER.read_bytes())
            json.dumps(report,allow_nan=False)
            self.assertTrue(output.exists());self.assertEqual(len(report['variants']['counts']['cross_validation']),3)

    def test_comparison_cannot_drop_an_unsupported_or_zero_episode_format(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);manifest=root/'m.json';output=root/'compare.json';registry=root/'split.json'
            args=argparse.Namespace(manifest=[manifest],output=output,split_registry=registry,no_ablations=True)
            for missing_rows in (True,False):
                rows=self.rows()
                if not missing_rows:
                    unsupported=dict(rows[0],format='teams',group='unsupported',contradiction_capture_known=0)
                    rows.append(unsupported)
                manifest.write_text(json.dumps([dict(format='teams',group='unsupported')]))
                with patch.object(m,'dataset',return_value=rows),self.assertRaisesRegex(ValueError,'known-capture movement maps.*teams'):
                    c.compare(args)
                self.assertFalse(registry.exists());self.assertFalse(output.exists())

    def test_unsampled_totals_are_verified_and_not_summed_episode_histories(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);totals=root/'totals.csv'
            totals.write_text('epoch,key,empty_goal,persistent_progress_blocked,unit_progress_blocked,sidestep_used,reachable_no_move,movement_queries\n0,4,2,1,0,3,1,10\n1,4,0,0,0,0,1,5\n')
            manifest=root/'m.json';record=dict(group='game',binary_sha256='binary',episode_sha256='episode',format='duel',
                provenance={'artifacts':{'gradient-contradictions.csv':dict(path='totals.csv',sha256=d.file_hash(totals))}})
            manifest.write_text(json.dumps([record,record]));report=c.live_totals([manifest])
            self.assertEqual(report['games'],1);self.assertEqual(report['overall']['counts']['movement_queries'],15)
            self.assertAlmostEqual(report['overall']['rates']['reachable_no_move'],2/15)
            totals.write_text('changed')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):c.live_totals([manifest])

    def test_separate_planning_and_stall_targets_never_enter_movement_label(self):
        rows=self.rows();rows[0].update(planning_queries=3,defer_planning_false_negative=2,refresh_planning_false_negative=0,defer_stalls=1,refresh_stalls=0)
        report=c.separate_outcomes(rows)
        self.assertEqual(report['defer_planning_false_negative'],2)
        self.assertEqual(report['defer_stalls'],1)
        self.assertEqual(rows[0]['defer_extra']-rows[0]['refresh_extra'],-2)


if __name__=='__main__':unittest.main()
