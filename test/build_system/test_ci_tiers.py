import importlib.util
import io
import math
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'.github/scripts'))
import ci_changed_paths as selector
import ci_coverage_baseline as guard
import ci_policy as policy
import ci_check_inventory as inventory
import ci_verify_selection as gate

class TierTest(unittest.TestCase):
    def profile(self,paths,event='pull_request'):
        return selector.coverage_profile(paths,event,selector.classify(paths))
    def test_known_implementation_and_test_boundaries_are_primary(self):
        self.assertFalse(self.profile(['src/map/gradient/PathGradientHarness.cpp'])['compatibility'])
        gui=self.profile(['src/hud/input/GameGUIInput.cpp'])
        self.assertFalse(gui['compatibility']);self.assertTrue(gui['browsers_all']);self.assertTrue(gui['android'])
        self.assertEqual(gui['android_arches'],['arm64-v8a'])
    def test_shared_simulation_platform_and_unknown_paths_are_full(self):
        for path in ['src/hud/GameGUI.h','src/game/Game_sync.cpp','src/ai/AI.cpp','src/net/Net.cpp','SConstruct','platforms/mobile/android.py','new-unknown-directory/thing.cpp','test/fixtures/javascript/profile.game.gz']:
            with self.subTest(path=path):self.assertTrue(self.profile([path])['compatibility'])
        self.assertTrue(self.profile(['src/map/gradient/PathGradientHarness.cpp','SConstruct'])['compatibility'])
        self.assertTrue(self.profile([])['android'])
    def test_master_and_schedule_override_docs(self):
        for event in ['push','schedule','workflow_dispatch']:
            result=self.profile(['README.md'],event)
            self.assertTrue(result['compatibility']);self.assertTrue(result['android']);self.assertTrue(result['browsers_all'])
    def test_runtime_and_shard_configuration_exercises_both_gcc_platforms(self):
        for path in ['test/run_tests.py','test/ci_native_shard_plan.py','test/ci-native-auxiliary.json','test/build_ci_timing_profile.py','test/ci-timings/ubuntu-22.04.json']:
            with self.subTest(path=path):self.assertTrue(self.profile([path])['compatibility'])

    def test_release_metadata_defers_to_release_but_mobile_workflow_stays_relevant(self):
        for path in ['platforms/stores/fdroid/metadata.yml','fastlane/metadata/title.txt']:
            self.assertFalse(self.profile([path])['android'])
        self.assertTrue(self.profile(['.github/workflows/mobile.yml'])['android'])
    def test_ci_contract_tests_do_not_select_mobile_or_compatibility(self):
        for path in selector.CI_TOOL_TESTS:
            with self.subTest(path=path):
                profile=self.profile([path])
                self.assertFalse(profile['android'])
                self.assertFalse(profile['compatibility'])
                self.assertEqual(profile['profile'],'lightweight')
                self.assertTrue(self.profile([path,'SConstruct'])['compatibility'])
                self.assertTrue(self.profile([path,'SConstruct'])['android'])
    def selection(self, paths, event='pull_request', draft=False):
        # A known passing observation for every current check isolates policy
        # integration from missing-history expansion or a network dependency.
        history = {'schema': 1, 'updated_at': 100, 'records': [
            dict(name=check['name'], branch='master', event='push', sha='a'*40,
                 run_id=i+1, attempt=1, created_at=1, end=2, seconds=1,
                 failed=0, paths=['src/map/Map.cpp'])
            for i, check in enumerate(inventory.inventory())]}
        with tempfile.TemporaryDirectory() as directory:
            old_cwd=Path.cwd()
            try:
                os.chdir(directory)
                with patch.object(selector,'changed_paths',return_value=paths), \
                     patch.object(selector,'_prediction_inputs',return_value=(history,None,[])), \
                     patch.object(guard,'activated',return_value=False), \
                     patch.object(sys,'argv',['ci_changed_paths.py','--base','base']), \
                     patch.dict(os.environ,{'GITHUB_EVENT_NAME':event,'DRAFT':str(draft).lower(),
                         'GITHUB_OUTPUT':str(Path(directory)/'outputs'),
                         'GITHUB_STEP_SUMMARY':str(Path(directory)/'summary')},clear=True), \
                     patch('sys.stdout',new_callable=io.StringIO), patch('sys.stderr',new_callable=io.StringIO):
                    selector.main()
                observed=json.loads(Path('artifacts/ci-selection.json').read_text())
                outputs=dict(line.split('=',1) for line in Path('outputs').read_text().splitlines())
                return observed,outputs,Path('summary').read_text()
            finally:
                os.chdir(old_cwd)

    def test_ci_contract_changes_require_full_inventory_even_without_old_tier_activation(self):
        # The new policy treats edits to CI/build machinery as a full-inventory
        # exception; the former cheap-contract shortcut is no longer effective.
        for event in ('pull_request','push'):
            with self.subTest(event=event):
                observed,outputs,_=self.selection(sorted(selector.CI_TOOL_TESTS),event)
                self.assertTrue(observed['selection']['native'])
                self.assertTrue(observed['selection']['android'])
                self.assertTrue(observed['full_matrix'])
                self.assertFalse(observed['tiers_enabled'])
                self.assertEqual(outputs['verification_mode'],'full')
                self.assertEqual(set(observed['execution_plan']['executed_ids']),
                                 {check['id'] for check in inventory.inventory()})

    def test_default_pull_requests_execute_predictive_budget_in_draft_and_ready(self):
        for draft in (True,False):
            with self.subTest(draft=draft):
                observed,outputs,summary=self.selection(['src/map/Map.cpp'],draft=draft)
                plan=observed['execution_plan'];predictor=plan['predictor']
                self.assertEqual(observed['draft'],draft)
                self.assertEqual(outputs['verification_mode'],'predictive')
                self.assertTrue(observed['predictive_enabled'])
                self.assertFalse(observed['tiers_enabled'])
                self.assertFalse(observed['full_matrix'])
                candidates=[check for check in inventory.inventory() if not check.get('full_only')]
                self.assertEqual(predictor['selected_count'],math.ceil(.12*len(candidates)))
                self.assertEqual(len(plan['ranked_ids']),predictor['selected_count'])
                self.assertEqual(plan['executed_ids'],inventory.dependency_closure(plan['ranked_ids']))
                self.assertEqual(predictor['unknown_ids'],[])
                for job,on in plan['jobs'].items():
                    self.assertEqual(outputs['run_'+job.replace('-','_')],str(on).lower())
                self.assertIn('Ranked budget: 12%',summary)
        # Pushes retain the entire development inventory regardless of a draft
        # PR's policy or the learned ranking.
        master,_,_=self.selection(['src/map/Map.cpp'],event='push',draft=True)
        self.assertTrue(master['full_matrix'])
        workflow=(ROOT/'.github/workflows/build.yml').read_text()
        self.assertIn('types: [opened, synchronize, reopened, ready_for_review, converted_to_draft, labeled, unlabeled]',workflow)
        self.assertIn('DRAFT: ${{ github.event.pull_request.draft }}',workflow)

    def test_cheap_only_summary_does_not_claim_engine_verification(self):
        observed,outputs,summary=self.selection(['README.md'])
        self.assertEqual(outputs['verification_mode'],'cheap-contracts')
        self.assertEqual(observed['execution_plan']['executed_ids'],[])
        self.assertIn('does not establish engine verification or acceptance of PR evidence',summary)
    def test_complete_browser_inventory_keeps_every_command(self):
        matrix=json.loads((ROOT/'.github/scripts/ci_browser_matrix.json').read_text())
        self.assertEqual({x['browsers'] for x in matrix},{'chromium','firefox','webkit'})
        self.assertTrue(all(x['command'] for x in matrix))
        self.assertGreater(len(matrix),10)
    def test_reusable_android_does_not_share_caller_concurrency_group(self):
        self.assertIn('group: mobile-${{ github.workflow }}-${{ github.ref }}',(ROOT/'.github/workflows/mobile.yml').read_text())

class BaselineTest(unittest.TestCase):
    def read(self,run=None,observed=None,expired=False):
        run=run or dict(name='build',head_branch='master',event='push',conclusion='success',head_sha='abc')
        observed=observed or dict(full_matrix=True,sha='abc',selection=policy.full(),policy_fingerprint=policy.fingerprint())
        archive=io.BytesIO()
        with zipfile.ZipFile(archive,'w') as z:z.writestr('ci-selection.json',json.dumps(observed))
        def read(path,token,binary=False):
            if binary:return archive.getvalue()
            if 'artifacts?' in path:return {'artifacts':[dict(name='ci-observation-selection',expired=expired,id=1)]}
            return run
        return read
    def test_only_successful_proven_full_master_baseline_activates(self):
        self.assertTrue(guard.validated_baseline('owner/repo','1','token',self.read()))
        for conclusion in ['failure','cancelled',None]:
            run=dict(name='build',head_branch='master',event='push',conclusion=conclusion,head_sha='abc')
            self.assertFalse(guard.validated_baseline('owner/repo','1','token',self.read(run)))
        self.assertFalse(guard.validated_baseline('owner/repo','1','token',self.read(expired=True)))
        self.assertFalse(guard.validated_baseline('owner/repo','1','token',self.read(observed=dict(full_matrix=False,sha='abc',selection={}))))
        self.assertFalse(guard.validated_baseline('owner/repo','1','token',self.read(observed=dict(full_matrix=True,sha='wrong',selection={}))))
        self.assertFalse(guard.validated_baseline('owner/repo','not-a-run','token',self.read()))

    def test_nightly_reuses_only_exact_full_available_evidence(self):
        run = dict(id=1,name='build',head_branch='master',event='push',conclusion='success',head_sha='abc')
        observed = dict(full_matrix=True,sha='abc',selection=policy.full(),policy_fingerprint=policy.fingerprint())
        def lookup(run=run, observed=observed, expired=False):
            artifact_read = self.read(run, observed, expired)
            def read(path, token, binary=False):
                if 'workflows/build.yml/runs?' in path:
                    return {'workflow_runs': [run]}
                return artifact_read(path, token, binary)
            return guard.successful_full_run('owner/repo', 'abc', 'token', read)
        self.assertEqual(lookup(), 1)
        for change in ({'head_sha':'other'}, {'conclusion':'failure'}, {'event':'pull_request'}, {'head_branch':'feature'}):
            self.assertIsNone(lookup(run=dict(run, **change)))
        for change in ({'full_matrix':False}, {'sha':'other'}, {'selection':{}}, {'policy_fingerprint':'old'}):
            self.assertIsNone(lookup(observed=dict(observed, **change)))
        self.assertIsNone(lookup(expired=True))
        for error in (OSError('API unavailable'), ValueError('invalid JSON'), zipfile.BadZipFile('invalid archive')):
            with self.subTest(error=error), patch.object(guard, 'recent_successes', side_effect=error):
                self.assertIsNone(guard.successful_full_run('owner/repo', 'abc', 'token'))
        self.assertIsNone(guard.successful_full_run('owner/repo', '', 'token'))


class AggregateGateTest(unittest.TestCase):
    def evidence(self, selected):
        plan=inventory.execution_plan(selected)
        plan.update(mode='predictive',run_attempt=2)
        needs={'changes':{'result':'success'},**{
            job:{'result':'success' if expected else 'skipped'}
            for job,expected in plan['jobs'].items()}}
        jobs=[dict(id=i+1,name=check['name'],status='completed',conclusion='success',run_attempt=2)
              for i,check in enumerate(plan['expected_checks'])]
        return needs,plan,jobs

    def test_workflow_uses_real_fail_closed_gate_and_every_top_level_result(self):
        workflow=(ROOT/'.github/workflows/build.yml').read_text().split('  ci-result:\n',1)[1]
        self.assertIn('run: python3 .github/scripts/ci_verify_selection.py',workflow)
        self.assertIn('CI_JOB_PLAN: ${{ needs.changes.outputs.job_plan }}',workflow)
        self.assertIn('NEEDS_JSON: ${{ toJSON(needs) }}',workflow)
        for job in inventory.TOP_JOBS:
            self.assertIn(job,workflow.split('    if:',1)[0])

    def test_selected_failures_cancellations_and_unexpected_runs_cannot_pass(self):
        needs,plan,jobs=self.evidence([check['id'] for check in inventory.inventory()])
        self.assertTrue(gate.verify(needs,plan,jobs)['ok'])
        for job,expected in plan['jobs'].items():
            for result in ('failure','cancelled','skipped' if expected else 'success'):
                modified=json.loads(json.dumps(needs));modified[job]['result']=result
                with self.subTest(job=job,result=result):
                    self.assertFalse(gate.verify(modified,plan,jobs)['ok'])
        for index,job in enumerate(jobs):
            for result in ('failure','cancelled','skipped'):
                modified=[dict(item) for item in jobs];modified[index]['conclusion']=result
                with self.subTest(check=job['name'],result=result):
                    self.assertFalse(gate.verify(needs,plan,modified)['ok'])

    def test_cheap_contract_gate_requires_selection_success_and_all_results(self):
        needs,plan,jobs=self.evidence([])
        self.assertTrue(gate.verify(needs,plan,jobs)['ok'])
        del needs['windows']
        self.assertFalse(gate.verify(needs,plan,jobs)['ok'])
        needs,plan,jobs=self.evidence([])
        needs['changes']['result']='failure'
        self.assertFalse(gate.verify(needs,plan,jobs)['ok'])

    def test_partial_primary_gcc_requires_exact_selected_shard_and_producer(self):
        needs,plan,jobs=self.evidence(['native/gcc13/shard-2'])
        self.assertEqual(set(plan['executed_ids']),{'native/gcc13/build','native/gcc13/shard-2'})
        self.assertTrue(gate.verify(needs,plan,jobs)['ok'])
        self.assertFalse(gate.verify(needs,plan)['ok'])
        self.assertFalse(gate.verify(needs,plan,jobs[:1])['ok'])
        old_attempt=[dict(job,run_attempt=1) for job in jobs]
        self.assertFalse(gate.verify(needs,plan,old_attempt)['ok'])
        for job in ('linux-gcc13-producer','linux-gcc13','linux-build'):
            modified=json.loads(json.dumps(needs));modified[job]['result']='skipped'
            self.assertFalse(gate.verify(modified,plan,jobs)['ok'])
        # A partial GCC13 run does not turn unselected GCC11/Clang results into
        # evidence, and an unexpected compatibility failure remains visible.
        needs['linux-gcc11']['result']='failure'
        self.assertFalse(gate.verify(needs,plan,jobs)['ok'])
