"""Exact gameplay windows, missingness and independently clustered paired inference."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments import resource_claim_gameplay as g
from test_resource_claim_policy import policy
from tools.tournaments.claim_policy import policy_digest


def frame(tick, scale=1):
    f = dict(tick=tick, coverage_start=0, extended_coverage_start=0, labour_coverage_start=0)
    for i in range(11): f[f'labour_{i}']=tick*scale
    for j in range(4):
        for p in range(4): f[f'filling_{j}_{p}']=tick*scale
    for flow in g.FLOWS:
        for m in range(2): f[f'{flow}_{m}']=tick*scale
    f['deaths_0_1']=tick//10
    return f


def design(state='state', policy='candidate', block='block'):
    return dict(cohort='cohort', partition='discovery', initial_state_identity=state,
                pair_block=block, policy_digest=policy, teams=[0], window_start=0, window_end=10)


class Gameplay(unittest.TestCase):
    def test_exact_flows_and_transport_denominators(self):
        counters=g.measure_frames(frame(0),frame(10),0,10)
        m=g.endpoint_metrics(counters,10)
        self.assertEqual(m['transport_ticks'],270)
        self.assertEqual(m['pursuit_ticks_per_delivery'],2)
        self.assertEqual(m['mean_transport_population'],27)
        self.assertEqual(m['starvation_deaths'],1)
        self.assertEqual(m['withdrawn_1'],10)
        self.assertNotIn('wasted_movement',m)

    def test_missing_not_zero_or_partial_coverage(self):
        for key in ['delivered_0','labour_3','deaths_0_1','filling_1_0']:
            f=frame(10);del f[key]
            with self.assertRaises(ValueError):g.measure_frames(frame(0),f,0,10)
        f=frame(10);f['labour_coverage_start']=5
        with self.assertRaises(ValueError):g.measure_frames(frame(0),f,0,10)
        f=frame(10);f['delivered_0']=-1
        with self.assertRaises(ValueError):g.measure_frames(frame(0),f,0,10)
        with self.assertRaises(ValueError):g.measure_frames(frame(0),frame(9),0,10)
        z=g.measure_frames(frame(0),frame(10,0),0,10)
        self.assertIsNone(g.endpoint_metrics(z,10)['pursuit_ticks_per_delivery'])
        self.assertIsNone(g.endpoint_metrics(z,10)['delivered_per_1000_transport_ticks'])

    def test_rotation_blocks_and_policy_partitions_never_pool(self):
        games=[]
        for block in range(5):
            for rotation in range(2):
                for policy,value in [('base',1),('candidate',block+2),('other',20)]:
                    d=design(f'{block}-{rotation}',policy,str(block))
                    games.append(dict(game=f'{block}-{rotation}-{policy}',design=d,metrics={'delivered':value}))
        report,missing=g.paired_report(games,'base',draws=100,seed=4)
        self.assertFalse(missing);self.assertEqual(len(report),2)
        candidate=next(r for r in report if r['policy_digest']=='candidate')
        self.assertEqual(candidate['paired_games'],10);self.assertEqual(candidate['independent_blocks'],5)
        self.assertEqual(candidate['mean_delta'],3);self.assertIsNotNone(candidate['ci95'])
        self.assertEqual(g.paired_report(games,'base',draws=100,seed=4)[0],report)
        games[0]['metrics']['delivered']=None
        report,_=g.paired_report(games,'base',draws=10)
        self.assertEqual(report[0]['missing_denominator_pairs'],1)

    def test_design_rejects_leakage_duplicates_and_pair_mismatch(self):
        d={'version':1,'source_experiment':'exp','games':{'a':design(policy='base'),'b':design()}}
        g.validate_design(d)
        bad=copy.deepcopy(d);bad['games']['b']['partition']='holdout'
        with self.assertRaises(ValueError):g.validate_design(bad)
        bad=copy.deepcopy(d);bad['games']['b']['policy_digest']='base'
        with self.assertRaises(ValueError):g.validate_design(bad)
        a={'game':'a','design':d['games']['a'],'metrics':{'x':1}}
        b={'game':'b','design':d['games']['b']|{'teams':[1]},'metrics':{'x':2}}
        with self.assertRaises(ValueError):g.paired_report([a,b],'base')
        report,missing=g.paired_report([b],'base')
        self.assertFalse(report);self.assertEqual(len(missing),1)

    def test_attested_actual_game_and_exact_boundary_duplicates(self):
        p=policy();job={'config':{'resource_claims_policy':p},'inputs':{'map':'sha'},'seeds':{'game':4}}
        d=design(g.initial_state_identity(job),policy_digest(p))
        result={'ticks':10,'resolved':{'active_claim_policy':'experimental','material_destinations_enabled':True,
            'resource_claims_policy_config':p,'resource_claims_policy_digest':policy_digest(p)},'winning_teams':[0]}
        record={'category':'success','job':job,'result':result}
        class Source:
            def telemetry(self, record):
                for t in (0,10,10):yield {'family':'gameplay','values':frame(t)|{'team':0,'final':int(t==10)}}
        row=g.read_game(Source(),record,d)
        self.assertEqual(row['actual_outcome']['winning_teams'],[0])
        self.assertEqual(row['metrics']['delivered'],20)
        changed=copy.deepcopy(record);changed['job']['seeds']['game']=5
        with self.assertRaises(ValueError):g.read_game(Source(),changed,d)
        changed=copy.deepcopy(record);changed['result']['resolved']['resource_claims_policy_digest']='wrong'
        with self.assertRaises(ValueError):g.read_game(Source(),changed,d)
        changed=copy.deepcopy(record);changed['result']['ticks']=9
        with self.assertRaises(ValueError):g.read_game(Source(),changed,d)

    def test_mode_identity_and_real_verified_bounded_results(self):
        disabled = {'active_claim_policy':'disabled','material_destinations_enabled':True}
        self.assertNotEqual(g.resolved_policy_identity(disabled),g.resolved_policy_identity(disabled|{'material_destinations_enabled':False}))
        with self.assertRaises(ValueError):g.resolved_policy_identity({})
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'artifacts').mkdir();(root/'results').mkdir()
            job={'id':'a','type':'game','build':'build','config':{},'inputs':{},'seeds':{}}
            (root/'experiment.json').write_text(json.dumps({'id':'exp','jobs':[job]}))
            payload=''.join('GLOB2_MEASURE '+' '.join(f'{k}={v}' for k,v in (frame(t)|{'team':0,'final':int(t==10)}).items())+'\n' for t in (0,10))
            packed=gzip.compress(payload.encode());identity=hashlib.sha256(packed).hexdigest()
            path=root/'artifacts'/identity;path.write_bytes(packed)
            record={'id':'attempt','job':job,'category':'success','host':'host','artifacts':[{'path':'stdout.log','sha256':identity,'bytes':len(packed),'encoding':'gzip'}],
                    'result':{'ticks':10,'resolved':disabled,'winning_teams':[0]}}
            (root/'results/a.json').write_text(json.dumps(record))
            d={'version':1,'source_experiment':'exp','games':{'a':design(g.initial_state_identity(job),g.resolved_policy_identity(disabled))}}
            report=g.analyze(root,d,g.resolved_policy_identity(disabled),draws=10)
            self.assertFalse(report['missing']);self.assertEqual(report['games'][0]['metrics']['delivered'],20)
            with patch.object(g,'MAX_LINE_BYTES',32):
                report=g.analyze(root,d,g.resolved_policy_identity(disabled),draws=10)
                self.assertIn('allowance',report['missing'][0]['reason'])
            with patch.object(g,'MAX_LOG_BYTES',len(packed)+10):
                report=g.analyze(root,d,g.resolved_policy_identity(disabled),draws=10)
                self.assertIn('allowance',report['missing'][0]['reason'])
            path.write_bytes(b'corrupted')
            report=g.analyze(root,d,g.resolved_policy_identity(disabled),draws=10)
            self.assertIn('checksum mismatch',report['missing'][0]['reason'])
            d['source_experiment']='other'
            with self.assertRaises(ValueError):g.analyze(root,d,'base')

    def test_auto_windows_pair_all_policies_by_coverage_not_endpoint_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'artifacts').mkdir();(root/'results').mkdir()
            jobs=[]
            for i,end in enumerate((20,10)):
                p=policy();p['fallback']='wait' if i==0 else 'roam';pid=policy_digest(p)
                job={'id':str(i),'type':'game','build':'build','config':{'players':['cortex'],'ticks':20,'resource_claims_policy':p},'inputs':{},'seeds':{'game':1},
                     'labels':{'initial_state_identity':'frozen-state','discovery_split':'holdout','block':'rotation-block','policy_digest':pid}}
                jobs.append(job)
                payload=''.join('GLOB2_MEASURE '+' '.join(f'{k}={v}' for k,v in (frame(t,100 if i else 1)|{'team':0,'final':int(t==end)}).items())+'\n' for t in range(0,end+1,10))
                packed=gzip.compress(payload.encode());identity=hashlib.sha256(packed).hexdigest();(root/'artifacts'/identity).write_bytes(packed)
                record={'id':str(i),'job':job,'category':'success','host':'host','artifacts':[{'path':'stdout.log','sha256':identity,'bytes':len(packed),'encoding':'gzip'}],
                        'result':{'ticks':end,'resolved':{'active_claim_policy':'experimental','material_destinations_enabled':True,'resource_claims_policy_config':p,'resource_claims_policy_digest':pid},'winning_teams':[0]}}
                (root/'results'/f'{i}.json').write_text(json.dumps(record))
            (root/'experiment.json').write_text(json.dumps({'id':'live-exp','labels':{'source_cohort':'frozen-cohort'},'jobs':jobs}))
            built=g.build_design(root)
            self.assertFalse(built['excluded']);self.assertEqual(len(built['design']['games']),2)
            self.assertEqual({r['window_start'] for r in built['design']['games'].values()},{0})
            self.assertEqual({r['window_end'] for r in built['design']['games'].values()},{10})
            self.assertEqual({r['design']['window_end'] for r in built['whole_observed_games']},{10,20})
            baseline=jobs[0]['labels']['policy_digest'];report=g.analyze(root,built['design'],baseline,draws=10)
            self.assertFalse(report['missing']);self.assertEqual(report['comparisons'][0]['partition'],'holdout')
            self.assertIsNone(report['comparisons'][0]['ci95'])
            (root/'results/1.json').unlink()
            missing=g.build_design(root)
            self.assertFalse(missing['design']['games']);self.assertEqual(len(missing['whole_observed_games']),1)
            self.assertTrue(any('pair incomplete' in r['reason'] for r in missing['excluded']))


if __name__=='__main__':unittest.main()
