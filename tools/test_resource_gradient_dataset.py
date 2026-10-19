# SPDX-License-Identifier: GPL-3.0-or-later
"""Dataset contracts protect experimental evidence from silent policy drift."""
import json
import tempfile
import unittest
from pathlib import Path
from tools import resource_gradient_dataset as d
from tools import resource_gradient_model as m


class DatasetTests(unittest.TestCase):
    def record(self, directory, cohort=None, name='one'):
        episode=directory/f'{name}.csv'
        episode.write_text('complete,defer_extra,refresh_extra,'+','.join(d.FEATURES)+'\n1,40,0,512,8,16,0,0,0,0\n')
        value=dict(group=name,format='duel',binary_sha256='abc',episodes=episode.name,episode_sha256=d.file_hash(episode))
        if cohort:value['cohort']=cohort
        return value

    def cohort(self, name='explore'):
        return dict(id=name,policy={'name':'seeded-field-intervals','intervals':[32,512]},contract={
            'features':list(d.FEATURES),'label':'movement-direction-extra-cost','future_offsets':[8,31],
            'benefit_unit':'direction-cost','request_type':'movement'})

    def test_seal_replay_and_tamper(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);source=p/'manifest.json';source.write_text(json.dumps([self.record(p)]));sealed=p/'sealed.json'
            d.seal(source,sealed,self.cohort());d.seal(source,sealed,self.cohort())
            rows=d.load([sealed]);self.assertEqual(rows[0]['cohort'],'explore')
            report=d.support_audit(rows)
            self.assertEqual(report['overall']['high_demand_old'],1)
            self.assertFalse(report['planning_coverage']['measured'])
            value=json.loads(sealed.read_text());value['records'][0]['format']='ffa';sealed.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError,'hash mismatch'):d.load([sealed])

    def test_sealed_dataset_is_relocatable_with_its_files(self):
        import shutil
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);original=p/'original';original.mkdir()
            manifest=original/'manifest.json';manifest.write_text(json.dumps([self.record(original)]))
            d.seal(manifest,original/'cohort.json',self.cohort())
            shutil.copytree(original,p/'copied');shutil.rmtree(original)
            self.assertEqual(d.load([p/'copied/cohort.json'])[0]['cohort'],'explore')

    def test_contracts_never_silently_pool(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);a=p/'a.json';b=p/'b.json'
            a.write_text(json.dumps([self.record(p,name='old')]))
            b.write_text(json.dumps([self.record(p,self.cohort(),name='new')]))
            self.assertEqual(len(d.load([a])),1)
            with self.assertRaisesRegex(ValueError,'Incompatible label contracts'):d.load([a,b])
            wrong=self.cohort('other');wrong['contract']['future_offsets']=[7,31]
            a.write_text(json.dumps([self.record(p,wrong,name='old')]))
            with self.assertRaisesRegex(ValueError,'Incompatible label contracts'):d.load([a,b])

    def test_cohort_balance_and_shared_map_group(self):
        rows=[dict(format='duel',cohort='explore',group='same-map',collection='a')]
        rows += [dict(format='duel',cohort='candidate',group='same-map',collection='b') for _ in range(99)]
        weights=d.balanced_weights(rows)
        self.assertAlmostEqual(weights[0],sum(weights[1:]))
        self.assertEqual(len({r['group'] for r in rows}),1)

    def test_permanent_exploration_survives_many_candidate_generations(self):
        rows=[dict(format='duel',cohort_role='exploration',cohort='core',group='map',collection='core')]
        rows += [dict(format='duel',cohort_role='candidate',cohort=f'generation{i}',group='map',collection=str(i))
                 for i in range(99)]
        weights=d.balanced_weights(rows)
        self.assertAlmostEqual(weights[0],sum(weights[1:]))
        rows[1]['cohort']='core'
        with self.assertRaisesRegex(ValueError,'role changed'):d.balanced_weights(rows)

    def test_duplicate_cannot_gain_weight_under_new_cohort(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);r=self.record(p,self.cohort());a=p/'a.json';a.write_text(json.dumps([r]))
            b=p/'b.json';r['cohort']=self.cohort('other');b.write_text(json.dumps([r]))
            with self.assertRaisesRegex(ValueError,'multiple cohorts'):d.load([a,b])

    def test_planning_label_not_movement_target(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);cohort=self.cohort();cohort['contract']['request_type']='planning'
            a=p/'a.json';a.write_text(json.dumps([self.record(p,cohort)]))
            with self.assertRaisesRegex(ValueError,'Only movement labels'):d.load([a])

    def test_planning_only_and_quiet_movement_remain_distinct(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);record=self.record(p,self.cohort());episode=p/record['episodes']
            episode.write_text('complete,defer_extra,refresh_extra,queries,movement_observed,planning_queries,'+
                ','.join(d.FEATURES)+'\n1,0,0,0,0,9,512,0,0,0,0,0,0\n1,0,0,0,1,0,64,0,1,0,0,0,0\n')
            record['episode_sha256']=d.file_hash(episode);manifest=p/'m.json';manifest.write_text(json.dumps([record]))
            rows=d.load([manifest]);self.assertEqual([r['movement_label_available'] for r in rows],[0,1])
            self.assertEqual(d.support_audit(rows)['planning_coverage']['planning_only_episodes'],1)
            from unittest.mock import patch
            with patch.object(m,'predict',return_value=0):result=m.metrics(rows,{'threshold':32000})
            self.assertEqual(result['episodes'],1)

    def test_sealed_and_record_cohorts_cannot_conflict(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);source=p/'m.json';record=self.record(p,self.cohort('inside'))
            source.write_text(json.dumps([record]));sealed=p/'sealed.json'
            with self.assertRaisesRegex(ValueError,'conflicts'):d.seal(source,sealed,self.cohort('outside'))
            self.assertFalse(sealed.exists())
            cohort=self.cohort('outside');records=[record]
            sealed.write_text(json.dumps(dict(schema=1,cohort=cohort,records=records,
                content_sha256=d.canonical_hash(dict(cohort=cohort,records=records)))))
            with self.assertRaisesRegex(ValueError,'conflicts'):d.load([sealed])

    def test_registered_split_preserves_maps_across_expansion(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'split.json'
            rows=[dict(group=g,format='duel') for g in ['held','dev0','dev1','dev2']]
            def initial(_):return {'held'},{f'dev{i}':i for i in range(3)},{}
            held,folds,_=d.registered_split(rows,path,initial)
            before=json.loads(path.read_text())['assignments']
            expanded=rows+[dict(group=f'new{i}',format='duel') for i in range(50)]
            def forbidden(_):raise AssertionError('Must not recompute an existing split')
            d.registered_split(expanded,path,forbidden)
            after=json.loads(path.read_text())['assignments']
            self.assertTrue(all(after[g]==value for g,value in before.items()))
            self.assertEqual(after['held'],-1)
            self.assertEqual(len(after),54)

    def test_registered_split_rejects_inconsistent_and_never_partially_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'split.json'
            rows=[dict(group=g,format='duel') for g in ['held','dev0','dev1','dev2']]
            def bad(_):return {'held'},{f'dev{i}':0 for i in range(3)},{}
            with self.assertRaisesRegex(ValueError,'coverage'):d.registered_split(rows,path,bad)
            self.assertFalse(path.exists())
            path.write_text(json.dumps(dict(schema=1,strategy='map-hash-713-v1',assignments={'held':False})))
            frozen=path.read_bytes()
            with self.assertRaisesRegex(ValueError,'Inconsistent'):d.registered_split(rows,path,bad)
            self.assertEqual(path.read_bytes(),frozen)
            path.write_text(json.dumps(dict(schema=1,strategy='map-hash-713-v1',assignments={'held':-1,'dev0':0,'dev1':1,'dev2':2})))
            frozen=path.read_bytes()
            # A new format with only a holdout map must fail without committing it.
            with self.assertRaisesRegex(ValueError,'coverage'):d.registered_split(rows+[dict(group='held',format='ffa')],path,bad)
            self.assertEqual(path.read_bytes(),frozen)

    def test_decision_metrics_are_threshold_sensitive(self):
        rows=[dict(format='duel',group='g',collection='a',defer_extra=80,refresh_extra=0,**dict.fromkeys(d.FEATURES,0)),
              dict(format='duel',group='g',collection='a',defer_extra=0,refresh_extra=8,**dict.fromkeys(d.FEATURES,0))]
        from unittest.mock import patch
        with patch.object(m,'predict',side_effect=[0,40000]):
            result=m.metrics(rows,{'threshold':32000})
        self.assertEqual(result['decision_regret'],44)
        self.assertEqual(result['positive_benefit_captured_fraction'],0)
        self.assertEqual(result['false_refresh_pct'],50)
        self.assertEqual(result['missed_threshold_decision_pct'],50)


if __name__=='__main__':unittest.main()
