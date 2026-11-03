#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest
import hashlib
import json
import struct
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from tools.resource_claim_physical_screen import screen,physical_plan,Thresholds,prove_no_reachable_alternative


def inputs():
    games=[str(i) for i in range(16)];families=['broad']*6+['protected']*5+['pooled']*5
    costs=np.full((16,4),100.0);costs[:,0]=105
    probes=np.full((16,4),20,dtype=np.uint64);probes[:,0]=10
    status=np.zeros((16,4,3),dtype=np.uint8)
    refs=np.repeat(np.arange(4,dtype=np.uint32)[:,None],8,axis=1)
    return [costs,probes,status,games,families,{'games':dict.fromkeys(games,'discovery'),'strata':dict(zip(games,families))},[0],{3},refs,np.array([10,20,20,20],dtype=np.uint64)]


class PhysicalScreen(unittest.TestCase):
    def structural_inputs(self):
        args=inputs()
        args[0]=np.concatenate((args[0],np.full((1,4),np.nan)))
        args[1]=np.concatenate((args[1],args[1][:1]))
        args[2]=np.concatenate((args[2],np.zeros((1,4,3),dtype=np.uint8)))
        args[2][-1,:,2]=1
        args[3].append('structural');args[4].append('broad')
        args[5]['games']['structural']='discovery';args[5]['strata']['structural']='broad'
        proof={'version':1,'contract':'G2CLMD01-v1-exhaustive-static-dijkstra',
               'game':'structural','event':'event','record_sha256':'a'*64,
               'manifest_sha256':'b'*64,'oracle_expanded':16,
               'provenance':{'censored':False,'metadata_mode':'published-xy','source_revision':'source','bundle_id':'bundle','cohort_id':'cohort'}}
        return args,proof

    def test_event_wide_na_retains_full_failure_guardrails_and_applicable_minima(self):
        args,proof=self.structural_inputs()
        options={'coverage_ready':True,'coverage_evidence':{'ready_for_reversible_allocation':True},'structural_na':[proof]}
        result=screen(*args,**options)
        self.assertEqual([x['candidate'] for x in result['eliminated']],[1,2])
        self.assertEqual(len(result['contract']['games']),17)
        self.assertEqual(len(result['contract']['travel_applicable_games']),16)
        self.assertEqual(result['contract']['travel_family_games'],{'broad':6,'protected':5,'pooled':5})
        args[2][-1,1,0]=1
        self.assertNotIn(1,[x['candidate'] for x in screen(*args,**options)['eliminated']])
        args[0][0,2]=np.nan
        self.assertNotIn(2,[x['candidate'] for x in screen(*args,**options)['eliminated']])
        args,proof=self.structural_inputs()
        # Full counts satisfy the gate; travel-applicable pooled games do not.
        args[4][11]='broad';args[4][-1]='pooled'
        args[5]['strata']['11']='broad';args[5]['strata']['structural']='pooled'
        self.assertFalse(screen(*args,**options)['coverage_ready'])

    def test_na_proof_cannot_exclude_candidate_specific_unknowns_or_duplicate_games(self):
        args,proof=self.structural_inputs()
        with self.assertRaisesRegex(ValueError,'duplicates'):
            screen(*args,structural_na=[proof,proof])
        args[0][-1,0]=0
        with self.assertRaisesRegex(ValueError,'contradicts'):screen(*args,structural_na=[proof])
        for change in ({'game':'other'},{'record_sha256':'wrong'},{'oracle_expanded':0},{'provenance':{'censored':True}},
                       {'version':True},{'event':1},{'provenance':None}):
            with self.assertRaisesRegex(ValueError,'invalid structural'):screen(*inputs(),structural_na=[proof|change])

    def test_exhaustive_na_proof_is_bound_to_validated_capture_and_rejects_reachable_stock(self):
        from tools.tournaments.claim_capture import HEADER,CANDIDATE,MAGIC,DESIGN
        missing=(1<<64)-1
        provenance={'source_revision':'source','bundle_id':'bundle','cohort_id':'cohort','metadata_mode':'published-xy','censored':False}
        metadata={'width':2,'height':2,'origin':0,'nearest':1,'game_id':'structural','event_id':'event',
                  'ordinal':1,'priority':2,'optimum':None,'optimal':None,'provenance':provenance,
                  'destination_lookup':'published-xy','destination_xy_missing':False,'oracle_expanded':3,'source_count':1}
        with tempfile.TemporaryDirectory() as directory:
            record=Path(directory)/'event.bin';manifest=Path(directory)/'manifest.json'
            def write(distance,flags=3):
                encoded=json.dumps(metadata).encode()
                data=HEADER.pack(MAGIC,1,len(encoded),4,1)+encoded+struct.pack('<4I',1,1,1,1)+CANDIDATE.pack(1,0xffffffff,flags,0,distance,missing)
                data+=hashlib.sha256(data).digest();record.write_bytes(data)
                manifest.write_text(json.dumps({'version':1,'complete':True,'censored':False,'design':DESIGN,'eligible':1,
                    'ordinal':1,'captured_ordinal':1,'replacements':1,'excluded_eligible_admission':0,
                    'excluded_eligible_sample_cap':0,'excluded_eligible_queue_cap':0,'priority':2,'file_sha256':hashlib.sha256(data).hexdigest()}))
            write(missing)
            proof=prove_no_reachable_alternative(record,manifest,expected_provenance=provenance)
            self.assertEqual(proof['record_sha256'],hashlib.sha256(record.read_bytes()).hexdigest())
            write(3)
            with self.assertRaisesRegex(ValueError,'exhaustive absence'):
                prove_no_reachable_alternative(record,manifest,expected_provenance=provenance)
            # Capacity alone is insufficient: this reachable stock cannot be
            # legally harvested, so it is not an alternative destination.
            write(3,flags=2)
            prove_no_reachable_alternative(record,manifest,expected_provenance=provenance)
            write(missing);record.write_bytes(record.read_bytes()[:-1])
            with self.assertRaises(ValueError):prove_no_reachable_alternative(record,manifest,expected_provenance=provenance)

    def test_tradeoff_proposes_real_physical_reduction_and_protects_backstop(self):
        args=inputs();r=screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True},thresholds=Thresholds(audit_percent=0))
        self.assertEqual([x['candidate'] for x in r['eliminated']],[1,2])
        self.assertEqual(r['physical_work']['omitted_arrays'],[1,2])
        self.assertEqual(r['physical_work']['physical_probe_entries_after'],30)
        self.assertFalse(r['applied']);self.assertIsNone(r['physical_work']['measured_cpu_seconds'])

    def test_shared_alias_or_other_policy_keeps_physical_array(self):
        args=inputs();args[8][3]=args[8][1]
        r=screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True},thresholds=Thresholds(audit_percent=0))
        self.assertEqual(r['physical_work']['omitted_arrays'],[2])
        p=physical_plan(args[8],[1,2],args[9],audit_percent=100)
        self.assertEqual(p['omitted_arrays'],[]);self.assertEqual(p['audit_arrays'],[2])

    def test_guardrails_missing_and_adverse_family_never_become_zero(self):
        args=inputs();args[0][0,1]=np.nan;args[2][0,1,0]=1
        args[0][11:,2]=50
        r=screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True})
        self.assertEqual(r['eliminated'],[])
        args=inputs();args[2][0,1,1]=1
        self.assertNotIn(1,[x['candidate'] for x in screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True})['eliminated']])

    def test_paired_uncertainty_rejects_noisy_boundary_even_when_means_pass(self):
        args=inputs();args[0][:,0]=100;args[0][:,1]=100/1.1
        args[0][0,0]=140;args[0][1,0]=60
        r=screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True})
        self.assertNotIn(1,[x['candidate'] for x in r['eliminated']])
        self.assertEqual(r,screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True}))

    def test_literal_quarter_probe_saving_and_no_travel_witness_search(self):
        args=inputs();args[1][:,1]=13
        r=screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True})
        self.assertNotIn(1,[x['candidate'] for x in r['eliminated']])
        args=inputs();args[6]=[0,2];args[1][:,2]=11;args[0][:,2]=100;args[0][11:,0]=120
        r=screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True})
        self.assertNotIn(1,[x['candidate'] for x in r['eliminated']])

    def test_coverage_contract_cannot_weaken_or_omit_strata(self):
        args=inputs()
        with self.assertRaisesRegex(ValueError,'coverage evidence'):screen(*args,coverage_ready=True)
        with self.assertRaisesRegex(ValueError,'cannot be weakened'):screen(*args,thresholds=Thresholds(minimum_games=1))
        del args[5]['strata']['0']
        with self.assertRaisesRegex(ValueError,'family'):screen(*args)

    def test_gates_holdout_and_memory_budget(self):
        args=inputs();self.assertEqual(screen(*args)['eliminated'],[])
        args[5]['games']['0']='holdout'
        with self.assertRaisesRegex(ValueError,'holdout'):screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True})
        args=inputs()
        with self.assertRaisesRegex(ValueError,'budget'):screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True},thresholds=Thresholds(maximum_working_bytes=1))
        args[4]=['broad']*16;args[5]['strata']=dict.fromkeys(args[3],'broad');args[5]['strata']['future']='pooled'
        self.assertFalse(screen(*args,coverage_ready=True,coverage_evidence={'ready_for_reversible_allocation':True})['coverage_ready'])


if __name__=='__main__':unittest.main()
