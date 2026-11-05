#!/usr/bin/env python3
"""Independent offline kernel contracts; native deep-freeze parity remains gated."""
import dataclasses
import json
import random
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.resource_claim_patterns import Pattern,PROBE,FAMILIES,encode,specifications
from tools.resource_claim_offline import (Candidate,FrozenEvent,Outcome,Scorer,NO_TILE,UNREACHABLE,
    write_catalog,catalog_arrays,logical_patterns,score_game,freeze_split,write_scores,alias_views,score_logical_game,deep_budget,GameAggregates)


def event(nearest=6):
    destinations=[NO_TILE]*64
    for tile in range(64):destinations[tile]=(6,9,27)[tile%3]
    return FrozenEvent(8,8,0,nearest,destinations,[Candidate(6,component=1,available=False,distance=8,access_distance=12),
        Candidate(9,component=1,distance=8,access_distance=20),Candidate(27,distance=UNREACHABLE,access_distance=UNREACHABLE)],
        game_id='seed-game1',event_id='tick1-owner1',optimum=8,provenance={'source_revision':'a','bundle_id':'b',
        'cohort_id':'c','metadata_mode':'published-xy','censored':False})


def reference(e,data,first):
    # Deliberately scalar, without using the production vector kernel or source plane.
    result=Outcome();best=None;probes=0
    for x,y,origin in PROBE.iter_unpack(data):
        start=e.origins[origin]
        if start==NO_TILE:continue
        tile=((start//e.width+y)%e.height)*e.width+(start%e.width+x)%e.width
        probes+=1;access=int(e.destinations[tile]);candidate=e.candidates.get(access)
        if candidate is None or not candidate.access_eligible:continue
        possible=[c for c in e.candidates.values() if c.available and c.component==candidate.component] if candidate.component!=NO_TILE else [candidate] if candidate.available else []
        if not possible:continue
        source=min(possible,key=lambda c:c.source)
        distance=e.geometry(access)
        if best is None or distance<best:
            best=distance;result=Outcome(source.source,access,tile,source.distance,candidate.access_distance,probes,False)
            if first:return result
    return dataclasses.replace(result,probes=probes)


class OfflineClaims(unittest.TestCase):
    def test_large_rectangular_wrap_and_unlabeled_probes_match_scalar_ranking(self):
        width,height=512,256
        tiles=[0,1,width-1,width*(height-1),width*height-1,17*width+255]
        destinations=[NO_TILE]*(width*height)
        for tile in range(0,len(destinations),3):destinations[tile]=tiles[tile%len(tiles)]
        e=FrozenEvent(width,height,width*height-2,tiles[3],destinations,
            [Candidate(tile,distance=10000,access_distance=10000) for tile in tiles],
            game_id='wrap',event_id='one',provenance=event().provenance,optimum=10000)
        rng=random.Random(491)
        arrays=[b''.join(PROBE.pack(rng.randrange(-1024,1025),rng.randrange(-1024,1025),rng.randrange(3))
                         for _ in range(n)) for n in (0,1,17,257,1024)]
        scorer=Scorer(e,prefix_entries=16,cache_bytes=8192)
        expected=[(reference(e,data,True),reference(e,data,False)) for data in arrays]
        self.assertEqual(scorer.evaluate_many(arrays),expected)
        self.assertEqual([scorer.evaluate(data) for data in arrays],expected)

    def test_all_240_pilot_profiles_match_independent_scalar_reference(self):
        profiles=(('unit',1,'every','forward',0),('resource',2,'every','reverse',1),
            ('midpoint',4,'random_block','shuffle',2),('interleaved',1,'every','forward',4),
            ('unit',4,'every','forward',6),('resource',1,'every','forward',7))
        for nearest in (2,18,16,22,6,54,48,50):
            e=event(nearest);scorer=Scorer(e,cache_bytes=8192);arrays=[];expected=[]
            for family in FAMILIES:
                for radius in (1,2,4,8,16):
                    for origin,stride,subset,order,direction in profiles:
                        d=(direction+e.octant())%8 if family.startswith('cone_') or family=='rays_toward' else 0
                        data=encode(Pattern(family,radius,origin,stride,subset,order,d))
                        outcomes=(reference(e,data,True),reference(e,data,False))
                        self.assertEqual(scorer.evaluate(data),outcomes)
                        arrays.append(data);expected.append(outcomes)
            self.assertLessEqual(scorer.used,8192)
            self.assertEqual(scorer.evaluate_many(arrays),expected)

    def test_segmented_batches_match_scalar_kernel_for_empty_and_wrapped_patterns(self):
        for nearest in (6,NO_TILE):
            e=event(nearest)
            arrays=[b'',PROBE.pack(0,0,2)]+[encode(s) for s in specifications(families=['spiral','cone_toward'],
                radii=[0,1,2,4],origins=['unit','interleaved'],strides=[1,4],directions=[0])]
            scorer=Scorer(e)
            self.assertEqual(scorer.evaluate_many(arrays),[scorer.evaluate(a) for a in arrays])
            with self.assertRaisesRegex(ValueError,'oversized'):scorer.evaluate_many(arrays,max_entries=1)

    def test_wrapped_midpoint_unknown_origins_and_unreachable_are_preserved(self):
        e=event(5);self.assertEqual(e.midpoint(),7) # -3/2 truncates to -1.
        data=PROBE.pack(2,0,0)
        result=Scorer(e).evaluate(data)[0]
        self.assertEqual(result.source,27);self.assertFalse(result.fallback)
        self.assertEqual(result.distance,UNREACHABLE)
        e=event(NO_TILE);data=PROBE.pack(0,0,1)+PROBE.pack(1,0,2)
        self.assertEqual(Scorer(e).evaluate(data),(Outcome(),Outcome()))

    def test_catalog_shards_deduplicate_and_refuse_incomplete_or_modified_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'catalog'
            specs=list(specifications(families=['spiral','random'],radii=[0,1,2],origins=['unit'],strides=[1],orders=['forward'],directions=[0]))
            summary=write_catalog(p,specs,shard_bytes=80,max_bytes=65536,reserve_bytes=0)
            self.assertTrue(summary['complete']);self.assertLess(summary['arrays'],summary['aliases'])
            arrays=list(catalog_arrays(p));self.assertEqual(len(arrays),summary['arrays'])
            self.assertTrue(all(f.stat().st_size<=80 for f in p.glob('*.bin')))
            self.assertEqual(len(list(logical_patterns(p))),len(specs))
            next(p.glob('*.bin')).write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError,'mismatch'):list(catalog_arrays(p))
            limited=Path(temp)/'limited'
            with self.assertRaisesRegex(ValueError,'alias limit'):write_catalog(limited,specs,max_aliases=1,reserve_bytes=0)
            with self.assertRaisesRegex(ValueError,'incomplete'):list(catalog_arrays(limited))

    def test_directional_alias_uses_each_events_nearest_bearing(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'catalog'
            specs=list(specifications(families=['cone_toward'],radii=[2],origins=['unit'],strides=[1],orders=['forward']))
            write_catalog(p,specs,reserve_bytes=0)
            _,views=next(logical_patterns(p));self.assertEqual(len(views),8)
            e=event(6)
            self.assertEqual(views[e.octant()],encode(Pattern('cone_toward',2,direction=4)))

    def test_logical_bearing_groups_and_global_game_means_remain_exact(self):
        import numpy as np
        with tempfile.TemporaryDirectory() as temp:
            catalog=Path(temp)/'catalog'
            specs=list(specifications(families=['cone_toward'],radii=[1,2],origins=['unit'],strides=[1],orders=['forward']))
            summary=write_catalog(catalog,specs,reserve_bytes=0)
            a=event(6);b=event(2);b.event_id='second'
            matrix=score_logical_game([a,b],catalog)
            naive=list(score_game([a,b],logical_patterns(catalog)))
            for index,row in enumerate(naive):
                for policy,metrics in enumerate(row['policies']):
                    for key,value in metrics.items():self.assertAlmostEqual(matrix[key][index,policy],value)
            split=freeze_split([a.game_id,'unobserved'],source_identity='s',cohort_identity='c')
            aggregate=GameAggregates(len(specs),split,catalog_identity=summary['identity'],provenance_identity='p')
            aggregate.add(a.game_id,matrix,provenance_identity='p')
            with self.assertRaisesRegex(ValueError,'duplicate'):aggregate.add(a.game_id,matrix,provenance_identity='p')
            report=aggregate.write(Path(temp)/'aggregate',reserve_bytes=0)
            self.assertEqual(report['missing_games'],['unobserved'])
            self.assertIsNone(report['confidence_intervals']);self.assertFalse(report['policy_selection_allowed'])
            with np.load(Path(temp)/'aggregate/aggregates.npz') as data:
                self.assertEqual(int(data['games'].sum()),int(aggregate.counts.sum()))

    def test_total_deep_allowance_reserves_catalog_inputs_and_aggregates(self):
        budget=deep_budget(catalog_bytes=1500000000,free_bytes=32*1024**3)
        self.assertEqual(budget['max_events'],1000)
        self.assertGreaterEqual(budget['freeze_bytes'],1000*budget['max_event_bytes'])
        with self.assertRaisesRegex(ValueError,'frozen-input'):deep_budget(catalog_bytes=1,events=1001,freeze_bytes=1,free_bytes=32*1024**3)
        with self.assertRaisesRegex(ValueError,'total allowance'):deep_budget(catalog_bytes=1,free_bytes=1)

    def test_score_output_budget_never_publishes_partial_as_complete(self):
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/'scores.gz'
            info=write_scores(output,[{'game':'one','value':3}],reserve_bytes=0)
            self.assertEqual(info['rows'],1)
            with self.assertRaisesRegex(ValueError,'exists'):write_scores(output,[],reserve_bytes=0)
            failed=Path(temp)/'failed.gz'
            with self.assertRaisesRegex(ValueError,'budget'):write_scores(failed,[{'value':'long'}],max_raw_bytes=1,reserve_bytes=0)
            self.assertFalse(failed.exists());self.assertTrue(Path(str(failed)+'.partial').exists())

    def test_streaming_game_sums_missing_cost_and_game_split(self):
        e=event();arrays=[('unreachable',PROBE.pack(2,0,0)),('available',PROBE.pack(0,0,0))]
        rows=list(score_game([e],arrays))
        self.assertEqual(rows[0]['policies'][0]['compared'],0)
        self.assertEqual(rows[0]['policies'][0]['unreachable'],1)
        self.assertEqual(rows[1]['policies'][0]['regret'],0)
        self.assertEqual(rows[1]['policies'][0]['ratio_sum'],1)
        self.assertEqual(rows[1]['policies'][0]['access_ratio_sum'],1.5)
        e.optimum=0
        zero=list(score_game([e],arrays))[1]['policies'][0]
        self.assertEqual(zero['zero_optimum_positive_cost'],1);self.assertEqual(zero['ratio_count'],0)
        self.assertEqual(zero['zero_optimum_positive_access_cost'],1);self.assertEqual(zero['access_ratio_count'],0)
        with self.assertRaisesRegex(ValueError,'memory budget'):list(score_game([e],arrays,max_event_bytes=1))
        with self.assertRaisesRegex(ValueError,'unique events'):list(score_game([e,e],arrays))
        split=freeze_split(['game2','game1'],source_identity='src',cohort_identity='cohort')
        self.assertEqual(split,freeze_split(['game1','game2'],source_identity='src',cohort_identity='cohort'))
        self.assertEqual(split['bootstrap_unit'],'game');self.assertFalse(split['policy_selection_allowed'])
        stratified=freeze_split(['a','b','c','d'],source_identity='s',cohort_identity='c',strata={'a':'x','b':'x','c':'y','d':'y'})
        self.assertEqual(sum(v=='holdout' for v in stratified['games'].values()),2)
        with self.assertRaises(ValueError):freeze_split(['same','same'],source_identity='s',cohort_identity='c')


if __name__=='__main__':unittest.main()
