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
from tools.tournaments.claim_journeys import MOVEMENT_KINDS, attest_digests
from tools.tournaments.resource_claims import trace_rows, read_game
from tools.tournaments.jobs import EngineJob
from tools.tournaments.transport import worker_archive
from claim_trace_fixture import job_and_result, movement_audit, stream, trace


def fixture():
    job,result=job_and_result()
    return trace(),job,result


class Journeys(unittest.TestCase):
    def test_required_movement_capture_rejects_unknown_coverage(self):
        rows,job,result=fixture();job['labels']={'require_movement_audit':True}
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);out=root/'output';out.mkdir();(out/'result.json').write_text(json.dumps(result))
            def capture():
                with gzip.open(out/'resource-claims-shadow.jsonl.gz','wt') as f:f.write(stream(rows).getvalue())
            capture();self.assertEqual(EngineJob('game').collect(job,root)['status'],'completed')
            rows[-1]['movement_audit'].update(coverage_start=None,coverage_known=False);capture()
            with self.assertRaisesRegex(ValueError,'segment coverage'):EngineJob('game').collect(job,root)

    def test_all_transport_movement_and_unknown_continuation_are_explicit(self):
        rows,_,_=fixture()
        audit=movement_audit((1,2,3,4,5,6),(10,20,30,40,50,60));audit['excluded_non_neighbor_moves']=1
        rows[-1]['movement_audit']=audit
        self.assertEqual(read_game(stream(rows))['movement_audit'],audit)
        for changes in ({'total_moves':20},{'moves':[1]},{'coverage_start':True},
                        {'coverage_known':False},{'coverage_end_exclusive':False},{'unclaimed_source_separation_available':True},
                        {'weighted_cost':[0,20,30,40,50,60]},{'excluded_non_neighbor_moves':-1}):
            bad=copy.deepcopy(rows);bad[-1]['movement_audit'].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):list(trace_rows(stream(bad)))
        audit.update(coverage_start=None,coverage_known=False)
        self.assertIsNone(read_game(stream(rows))['movement_audit']['coverage_start'])
        audit.update(coverage_start=5,coverage_end=None)
        self.assertIsNone(read_game(stream(rows))['movement_audit']['coverage_end'])
        del rows[0]['movement_audit_version']
        with self.assertRaisesRegex(ValueError,'movement audit'):list(trace_rows(stream(rows)))

    def test_journeys_do_not_impute_partial_optima(self):
        rows,_,_=fixture();game=read_game(stream(rows))
        self.assertEqual(game['journeys']['rows'],1);self.assertEqual(game['journeys']['partial'],1)
        rows[1].update(baseline_status='limit',baseline_cost=None)
        self.assertEqual(read_game(stream(rows))['journeys']['baseline_missing'],1)

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

    def test_old_versions_and_unknown_rows_rejected_and_prototype_digest(self):
        rows,_,_=fixture()
        for change in ({'version':1},{'kind':'decision'}):
            bad=copy.deepcopy(rows);bad[0].update(change)
            with self.subTest(change=change),self.assertRaises(ValueError):list(trace_rows(stream(bad)))
        bad=copy.deepcopy(rows);bad.insert(1,{'kind':'decision','ordinal':1})
        with self.assertRaisesRegex(ValueError,'unknown claim trace row'):list(trace_rows(stream(bad)))
        attest_digests([''],{'active_claim_policy':'prototype-stationary-v1'})
        with self.assertRaises(ValueError):attest_digests(['a'*64],{'active_claim_policy':'prototype-stationary-v1'})
        with self.assertRaises(ValueError):attest_digests([''],{'active_claim_policy':'disabled'})

    def test_collect_journey_attests_digest_and_isolated_archive(self):
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
