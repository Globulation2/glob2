"""Coverage policy, checkpoint, cache and inventory safety contracts."""
import argparse
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.github/scripts'))
import ci_policy as policy
import ci_changed_paths as selector
import ci_coverage_baseline as baseline
import ci_cleanup as cleanup
import ci_inventory as inventory
import ci_dependency_cache as dependencies
import ci_compare_evidence as evidence
import ci_run_metrics as metrics


class PolicyTest(unittest.TestCase):
    def select(self, paths, labels=()):
        return policy.select(paths, labels, known=True)[0]

    def test_portable_cases_remain_primary_and_simulation_adds_compatibility(self):
        ordinary = self.select(['test/PathGradientHarness.cpp'])
        self.assertTrue(ordinary['native'])
        self.assertFalse(ordinary['windows'])
        self.assertFalse(ordinary['coverage'])
        for path in ['src/ai/Maxima.cpp','src/Game_sync.cpp','src/map/gradient/Gradient.cpp','src/unit/Unit.cpp']:
            result = self.select([path])
            for flag in ['native','windows','compatibility','cross_platform','browser']:
                self.assertTrue(result[flag], (path,flag))
            for flag in ['coverage','variants','android','deployment','map_generators']:
                self.assertFalse(result[flag], (path,flag))

    def test_rendering_mobile_maps_and_network_have_distinct_boundaries(self):
        ui = self.select(['src/gui/GameGUIInput.cpp'])
        self.assertTrue(ui['native'] and ui['browser'] and ui['android'])
        self.assertFalse(ui['cross_platform'] or ui['coverage'])
        maps = self.select(['src/map/generator/core/MapGenerator.cpp'])
        self.assertTrue(maps['map_generators'] and maps['compatibility'])
        network = self.select(['src/net/NetConnection.cpp'])
        self.assertTrue(network['windows'] and network['deployment'] and network['cross_platform'])
        self.assertTrue(self.select(['src/sim/SimulationRunner.cpp'])['tsan'])
        self.assertTrue(self.select(['src/scene/Scene.cpp'])['tsan'])

    def test_shared_unknown_and_unavailable_inputs_fail_closed(self):
        for path in ['src/Version.h','libgag/include/Surface.h','SConstruct','unmapped/new.cpp','test/ci_native_shard_plan.py']:
            self.assertEqual(self.select([path]), policy.full(), path)
        self.assertEqual(policy.select([], known=False)[0], policy.full())
        self.assertFalse(any(self.select([]).values()))
        self.assertEqual(self.select(['src/gui/GameGUIInput.cpp','SConstruct']),policy.full())

    def test_labels_only_expand(self):
        paths = ['src/Game_sync.cpp']
        ordinary = self.select(paths)
        for label in policy.LABELS:
            expanded = self.select(paths,[label])
            self.assertTrue(all(not value or expanded[key] for key,value in ordinary.items()))
        self.assertEqual(self.select(paths,['ci:full']),policy.full())
        self.assertEqual(self.select(paths,['ci:skip']),ordinary)

    def test_browser_compatibility_keeps_every_engine_and_thread_variant(self):
        sim = self.select(['src/Game_sync.cpp'])
        rows = policy.browser_matrix(sim,['src/Game_sync.cpp'])
        self.assertEqual({row['browsers'] for row in rows},{'chromium','firefox','webkit'})
        self.assertEqual(len(rows),3)
        self.assertTrue(all('determinism.spec.js' in row['command'] for row in rows))
        ui = self.select(['src/gui/GameGUIInput.cpp'])
        ui_rows=policy.browser_matrix(ui,['src/gui/GameGUIInput.cpp'])
        self.assertEqual({r['browsers'] for r in ui_rows},{'chromium','firefox','webkit'})
        self.assertFalse(any('scripting' in r['name'] or 'WSS and simulation' in r['name'] for r in ui_rows))
        self.assertLess(len(ui_rows),len(json.loads((ROOT/'.github/scripts/ci_browser_matrix.json').read_text())))

    def test_release_metadata_does_not_compile_engine(self):
        paths=['fdroid/metadata.yml','fastlane/metadata/title.txt','tools/package_steam_windows.py',
               '.github/workflows/steam-windows-package.yml','.github/workflows/mac-app-store.yml',
               'mobile/android_release.py','tests/build_system/test_ci_policy.py']
        self.assertFalse(any(self.select(paths).values()))
        before_activation,_=self.exercise('pull_request',paths,enabled=False)
        self.assertFalse(any(before_activation['selection'].values()))

    def exercise(self,event,paths,draft=False,labels=(),enabled=True,checkpoint='old'):
        with tempfile.TemporaryDirectory() as directory:
            payload=Path(directory)/'event.json'
            payload.write_text(json.dumps({'pull_request':{'draft':draft,'labels':[{'name':label} for label in labels]}}))
            old=Path.cwd()
            try:
                os.chdir(directory)
                with patch.dict(os.environ,{'GITHUB_EVENT_NAME':event,'GITHUB_EVENT_PATH':str(payload),'GITHUB_REPOSITORY':'o/r','GH_TOKEN':'token'},clear=True),patch.object(sys,'argv',['selector','--base','pr-base']),patch.object(selector,'changed_paths',return_value=paths) as diff,patch.object(baseline,'activated',return_value=enabled),patch.object(baseline,'master_checkpoint',return_value=checkpoint),redirect_stdout(io.StringIO()):
                    selector.main()
                return json.loads(Path('artifacts/ci-selection.json').read_text()),diff.call_args
            finally:
                os.chdir(old)

    def test_draft_ready_and_explicit_run_transitions(self):
        draft,_=self.exercise('pull_request',['src/Game_sync.cpp'],draft=True)
        self.assertFalse(any(draft['selection'].values()))
        ready,_=self.exercise('pull_request',['src/Game_sync.cpp'])
        forced,_=self.exercise('pull_request',['src/Game_sync.cpp'],draft=True,labels=['ci:run'])
        self.assertEqual(ready['selection'],forced['selection'])
        self.assertFalse(forced['draft'])

    def test_timing_inventory_does_not_change_for_equivalent_policy_implementation(self):
        with patch.object(policy, 'fingerprint', return_value='implementation-one'):
            before, _ = self.exercise('pull_request',['src/Game_sync.cpp'])
        with patch.object(policy, 'fingerprint', return_value='implementation-two'):
            after, _ = self.exercise('pull_request',['src/Game_sync.cpp'])
        self.assertNotEqual(before['policy_fingerprint'],after['policy_fingerprint'])
        self.assertEqual(before['inventory_fingerprint'],after['inventory_fingerprint'])

    def test_master_uses_successful_checkpoint_not_last_push(self):
        report,call=self.exercise('push',['src/Game_sync.cpp'],checkpoint='last-success')
        self.assertEqual(call.args,('last-success',))
        self.assertEqual(report['checkpoint'],'last-success')
        self.assertTrue(report['checkpoint_eligible'])
        report,call=self.exercise('push',[],checkpoint=None)
        self.assertIsNone(call)
        self.assertTrue(report['full_matrix'])
        report,_=self.exercise('schedule',['README.md'])
        self.assertTrue(report['full_matrix'])

    def test_checkpoint_never_uses_divergent_or_unattested_revision(self):
        runs=[{'id':1,'event':'push','head_sha':'future'},{'id':2,'event':'push','head_sha':'last-success'}]
        with patch.object(baseline,'recent_successes',return_value=runs),patch.object(baseline,'selection_evidence',return_value={'checkpoint_eligible':True,'policy_fingerprint':policy.fingerprint()}),patch.object(baseline.subprocess,'run',side_effect=[argparse.Namespace(returncode=1),argparse.Namespace(returncode=0)]):
            self.assertEqual(baseline.master_checkpoint('o/r','token'),'last-success')
        with patch.object(baseline,'recent_successes',return_value=runs),patch.object(baseline,'selection_evidence',return_value={'checkpoint_eligible':True,'policy_fingerprint':'old-policy'}),patch.object(baseline.subprocess,'run',return_value=argparse.Namespace(returncode=0)):
            self.assertIsNone(baseline.master_checkpoint('o/r','token'))


class ReuseTest(unittest.TestCase):
    def test_native_coverage_audit_rejects_duplicate_missing_and_disagreeing_shards(self):
        rows=[{'eligible':['a','b'],'assigned':['a'],'profile':'full'}, {'eligible':['a','b'],'assigned':['b'],'profile':'full'}]
        self.assertEqual(inventory.audit(rows),2)
        for bad in [rows[:1],rows+[rows[0]],[rows[0],dict(rows[1],eligible=['b'])],[]]:
            with self.assertRaises(ValueError):inventory.audit(bad)

    def test_restored_dependencies_require_matching_identity_and_exact_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'library.so').write_bytes(b'good')
            seal={'key':'toolchain','files':dependencies.contents(root)}
            (root/'.ci-cache.json').write_text(json.dumps(seal))
            self.assertTrue(dependencies.verify(root,'toolchain'))
            self.assertFalse(dependencies.verify(root,'changed-compiler'))
            (root/'library.so').write_bytes(b'bad')
            self.assertFalse(dependencies.verify(root,'toolchain'))

    def test_cleanup_protects_open_pr_and_newest_master_generations(self):
        rows=[{'id':n,'key':f'ccache-main-gcc-37000000{n}-1','ref':'refs/heads/master','created_at':str(n)} for n in range(1,6)]
        rows += [{'id':10,'key':'pr','ref':'refs/pull/10/merge'}, {'id':11,'key':'pr','ref':'refs/pull/11/merge'}]
        self.assertEqual({row['id'] for row in cleanup.obsolete_caches(rows,{11})},{1,2,10})

    def test_different_case_inventories_do_not_produce_speed_comparisons(self):
        run=dict(event='pull_request',selection={'native':True},conclusion='success',inventory_fingerprint='a',queue_seconds=1,execution_seconds=2,runner_minutes=3,time_to_result_seconds=4)
        self.assertEqual(metrics.compare([run]*10,[dict(run,inventory_fingerprint='b')]*10),[])

    def test_native_and_browser_evidence_must_include_every_selected_identity(self):
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);trace=b'x'*1501
            for platform in ['ubuntu-24.04','windows']:
                path=root/f'browser-determinism-{platform}'/'native.replay.checksums';path.parent.mkdir();path.write_bytes(trace)
            for variant,threads in [('serial',1),('threaded',1),('threaded',2),('threaded',4)]:
                path=root/'browser-determinism-wasm-0'/'wasm'/f'{variant}-{threads}';path.mkdir(parents=True)
                (path/'wasm.replay.checksums').write_bytes(trace)
                (path/'manifest.json').write_text(json.dumps(dict(project='chromium',variant=variant,threads=threads,ticks=1500,seed=42,trace_sha256=hashlib.sha256(trace).hexdigest())))
            self.assertEqual(evidence.validate_traces(root,['ubuntu-24.04','windows'],{'chromium'}),6)
            with self.assertRaises(ValueError):evidence.validate_traces(root,['ubuntu-24.04','windows'],{'chromium','firefox'})
            (root/'browser-determinism-windows'/'native.replay.checksums').write_bytes(b'y'*1501)
            with self.assertRaises(ValueError):evidence.validate_traces(root,['ubuntu-24.04','windows'],{'chromium'})


if __name__=='__main__':unittest.main()
