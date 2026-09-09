#!/usr/bin/env python3
import itertools
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import maxima_win_statistics as stats
import maxima_win_experiment as exp
import maxima_experiment_queue as fleet
import maxima_win_report as reporting


class ExactInference(unittest.TestCase):
    def test_zero_disagreement_is_not_certainty(self):
        low,high=stats.exact_interval(100,0,0,.001)
        self.assertLess(low,0); self.assertGreater(high,0)
    def test_labels_reverse_interval(self):
        low,high=stats.exact_interval(1000,45,12,.001)
        revlow,revhigh=stats.exact_interval(1000,12,45,.001)
        self.assertAlmostEqual(low,-revhigh); self.assertAlmostEqual(high,-revlow)
    def test_missing_envelope_contains_every_completion(self):
        for n,b,c,m in ((10,1,2,4),(10,0,0,10),(100,0,1,5)):
            low,high=stats.exact_interval(n,b,c,.001,m)
            for db in range(m+1):
                for dc in range(m-db+1):
                    lo,hi=stats.exact_interval(n,b+db,c+dc,.001)
                    self.assertLessEqual(low,lo+1e-12); self.assertGreaterEqual(high,hi-1e-12)
    def test_exact_small_n_coverage(self):
        from scipy.stats import multinomial
        for q,delta in ((.002,0),(.1,0),(.1,.02),(.7,-.02),(1,0)):
            n=20; misses=0
            for b in range(n+1):
                for c in range(n-b+1):
                    lo,hi=stats.exact_interval(n,b,c,.04)
                    if not lo<=delta<=hi:
                        misses+=multinomial.pmf([b,c,n-b-c],n,[(q+delta)/2,(q-delta)/2,1-q])
            self.assertLessEqual(misses,.04+1e-10)
    def test_rare_wins_and_shared_baseline(self):
        baseline=[0]*99+[1]
        rows=[{'scenario_id':str(i),'on':int(i==0),'off':b} for i,b in enumerate(baseline)]
        self.assertEqual(stats.analyze(rows,.01)['delta'],0)
        self.assertEqual(stats.analyze(rows,.01)['independent_pairs'],100)
        # Another hypothesis may share this baseline, but cannot duplicate pairs within itself.
        self.assertEqual(stats.analyze(rows,.02)['independent_pairs'],100)
        with self.assertRaises(ValueError): stats.analyze(rows+rows,.01)
        with self.assertRaises(ValueError): stats.analyze([{**rows[0],'repeat':True}],.01)
    def test_missing_not_draw_and_decisions(self):
        r=stats.analyze([{'scenario_id':'a','on':None,'off':0}],.01)
        self.assertIsNone(r['delta']); self.assertEqual(r['status'],'unresolved')
        self.assertEqual(stats.conclusion(.001,.019)['direction'],'helpful')
        self.assertTrue(stats.conclusion(.001,.019)['practically_equivalent'])
        self.assertFalse(stats.conclusion(.001,.03)['magnitude_above_two_points'])
        self.assertTrue(stats.conclusion(-.04,-.021)['magnitude_above_two_points'])


class PopulationCutoff(unittest.TestCase):
    protocol={'hard_tick_limit':200000,'cutoff_scoring':'population_v1'}
    def case(self, populations, fmt='ffa3', focal=(0,)):
        scenario={'format':fmt,'players':[{'player':i,'team':2*i,'focal':i in focal} for i in range(len(populations))]}
        receipt={'outcome':None,'terminal':{'tick':200000,'game_ended':False,'players':[
            {'player':i,'team':2*i,'alive':True,'lost':False,'population':n} for i,n in enumerate(populations)]}}
        return scenario,receipt
    def test_ffa_compares_each_opponent_not_their_sum(self):
        s,r=self.case([30,20,20]);v=exp.adjudicate(self.protocol,s,r)
        self.assertEqual(v['outcome'],1);self.assertEqual(v['decision_reason'],'population_win')
        self.assertIsNone(v['natural_outcome'])
    def test_team_population_sums_nonadjacent_allies(self):
        s,r=self.case([12,20,12,3],'2v2',(0,2))
        self.assertEqual(exp.adjudicate(self.protocol,s,r)['outcome'],1)
    def test_equal_leaders_are_draw_even_if_focal_trails(self):
        for populations in ([20,20,1],[1,20,20]):
            s,r=self.case(populations);v=exp.adjudicate(self.protocol,s,r)
            self.assertEqual(v['outcome'],0);self.assertTrue(v['cutoff_draw'])
            self.assertEqual(v['decision_reason'],'population_draw')
    def test_eliminated_team_cannot_win_population_cutoff(self):
        s,r=self.case([20,90,10]);r['terminal']['players'][1]['lost']=True
        self.assertEqual(exp.adjudicate(self.protocol,s,r)['outcome'],1)
    def test_natural_result_precedes_population(self):
        s,r=self.case([1,100,100])
        for outcome in (0,1):
            r['outcome']=outcome;r['terminal']['tick']=1000
            self.assertEqual(exp.adjudicate(self.protocol,s,r)['outcome'],outcome)
    def test_rejects_missing_stale_negative_or_duplicate_population_receipts(self):
        import copy
        s,r=self.case([10,20,30])
        for bad in (None,-1,True,1.5):
            value=copy.deepcopy(r);value['terminal']['players'][0]['population']=bad
            with self.assertRaises(exp.IntegrityError):exp.adjudicate(self.protocol,s,value)
        r['terminal']['tick']=199999
        with self.assertRaises(exp.IntegrityError):exp.adjudicate(self.protocol,s,r)
        r['terminal']['tick']=200000;r['terminal']['players'][1]=r['terminal']['players'][0]
        with self.assertRaises(exp.IntegrityError):exp.adjudicate(self.protocol,s,r)
    def test_legacy_protocol_keeps_unresolved(self):
        s,r=self.case([30,10,20]);self.assertIsNone(exp.adjudicate({},s,r)['outcome'])


class Protocol(unittest.TestCase):
    def setUp(self):
        self.protocol={'protocol_id':'frozen','maps':{f:[{'path':'fixture.map','teams':5 if f=='ffa5' else 4,
                       'sha256':'map'}] for f in exp.FORMATS},
                       'defaults':{f:{'farming.enabled':False,'farming.gate_clearing_enabled':True} for f in exp.FORMATS},
                       'switches':['farming.enabled','farming.gate_clearing_enabled']}
    def test_balanced_controls_and_weak_first(self):
        from collections import Counter
        rows=[exp.scenario(self.protocol,'controls',i,True) for i in range(200)]
        self.assertEqual(set(Counter((r['format'],r['opponent']) for r in rows).values()),{10})
        self.assertEqual({r['opponent'] for r in rows[:100]},{1,2})
        self.assertEqual({r['opponent'] for r in rows[100:]},{5,6})
        self.assertEqual(rows[7],exp.scenario(self.protocol,'controls',7,True))
    def test_full_ai_off_manifest_is_focal_only_and_uses_fresh_seeds(self):
        protocol={**self.protocol,'controls':{'kind':'no_orders','seed_namespace':6},'binary':'glob2','binary_sha256':'binary'}
        manifest=reporting.comparisons(protocol,'controls',{'no_orders':200},[],{exp.NO_ORDERS:True})
        old={exp.scenario(self.protocol,'controls',i,True)['seed'] for i in range(200)}
        for job in manifest['jobs']:
            scenario=job['scenario']; self.assertNotIn(scenario['seed'],old)
            settings=job['settings']
            disabled=[int(k) for k,v in settings.items() if v.get(exp.NO_ORDERS)]
            if disabled:
                self.assertEqual(set(disabled),{p['player'] for p in scenario['players'] if p['focal']})
                args,envelope=exp.command(protocol,scenario,settings,10,'audit.jsonl')
                actual=[int(args[i+1]) for i,x in enumerate(args) if x=='--maxima-no-orders-player']
                self.assertEqual(set(actual),set(disabled))
                self.assertFalse(any(exp.NO_ORDERS in x for x in args))
                self.assertEqual(envelope['configuration'],exp.identity(settings))
        with self.assertRaises(ValueError):
            reporting.comparisons(protocol,'controls',{'no_orders':200},[],{'tactics.enabled':False})
    def test_independent_namespaces_and_uniform_sampling(self):
        seeds=[exp.scenario(self.protocol,s,i)['seed'] for s in exp.NAMESPACES for i in range(100)]
        self.assertEqual(len(seeds),len(set(seeds)))
        rows=[exp.scenario(self.protocol,'pilot',i) for i in range(4000)]
        from collections import Counter
        counts=Counter((r['format'],r['opponent']) for r in rows)
        self.assertEqual(len(counts),20); self.assertTrue(all(140<c<260 for c in counts.values()))
    def test_all_alliance_arrangements_and_isolation(self):
        arrangements=set()
        for i in range(300):
            s=exp.scenario(self.protocol,'pilot',i)
            if s['format']!='2v2': continue
            arrangements.add(tuple(p['player'] for p in s['players'] if p['focal']))
            on=exp.arm_settings(self.protocol,s,'farming.gate_clearing_enabled',True)
            off=exp.arm_settings(self.protocol,s,'farming.gate_clearing_enabled',False)
            self.assertEqual(sum(on[k]!=off[k] for k in on),2)
            self.assertTrue(all(v['farming.enabled'] for v in off.values()))
            for p in s['players']:
                if not p['focal'] and s['opponent']==6: self.assertEqual(on[str(p['player'])],off[str(p['player'])])
        self.assertEqual(len(arrangements),6)
    def test_baseline_cache_identity_is_full_configuration(self):
        a={'farming.enabled':True,'recon.enabled':True}
        self.assertEqual(exp.identity(a),exp.identity(dict(reversed(list(a.items())))))
        self.assertNotEqual(exp.identity(a),exp.identity({**a,'recon.enabled':False}))
    def test_manifest_baseline_reuse_and_pilot_gates(self):
        switches=self.protocol['switches']
        manifest=reporting.comparisons(self.protocol,'pilot',dict.fromkeys(switches,100),switches)
        self.assertEqual(len(manifest['comparisons']),200)
        self.assertLess(len(manifest['jobs']),400)
        self.assertEqual(len({j['execution_id'] for j in manifest['jobs']}),len(manifest['jobs']))
        with self.assertRaises(ValueError): reporting.comparisons(self.protocol,'pilot',{switches[0]:99},switches)
        with self.assertRaises(ValueError): reporting.comparisons(self.protocol,'pilot',{switches[0]:100},[])
    def test_combined_configuration_changes_both_allies_only(self):
        confirmed={'confirmatory':True,'protocol_id':'frozen','results':[{'switch':'farming.enabled','default_change_candidate':True}]}
        manifest=reporting.combined_comparisons(self.protocol,confirmed,100)
        self.assertEqual(manifest['stage'],'combination')
        self.assertEqual(manifest['changes'],{'farming.enabled':False})
        self.assertTrue(all(j['scenario']['stage']=='combination' for j in manifest['jobs']))

    def test_hard_tick_limit_cannot_be_overridden(self):
        with self.assertRaisesRegex(ValueError,'hard tick limit'):
            exp.command({'hard_tick_limit':200000},{},{},200001,'audit.jsonl')

    def test_missing_audit_rejected(self):
        with self.assertRaises(exp.IntegrityError): exp.read_audit('/does/not/exist',{}, {},{})


class Fleet(unittest.TestCase):
    def test_reserve_entire_smt_core(self):
        topology='# CPU,Core,Socket,Online\n0,0,0,Y\n1,1,0,Y\n2,0,0,Y\n3,1,0,Y\n'
        plan=fleet.affinity(topology,3)
        self.assertEqual(plan['jobs'],2)
        self.assertEqual(set(plan['reserved_cpus'])&set(plan['simulation_cpus']),set())
        self.assertEqual(plan['reserved_cpus'],[1,3])
        with self.assertRaises(ValueError): fleet.affinity('0,0,0,Y',3)
    def test_three_failures_three_successes(self):
        h=fleet.Health()
        self.assertTrue(h.observe(False));self.assertTrue(h.observe(False));self.assertFalse(h.observe(False))
        self.assertFalse(h.observe(True));self.assertFalse(h.observe(True));self.assertTrue(h.observe(True))
    def test_persistent_claims_and_independent_transfers(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'queue.sqlite'; q=fleet.Queue(path); key=q.enqueue({'arm':'on'})
            self.assertIsNone(q.claim('host',0))
            for _ in range(3): q.health('host',True,{'simulation_cpus':[0]})
            self.assertEqual(q.claim('host',0)['id'],key)
            q2=fleet.Queue(path);self.assertIsNone(q2.claim('host',1))
            q.finish(key,{'outcome':1}); q.transfer(key,False,'network unavailable')
            self.assertIsNone(q2.claim('host',1));self.assertEqual(q.summary()['pending_transfers'],1)
            q.transfer(key,True);self.assertEqual(q.summary()['pending_transfers'],0)
            q.close();q2.close()


if __name__=='__main__': unittest.main()
