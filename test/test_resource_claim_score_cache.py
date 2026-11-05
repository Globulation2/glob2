#!/usr/bin/env python3
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.resource_claim_offline import Candidate,FrozenEvent,write_catalog,alias_views
from tools.resource_claim_patterns import specifications
from tools.resource_claim_score_cache import score_cached


class ScoreCache(unittest.TestCase):
    def test_exact_reuse_corruption_and_bounded_allocation(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);catalog=root/'catalog';cache=root/'cache';wire=root/'wire.bin';wire.write_bytes(b'synthetic immutable event fixture')
            write_catalog(catalog,list(specifications(families=['spiral'],radii=[0,1],origins=['unit'],strides=[1],orders=['forward'],directions=[0])),reserve_bytes=0)
            views=alias_views(catalog)
            event=FrozenEvent(8,8,0,6,[6]*64,[Candidate(6,distance=4,access_distance=4)],game_id='g',event_id='e',optimum=4,
                provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c','metadata_mode':'published-xy','censored':False})
            with self.assertRaisesRegex(ValueError,'allowance'):score_cached(event,catalog,views,wire,cache,max_bytes=1)
            values,hit,key=score_cached(event,catalog,views,wire,cache);self.assertFalse(hit)
            with patch('tools.resource_claim_score_cache.score_logical_game',side_effect=AssertionError('must reuse')):
                restored,hit,key2=score_cached(event,catalog,views,wire,cache)
            self.assertTrue(hit);self.assertEqual(key,key2);self.assertTrue((values==restored).all())
            import numpy as np
            with self.assertRaisesRegex(ValueError,'view matrix'):
                score_cached(event,catalog,views[:,:1],wire,cache)
            with self.assertRaisesRegex(ValueError,'view matrix'):
                score_cached(event,catalog,views.astype(np.int32),wire,cache)
            changed=views.copy();changed[0,:]=changed[-1,:]
            with patch('tools.resource_claim_score_cache.score_logical_game',side_effect=RuntimeError('different effective inputs')):
                with self.assertRaisesRegex(RuntimeError,'different effective inputs'):
                    score_cached(event,catalog,changed,wire,cache)
            event.optimum=3
            with patch('tools.resource_claim_score_cache.score_logical_game',side_effect=RuntimeError('different decoded event')):
                with self.assertRaisesRegex(RuntimeError,'different decoded event'):
                    score_cached(event,catalog,views,wire,cache)
            event.optimum=4
            binary=cache/(key+'.bin');data=bytearray(binary.read_bytes());data[-1]^=1;binary.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'attestation'):score_cached(event,catalog,views,wire,cache)


if __name__=='__main__':unittest.main()
