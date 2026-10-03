import importlib.util
import io
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

class TierTest(unittest.TestCase):
    def profile(self,paths,event='pull_request'):
        return selector.coverage_profile(paths,event,selector.classify(paths))
    def test_known_implementation_and_test_boundaries_are_primary(self):
        self.assertFalse(self.profile(['test/PathGradientHarness.cpp'])['compatibility'])
        gui=self.profile(['src/gui/GameGUIInput.cpp'])
        self.assertFalse(gui['compatibility']);self.assertTrue(gui['browsers_all']);self.assertTrue(gui['android'])
        self.assertEqual(gui['android_arches'],['arm64-v8a'])
    def test_shared_simulation_platform_and_unknown_paths_are_full(self):
        for path in ['src/gui/GameGUI.h','src/Game_sync.cpp','src/ai/AI.cpp','src/net/Net.cpp','SConstruct','mobile/android.py','new-unknown-directory/thing.cpp','test/fixtures/javascript/profile.game.gz']:
            with self.subTest(path=path):self.assertTrue(self.profile([path])['compatibility'])
        self.assertTrue(self.profile(['test/PathGradientHarness.cpp','SConstruct'])['compatibility'])
        self.assertTrue(self.profile([])['android'])
    def test_schedule_and_dispatch_override_docs_while_master_pushes_follow_paths(self):
        for event in ['schedule','workflow_dispatch']:
            result=self.profile(['README.md'],event)
            self.assertTrue(result['compatibility']);self.assertTrue(result['android']);self.assertTrue(result['browsers_all'])
        push=self.profile(['README.md'],'push')
        self.assertFalse(push['compatibility']);self.assertFalse(push['android']);self.assertFalse(push['browsers_all'])
        self.assertTrue(self.profile([],'push')['compatibility'])
    def test_master_push_diffs_from_the_last_completed_master_build(self):
        runs={'workflow_runs':[
            {'event':'push','conclusion':None,'head_sha':'pending'},
            {'event':'push','conclusion':'cancelled','head_sha':'superseded'},
            {'event':'push','conclusion':'success','head_sha':'current'},
            {'event':'pull_request','conclusion':'success','head_sha':'pr'},
            {'event':'push','conclusion':'failure','head_sha':'tested'},
            {'event':'schedule','conclusion':'success','head_sha':'older'}]}
        self.assertEqual(selector.last_tested_master('o/r','t','current',read=lambda path,token:runs),'tested')
        self.assertIsNone(selector.last_tested_master('o/r','t','x',read=lambda path,token:{'workflow_runs':[]}))
        def outputs(event,base,paths):
            with tempfile.TemporaryDirectory() as directory:
                old_cwd=Path.cwd()
                try:
                    os.chdir(directory)
                    with patch.object(selector,'changed_paths',return_value=paths) as diff, \
                         patch.object(selector,'last_tested_master',return_value=base), \
                         patch.object(guard,'activated',return_value=False), \
                         patch.object(sys,'argv',['ci_changed_paths.py']), \
                         patch.dict(os.environ,{'GITHUB_EVENT_NAME':event,'GITHUB_OUTPUT':str(Path(directory)/'outputs')},clear=True), \
                         patch('sys.stdout',new_callable=io.StringIO), patch('sys.stderr',new_callable=io.StringIO):
                        selector.main()
                    return json.loads(Path('artifacts/ci-selection.json').read_text()),diff.call_args
                finally:
                    os.chdir(old_cwd)
        docs,call=outputs('push','tested',['README.md'])
        self.assertEqual(call.args,('tested',))
        self.assertEqual(set(docs['selection'].values()),{False});self.assertFalse(docs['full_matrix'])
        native,_=outputs('push','tested',['src/map/Map.cpp'])
        self.assertTrue(native['selection']['native'])
        # No completed master build (or an API failure): full CI, a valid baseline.
        full,call=outputs('push',None,['README.md'])
        self.assertIsNone(call);self.assertTrue(full['full_matrix'])
        scheduled,call=outputs('schedule','tested',['README.md'])
        self.assertIsNone(call);self.assertTrue(scheduled['full_matrix'])
    def test_runtime_and_shard_configuration_exercises_both_gcc_platforms(self):
        for path in ['test/run_tests.py','test/ci_native_shard_plan.py','test/ci-native-auxiliary.json','test/build_ci_timing_profile.py','test/ci-timings/ubuntu-22.04.json']:
            with self.subTest(path=path):self.assertTrue(self.profile([path])['compatibility'])

    def test_android_metadata_remains_relevant(self):
        for path in ['fdroid/metadata.yml','fastlane/metadata/title.txt','.github/workflows/mobile.yml']:
            self.assertTrue(self.profile([path])['android'])
    def test_ci_contract_tests_do_not_select_mobile_or_compatibility(self):
        for path in selector.CI_TOOL_TESTS:
            with self.subTest(path=path):
                profile=self.profile([path])
                self.assertFalse(profile['android'])
                self.assertFalse(profile['compatibility'])
                self.assertEqual(profile['profile'],'lightweight')
                self.assertTrue(self.profile([path,'SConstruct'])['compatibility'])
                self.assertTrue(self.profile([path,'SConstruct'])['android'])
    def test_contract_only_selection_is_lightweight_before_tiers_are_enabled(self):
        # Exercise the effective outputs, including the legacy Android fallback,
        # rather than just the desired profile. Scheduled runs must remain full coverage.
        for event in ['pull_request','schedule']:
            with self.subTest(event=event),tempfile.TemporaryDirectory() as directory:
                old_cwd=Path.cwd()
                try:
                    os.chdir(directory)
                    with patch.object(selector,'changed_paths',return_value=sorted(selector.CI_TOOL_TESTS)), \
                         patch.object(guard,'activated',return_value=False), \
                         patch.object(sys,'argv',['ci_changed_paths.py','--base','base']), \
                         patch.dict(os.environ,{'GITHUB_EVENT_NAME':event,'GITHUB_OUTPUT':str(Path(directory)/'outputs')},clear=True), \
                         patch('sys.stdout',new_callable=io.StringIO):
                        selector.main()
                    observed=json.loads(Path('artifacts/ci-selection.json').read_text())
                    self.assertEqual(set(observed['selection'].values()),{event=='schedule'})
                    self.assertEqual(observed['full_matrix'],event=='schedule')
                    self.assertFalse(observed['tiers_enabled'])
                finally:
                    os.chdir(old_cwd)
    def test_draft_pull_requests_defer_every_selected_check(self):
        paths=['src/Game_sync.cpp','src/gui/GameGUI.h','mobile/android.py']
        for draft in ['true','false']:
            with self.subTest(draft=draft),tempfile.TemporaryDirectory() as directory:
                old_cwd=Path.cwd()
                try:
                    os.chdir(directory)
                    with patch.object(selector,'changed_paths',return_value=paths), \
                         patch.object(guard,'activated',return_value=False), \
                         patch.object(sys,'argv',['ci_changed_paths.py','--base','base']), \
                         patch.dict(os.environ,{'GITHUB_EVENT_NAME':'pull_request','DRAFT':draft,'GITHUB_OUTPUT':str(Path(directory)/'outputs')},clear=True), \
                         patch('sys.stdout',new_callable=io.StringIO), patch('sys.stderr',new_callable=io.StringIO):
                        selector.main()
                    observed=json.loads(Path('artifacts/ci-selection.json').read_text())
                    outputs=dict(line.split('=',1) for line in Path('outputs').read_text().splitlines())
                    self.assertEqual(observed['draft'],draft=='true')
                    self.assertEqual(set(observed['selection'].values()),{draft=='false'})
                    for key in ['native','browser','map_generators','deployment','cross_platform','android','compatibility']:
                        self.assertEqual(outputs[key],'false' if draft=='true' else 'true',key)
                finally:
                    os.chdir(old_cwd)
        # A draft push is still deferred only for pull requests; master is always full.
        workflow=(ROOT/'.github/workflows/build.yml').read_text()
        self.assertIn('types: [opened, synchronize, reopened, ready_for_review]',workflow)
        self.assertIn('DRAFT: ${{ github.event.pull_request.draft }}',workflow)
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
        observed=observed or dict(full_matrix=True,sha='abc',selection={k:True for k in ['native','browser','map_generators','deployment','cross_platform','android']})
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

class AggregateGateTest(unittest.TestCase):
    def test_selected_failures_and_cancellations_cannot_pass(self):
        import re,os
        from unittest.mock import patch
        workflow=(ROOT/'.github/workflows/build.yml').read_text().split('  ci-result:\n',1)[1]
        code=workflow.split("          python3 - <<'PY'\n",1)[1].split('\n          PY',1)[0]
        import textwrap
        code=textwrap.dedent(code)
        jobs=['android','native-coverage','linux','linux-variants','linux-map-generators','windows','windows-server','web-build','web-native','web-deploy','web-test','browser-determinism','platform','platform-stack']
        selected={k:'true' for k in ['native','browser','map_generators','deployment','cross_platform','android','compatibility','platform','platform_stack']}
        needs={'changes':{'result':'success','outputs':selected},**{job:{'result':'success'} for job in jobs}}
        with patch.dict(os.environ,NEEDS_JSON=json.dumps(needs),GITHUB_EVENT_NAME='push'):
            exec(code,{})
        for job in jobs:
            for result in ['failure','cancelled','skipped']:
                modified=json.loads(json.dumps(needs));modified[job]['result']=result
                with self.subTest(job=job,result=result),patch.dict(os.environ,NEEDS_JSON=json.dumps(modified),GITHUB_EVENT_NAME='push'):
                    with self.assertRaises(AssertionError):exec(code,{})
    def test_primary_gcc_gate_rejects_unexpected_compatibility_results(self):
        import subprocess,os
        workflow=(ROOT/'.github/workflows/build.yml').read_text().split('  linux-build:\n',1)[1].split('  linux-clang:\n',1)[0]
        script=workflow.split('        run: |\n',1)[1]
        import textwrap
        script=textwrap.dedent(script)
        for compatibility,gcc11,gcc13,okay in [('false','skipped','success',True),('true','success','success',True),('true','skipped','success',False),('false','failure','success',False),('false','skipped','cancelled',False)]:
            result=subprocess.run(['bash','-e','-c',script],env=dict(os.environ,COMPATIBILITY=compatibility,GCC11=gcc11,GCC13=gcc13),capture_output=True)
            self.assertEqual(result.returncode==0,okay)
