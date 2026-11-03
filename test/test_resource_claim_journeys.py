#!/usr/bin/env python3
import copy
import gzip
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.tournaments.claim_journeys import EVENTS, MOVEMENT_KINDS, attest_digests
from tools.tournaments.resource_claims import COLUMNS, trace_rows, read_game
from tools.tournaments.jobs import EngineJob
from tools.tournaments.claim_policy import policy_digest
from tools.tournaments.transport import worker_archive


def fixture():
    policy={'source_version':1,'pattern':'spiral:k1','pattern_digest':'a'*64,'selection':'first_available',
        'fallback':'wait','fallback_after_decisions':16,'destination_refresh_j':0,'route_refresh_j':0,
        'route_target':'claimed_access','pending_step':'wait','maximum_expansions':4096,'use_primary_gradient':False}
    schema={'kind':'schema','version':1,'patterns':['one'],'policies':['first_available'],
        'outcome_columns':list(COLUMNS),'sample_every':1,'max_samples':32,'max_output_bytes':1048576,
        'live_journey_version':1,'live_event_names':list(EVENTS)}
    events=[0]*len(EVENTS);events[0]=1
    row={'kind':'journey','schema_version':1,'owner':65536,'task':1,'acquired_tick':5,'end_tick':10,
        'origin':0,'source':8,'access':8,'material':1,'terminal':'end_of_segment','steps':2,'live_cost':20,
        'events':events,'policy_digest':policy_digest(policy),'baseline_status':'reached','baseline_cost':10,
        'baseline_expansions':10,'baseline_elapsed_ns':200}
    live={'version':1,'journey_eligible':1,'journey_sampled':1,'journey_admitted':1,'journey_completed':1,
        'excluded_capacity':0,'excluded_limit':0,'actual_moves':2,'actual_weighted_cost':20,'untracked_claim_moves':0,
        'sample_every':1,'max_active':8,'max_admissions':32,'timeout_ticks':65536,'event_counts':events,
        'sampling_censored':False,'admission_uncensored':True,'censored_journey_outcomes':1,
        'eligible_by_phase':[1]+[0]*15,'sampled_by_phase':[1]+[0]*15,'admitted_by_phase':[1]+[0]*15,
        'excluded_by_phase':[0]*16}
    footer={'kind':'summary','complete':True,'submitted':0,'completed':0,'live':live}
    resolved={'active_claim_policy':'experimental','material_destinations_enabled':True,
        'resource_claims_policy_config':policy,'resource_claims_policy_digest':policy_digest(policy)}
    result={'schema_version':1,'status':'completed','resolved':resolved,
        'resource_claims_shadow':{'schema_version':1,'enforced':True,'submitted':0,'completed':0}}
    job={'config':{'resource_claims_policy':policy,'material_destinations':True,
        'resource_claims_probe':{'sample_every':1,'max_samples':32,'max_bytes':1048576}}}
    return [schema,row,footer],job,result


def stream(rows):return io.StringIO(''.join(json.dumps(r)+'\n' for r in rows))


class Journeys(unittest.TestCase):
    def test_required_movement_capture_rejects_old_or_unknown_coverage(self):
        rows,job,result=fixture();job['labels']={'require_movement_audit':True}
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);out=root/'output';out.mkdir();(out/'result.json').write_text(json.dumps(result))
            def capture():
                with gzip.open(out/'resource-claims-shadow.jsonl.gz','wt') as f:f.write(stream(rows).getvalue())
            capture()
            with self.assertRaisesRegex(ValueError,'required movement audit'):EngineJob('game').collect(job,root)
            rows[0]['movement_audit_version']=1
            rows[-1]['movement_audit']={'version':1,'scope':'all-transport-committed-grid-moves','kind_names':list(MOVEMENT_KINDS),
                'coverage_start':5,'coverage_end':10,'coverage_end_exclusive':True,'coverage_known':True,
                'moves':[0,2,0,0,0,0],'weighted_cost':[0,20,0,0,0,0],'total_moves':2,'total_weighted_cost':20,
                'excluded_non_neighbor_moves':0,'unclaimed_source_separation_available':False}
            capture();self.assertEqual(EngineJob('game').collect(job,root)['status'],'completed')
            rows[-1]['movement_audit'].update(coverage_start=None,coverage_known=False);capture()
            with self.assertRaisesRegex(ValueError,'segment coverage'):EngineJob('game').collect(job,root)

    def test_all_transport_movement_and_unknown_continuation_are_explicit(self):
        rows,_,_=fixture()
        self.assertIsNone(read_game(stream(rows),{})['movement_audit'])
        rows[0]['movement_audit_version']=1
        audit={'version':1,'scope':'all-transport-committed-grid-moves','kind_names':list(MOVEMENT_KINDS),
               'coverage_start':5,'coverage_end':10,'coverage_known':True,'coverage_end_exclusive':True,
               'moves':[1,2,3,4,5,6],'weighted_cost':[10,20,30,40,50,60],
               'total_moves':21,'total_weighted_cost':210,'excluded_non_neighbor_moves':1,
               'unclaimed_source_separation_available':False}
        rows[-1]['movement_audit']=audit
        self.assertEqual(read_game(stream(rows),{})['movement_audit'],audit)
        for changes in ({'total_moves':20},{'moves':[1]},{'coverage_start':True},
                        {'coverage_known':False},{'coverage_end_exclusive':False},{'unclaimed_source_separation_available':True},
                        {'weighted_cost':[0,20,30,40,50,60]},{'excluded_non_neighbor_moves':-1}):
            bad=copy.deepcopy(rows);bad[-1]['movement_audit'].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):list(trace_rows(stream(bad)))
        audit.update(coverage_start=None,coverage_known=False)
        self.assertIsNone(read_game(stream(rows),{})['movement_audit']['coverage_start'])
        audit.update(coverage_start=5,coverage_end=None)
        self.assertIsNone(read_game(stream(rows),{})['movement_audit']['coverage_end'])
        del rows[0]['movement_audit_version']
        with self.assertRaisesRegex(ValueError,'unadvertised movement'):list(trace_rows(stream(rows)))

    def test_journeys_do_not_change_decision_counts_or_impute_partial_optima(self):
        rows,_,_=fixture();game=read_game(stream(rows),{})
        self.assertEqual(game['diagnostics']['events'],0)
        self.assertEqual(game['journeys']['rows'],1);self.assertEqual(game['journeys']['partial'],1)
        self.assertEqual(game['metrics'],{} if isinstance(game['metrics'],dict) else [])
        rows[1].update(baseline_status='limit',baseline_cost=None)
        self.assertEqual(read_game(stream(rows),{})['journeys']['baseline_missing'],1)

    def test_duplicate_unadvertised_bad_baseline_and_bounds_rejected(self):
        rows,_,_=fixture()
        for changes in ({'end_tick':1},{'task':True},{'events':[1]}, {'baseline_status':'limit'},
                        {'baseline_status':'not_ready','baseline_cost':None}, {'policy_digest':'bad'},
                        {'steps':0}, {'terminal':'success'}):
            altered=copy.deepcopy(rows);altered[1].update(changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):list(trace_rows(stream(altered)))
        with self.assertRaisesRegex(ValueError,'duplicate'):list(trace_rows(stream(rows[:2]+rows[1:])))
        altered=copy.deepcopy(rows);del altered[0]['live_journey_version']
        with self.assertRaises(ValueError):list(trace_rows(stream(altered)))
        altered=copy.deepcopy(rows);altered[2]['live']['max_admissions']=513
        with self.assertRaises(ValueError):list(trace_rows(stream(altered)))

    def test_task_generation_retains_full_uint64_wire_range(self):
        rows,_,_=fixture();rows[1]['task']=2**64-1
        self.assertEqual(len(list(trace_rows(stream(rows)))),3)
        rows[1]['task']=2**64
        with self.assertRaises(ValueError):list(trace_rows(stream(rows)))

    def test_summary_censor_phase_and_completion_counterexamples(self):
        rows,_,_=fixture()
        for changes in ({'sampling_censored':True},{'journey_completed':0},{'journey_sampled':2},
                        {'admitted_by_phase':[0]*16},{'censored_journey_outcomes':0},{'actual_moves':3},{'scope':'all-movement'},{'sample_every':2},{'max_admissions':33},
                        {'phase_bin_ticks':8192},{'unclaimed_movement_available':True}):
            altered=copy.deepcopy(rows);altered[-1]['live'].update(changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):list(trace_rows(stream(altered)))

    def test_legacy_zero_journey_stream_and_prototype_digest(self):
        rows,_,_=fixture();del rows[0]['live_journey_version'];del rows[0]['live_event_names'];del rows[-1]['live']
        self.assertEqual(len(list(trace_rows(stream([rows[0],rows[-1]])))),2)
        attest_digests([''],{'active_claim_policy':'prototype-stationary-v1'})
        with self.assertRaises(ValueError):attest_digests(['a'*64],{'active_claim_policy':'prototype-stationary-v1'})
        with self.assertRaises(ValueError):attest_digests([''],{'active_claim_policy':'disabled'})

    def test_collect_zero_decision_journey_attests_digest_and_isolated_archive(self):
        rows,job,result=fixture()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);output=root/'attempt/output';output.mkdir(parents=True)
            (output/'result.json').write_text(json.dumps(result));(root/'job.json').write_text(json.dumps(job))
            def write_trace():
                with gzip.open(output/'resource-claims-shadow.jsonl.gz','wt') as f:f.write(stream(rows).read())
            write_trace();self.assertEqual(EngineJob('game').collect(job,root/'attempt')['status'],'completed')
            archive=root/'worker.pyz';worker_archive(archive)
            code="import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from tournaments.jobs import EngineJob;assert EngineJob('game').collect(json.loads(Path(sys.argv[2]).read_text()),sys.argv[3])['status']=='completed';assert 'numpy' not in sys.modules"
            env=dict(os.environ);env.pop('PYTHONPATH',None)
            child=subprocess.run([sys.executable,'-I','-S','-c',code,str(archive),str(root/'job.json'),str(root/'attempt')],cwd=root,env=env,capture_output=True,text=True)
            self.assertEqual(child.returncode,0,child.stderr)
            rows[1]['policy_digest']='f'*64;write_trace()
            with self.assertRaisesRegex(ValueError,'journey policy digest'):EngineJob('game').collect(job,root/'attempt')


if __name__=='__main__':unittest.main()
