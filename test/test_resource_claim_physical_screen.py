#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from tools.resource_claim_physical_screen import screen,physical_plan,Thresholds


def inputs():
    games=[str(i) for i in range(16)];families=['broad']*6+['protected']*5+['pooled']*5
    costs=np.full((16,4),100.0);costs[:,0]=105
    probes=np.full((16,4),20,dtype=np.uint64);probes[:,0]=10
    status=np.zeros((16,4,3),dtype=np.uint8)
    refs=np.repeat(np.arange(4,dtype=np.uint32)[:,None],8,axis=1)
    return [costs,probes,status,games,families,{'games':dict.fromkeys(games,'discovery'),'strata':dict(zip(games,families))},[0],{3},refs,np.array([10,20,20,20],dtype=np.uint64)]


class PhysicalScreen(unittest.TestCase):
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
