#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest
import tempfile
import io
import gzip
import json
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from tools.resource_claim_screen import DominanceScreen,goal_features,run,selected_observation,realized_farming_regime
from tools.resource_claim_offline import Candidate,FrozenEvent,METRIC_FIELDS,NO_TILE,write_catalog,identity
from tools.resource_claim_patterns import specifications


def matrix():
    m=np.zeros((3,2),dtype=[(n,'<f8' if n in ('ratio_sum','access_ratio_sum') else '<u8') for n in METRIC_FIELDS])
    m['events']=1;m['access_compared']=1;m['compared']=1;m['probes']=4;m['access_regret']=10
    m[1]['probes']=8;m[2]['probes']=8
    return m


class Screening(unittest.TestCase):
    def test_conservative_cut_preserves_rare_benefit_unknown_cost_and_backstop(self):
        split={'games':{str(i):'discovery' for i in range(16)},'strata':{str(i):'protected' for i in range(16)}}
        screen=DominanceScreen(3,[0],{3},split)
        for i in range(16):
            m=matrix();m[2,1]['access_compared']=0;m[2,1]['access_regret']=0
            if i==15:m[2,0]['access_regret']=9
            screen.add(str(i),m,{'family':'protected','material':1,'nearest_octant':i%8,'goal_density':'sparse','local_sparse_nearest_r16':True,'protected_food_adjacency_proxy':True,'tick':90000})
            if i==14:self.assertEqual(screen.report()['eliminated'],[])
        report=screen.report()
        self.assertTrue(report['ready_for_reversible_allocation'])
        self.assertEqual([r['candidate'] for r in report['eliminated']],[2])
        self.assertFalse(report['shipping_policy_selected'])

    def test_broad_only_data_cannot_eliminate_global_farm_candidates(self):
        split={'games':{str(i):'discovery' for i in range(30)},
               'strata':{str(i):'default' if i<16 else 'protected' if i<23 else 'pooled' for i in range(30)}}
        screen=DominanceScreen(3,[0],set(),split)
        for i in range(16):screen.add(str(i),matrix(),{'family':'default'})
        report=screen.report()
        self.assertFalse(report['ready_for_reversible_allocation']);self.assertEqual(report['eliminated'],[])
        self.assertEqual(report['missing_family_games'],{'protected':5,'pooled':5})

    def test_selected_observation_streams_nested_gzip_at_exact_ordinal(self):
        rows=[{'kind':'decision','ordinal':1,'tick':10,'features':{'pooled_farm':False}},
              {'kind':'decision','ordinal':7,'tick':90000,'material':1,'reason':'farm_reserve','features':{'pooled_farm':True}}]
        payload=gzip.compress(gzip.compress(b''.join((json.dumps(r)+'\n').encode() for r in rows)))
        class Source:
            def open_artifact(self,*args):return gzip.GzipFile(fileobj=io.BytesIO(payload),mode='rb')
        selected=selected_observation(Source(),{},7)
        self.assertEqual(selected['tick'],90000);self.assertTrue(selected['features']['pooled_farm'])
        with self.assertRaisesRegex(ValueError,'absent'):selected_observation(Source(),{},8)

    def test_family_labels_without_realized_late_farming_do_not_allow_cuts(self):
        split={'games':{str(i):'discovery' for i in range(16)},'strata':{str(i):'pooled' for i in range(16)}}
        screen=DominanceScreen(3,[0],set(),split)
        for i in range(16):screen.add(str(i),matrix(),{'family':'pooled','tick':90000,'goal_density':'sparse','pooled_farm':False})
        report=screen.report()
        self.assertEqual(report['missing_family_games'],{})
        self.assertEqual(report['missing_realized_regimes'],['late_sparse_or_exhausted_selected_pool'])
        self.assertFalse(report['ready_for_reversible_allocation']);self.assertEqual(report['eliminated'],[])

    def test_holdout_duplicate_and_multi_event_inputs_are_rejected(self):
        screen=DominanceScreen(3,[0],set(),{'games':{'d':'discovery','h':'holdout'}})
        with self.assertRaisesRegex(ValueError,'holdout'):screen.add('h',matrix(),{})
        m=matrix();m['events']=2
        with self.assertRaisesRegex(ValueError,'one frozen event'):screen.add('d',m,{})
        screen.add('d',matrix(),{})
        with self.assertRaisesRegex(ValueError,'unique'):screen.add('d',matrix(),{})

    def test_exhausted_selected_pool_qualifies_despite_other_available_pools(self):
        event=FrozenEvent(64,64,0,0,[0]*4096,
            [Candidate(0,component=0,available=False)]+[Candidate(i,component=1,available=True) for i in range(1,30)],
            game_id='g',event_id='e',optimum=4,
            provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c','metadata_mode':'published-xy','censored':False})
        f=goal_features(event,{'tick':90000,'material':1,'observation_features':{'pooled_farm':True}},{'farm_mode':'pooled'})
        self.assertFalse(f['local_sparse_nearest_r16'])
        self.assertGreater(f['local_goals']['nearest_r16']['available_goals'],0)
        self.assertEqual(f['nearest_pool_component']['available_stock_sites'],0)
        self.assertEqual(realized_farming_regime(f),'late_exhausted_selected_pool')
        f['nearest_pool_component']['nearest_access_eligible']=False
        self.assertIsNone(realized_farming_regime(f))

    def test_wild_food_and_known_false_direct_protection_do_not_qualify(self):
        event=FrozenEvent(32,32,0,0,[0]*1024,[Candidate(0)],game_id='g',event_id='e',optimum=None,
            provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c','metadata_mode':'published-xy','censored':False})
        meta={'tick':90000,'material':1,'observation_features':{'resource_growth_rate':1,'farm_areas_enabled':False}}
        self.assertIsNone(realized_farming_regime(goal_features(event,meta,{'farm_mode':'protected'})))
        meta['observation_features']['nearby_forbidden_count']=2
        proxy=goal_features(event,meta,{'farm_mode':'protected'})
        self.assertIsNone(proxy['protected_food_direct'])
        self.assertEqual(realized_farming_regime(proxy),'late_sparse_protected')
        meta['observation_features']['nearby_forbidden_growing_material_count']=0
        self.assertIsNone(realized_farming_regime(goal_features(event,meta,{'farm_mode':'protected'})))
        meta['observation_features']['nearby_forbidden_growing_material_count']=1
        direct=goal_features(event,meta,{'farm_mode':'protected'})
        self.assertTrue(direct['protected_food_direct']);self.assertEqual(realized_farming_regime(direct),'late_sparse_protected')

    def test_remote_dense_stocks_do_not_hide_local_sparse_farm(self):
        candidates=[Candidate(i,distance=4,access_distance=4) for i in range(64*24,64*40)]
        candidates.append(Candidate(0,available=False,distance=4,access_distance=4))
        event=FrozenEvent(64,64,0,0,[0]*4096,candidates,game_id='g',event_id='e',optimum=4,
            provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c','metadata_mode':'published-xy','censored':False})
        f=goal_features(event,{'tick':90000,'material':1},{'farm_mode':'protected'})
        self.assertEqual(f['goal_density'],'dense');self.assertTrue(f['local_sparse_nearest_r16'])
        self.assertEqual(f['local_goals']['nearest_r16']['legal_goals'],1)
        self.assertEqual(f['local_goals']['nearest_r16']['available_goals'],0)
        self.assertEqual(f['local_goals']['nearest_r16']['area_tiles'],797)

    def test_small_catalog_screen_pipeline_writes_bounded_complete_report(self):
        # Wire attestation has independent real-native parity tests. Here isolate
        # orchestration/SQLite factor selection with sixteen tiny synthetic games.
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);catalog=root/'catalog';out=root/'screen'
            write_catalog(catalog,list(specifications(families=['spiral','rays'],radii=[0,1,2],
                origins=['unit'],strides=[1],orders=['forward'],directions=[0])),reserve_bytes=0)
            games=[str(i) for i in range(16)]
            strata={g:('default' if int(g)<6 else 'protected' if int(g)<11 else 'pooled') for g in games}
            split={'games':{g:'discovery' for g in games}|{'held':'holdout'},'strata':strata|{'held':'default'},'cohort_identity':'c'}
            split['identity']=identity(split)
            splitpath=root/'split.json';splitpath.write_text(json.dumps(split))
            inputs=[];events={}
            provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c','metadata_mode':'published-xy','censored':False}
            for g in games:
                (root/g).mkdir();(root/g/'event.bin').write_bytes(('synthetic orchestration fixture '+g).encode())
                context={'source_revision':'s','bundle_id':'b','cohort_id':'c','game_id':g}
                inputs.append((root/g,context,{'farm_mode':strata[g],'claims_enforced':False}))
                event=FrozenEvent(32,32,0,6,[6]*1024,[Candidate(6,distance=8,access_distance=8)],
                    game_id=g,event_id='event',optimum=8,provenance=provenance)
                events[g]=(event,{'claims_enforced':False,'active_policy':'disabled','material':1,'tick':90000,'observation_features':{'pooled_farm':strata[g]=='pooled','nearby_forbidden_count':2,'resource_growth_rate':1,'farm_areas_enabled':False}})
            with patch('tools.resource_claim_screen.discovery_inputs',return_value=(inputs,[])), \
                 patch('tools.resource_claim_screen.read_event',side_effect=lambda path,*a,**k:events[path.parent.name]), \
                 patch('tools.resource_claim_screen.verify_pilot'):
                result=run('synthetic-results',catalog,splitpath,out)
            self.assertTrue(result['complete']);self.assertTrue(result['screening_ready'])
            report=json.loads((out/'screen.json').read_text())
            self.assertEqual(report['family_games'],{'default':6,'protected':5,'pooled':5})
            self.assertFalse(report['shipping_policy_selected'])
            self.assertEqual(len(json.loads((out/'timings.json').read_text())),32)

    def test_legal_goals_and_pool_availability_do_not_use_raw_stock_density(self):
        candidates=[Candidate(i,access_eligible=False,available=False) for i in range(20)]
        candidates[10]=Candidate(10,component=1,available=False,access_eligible=True,distance=3,access_distance=8)
        candidates.append(Candidate(40,component=1,available=True,access_eligible=False,distance=3,access_distance=10))
        event=FrozenEvent(32,32,0,10,[NO_TILE]*1024,candidates,game_id='g',event_id='e',optimum=3,
            provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c','metadata_mode':'published-xy','censored':False})
        f=goal_features(event,{'tick':90000,'material':1},{'farm_mode':'pooled'})
        self.assertEqual(f['raw_stock_density'],'dense');self.assertEqual(f['goal_density'],'sparse')
        self.assertEqual(f['eligible_goal_count'],1);self.assertEqual(f['available_eligible_goal_count'],1)


if __name__=='__main__':unittest.main()
