#!/usr/bin/env python3
"""Protocol, durable lease acceptance, transfer corruption and offline analysis tests."""
import base64
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.bundles import package_identity, platform_identity, register_bundle
from tools.tournaments.common import atomic_json, digest, file_hash, read_json, store_artifact
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job, validate_experiment
from tools.tournaments.transfer import put_chunk, offset
from tools.tournaments.worker import Worker

FAKE = '''#!/usr/bin/env python3
import json, pathlib, sys
if sys.argv[1]=='--headless-catalog':
 print(json.dumps({'schema_version':1,'commands':['game','generate_map'],'ais':[],'generators':[]}));sys.exit(0)
if '--report' in sys.argv and sys.argv[sys.argv.index('--report')+1]=='slow':
 import time;time.sleep(10)
out=pathlib.Path(sys.argv[sys.argv.index('--output-dir')+1]);out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps({'schema_version':1,'status':'completed','job_type':'generate_map'}))
'''


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        source = self.root / 'source'; source.mkdir()
        (source / 'glob2').write_text(FAKE); (source / 'glob2').chmod(0o755)
        self.bundle = register_bundle(source, self.root / 'bundles', 'glob2', 'fixture')
        self.job = job('generate_map', self.bundle['id'], seeds={'map': 1}, config={'generator': 15})
        self.manifest = {'schema_version': 1, 'id': 'fixture', 'jobs': [self.job],
                         'settings': {'heartbeat_seconds': 0.1, 'lease_seconds': 1}}
        self.coordinator = Coordinator.submit(self.root / 'experiment', self.manifest,
                                               [self.root / 'bundles' / self.bundle['id']])
        self.status = {'accepting': True, 'slots': 1, 'platform': platform_identity(), 'builds': []}

    def tearDown(self):
        self.coordinator.close()
        self.temp.cleanup()

    def result(self, attempt, category='success'):
        return {'schema_version': 1, 'id': attempt['id'], 'token': attempt['token'], 'job': attempt['job'],
                'host': attempt['host'], 'package_id': package_identity(), 'artifacts': [],
                'category': category, 'result': {'schema_version': 1, 'status': 'completed'}, 'missing_artifacts': []}

    def test_expiry_late_duplicate_and_restart(self):
        c = self.coordinator
        first = c.dispatch('one', self.status, now=10)[0]
        c.expire(now=12)
        second = c.dispatch('two', self.status, now=12)[0]
        self.assertFalse(c.accept(self.result(first), now=12.1))
        self.assertTrue(c.accept(self.result(second), now=12.2))
        self.assertTrue(c.accept(self.result(second), now=12.3))
        self.assertEqual(c.status()['jobs'], {'completed': 1})
        path = c.root / 'results' / (self.job['id'] + '.json')
        path.unlink()
        c.close(); self.coordinator = Coordinator(c.root)
        self.assertEqual(read_json(path)['id'], second['id'])
        self.assertEqual(len(list((c.root / 'attempts').glob('*.json'))), 2)

    def test_expired_lease_cannot_be_revived(self):
        c = self.coordinator
        first = c.dispatch('one', self.status, now=10)[0]
        c.renew('one', [first], now=12)
        self.assertFalse(c.accept(self.result(first), now=12))
        self.assertEqual(c.status()['jobs'], {'pending': 1})

    def test_corruption_prevents_commit(self):
        c = self.coordinator
        attempt = c.dispatch('one', self.status)[0]
        record = self.result(attempt)
        record['artifacts'] = [{'sha256': 'f' * 64, 'bytes': 3}]
        with self.assertRaises(ValueError): c.accept(record)
        self.assertEqual(c.status()['jobs'], {'active': 1})

    def test_crash_retry_budget(self):
        c = self.coordinator
        for _ in range(2):
            attempt = c.dispatch('one', self.status)[0]
            c.accept(self.result(attempt, 'crash'))
        self.assertEqual(c.status()['jobs'], {'failed': 1})
        c.retry()
        self.assertEqual(c.status()['jobs'], {'pending': 1})

    def test_generation_failure_not_retried(self):
        attempt = self.coordinator.dispatch('one', self.status)[0]
        self.assertTrue(self.coordinator.accept(self.result(attempt, 'generation_failed')))
        self.assertEqual(self.coordinator.dispatch('one', self.status), [])

    def test_pause_cancel_and_disk_pressure(self):
        c = self.coordinator
        c.control('paused')
        self.assertEqual(c.dispatch('one', self.status), [])
        c.control('running')
        self.assertEqual(c.dispatch('one', self.status | {'accepting': False}), [])
        attempt = c.dispatch('one', self.status)[0]
        c.control('cancelled')
        self.assertFalse(c.accept(self.result(attempt)))
        self.assertEqual(c.status()['jobs'], {'cancelled': 1})
        with self.assertRaises(ValueError): c.control('running')

    def test_manifest_and_build_mismatch(self):
        with self.assertRaises(ValueError):
            Coordinator.submit(self.coordinator.root, dict(self.manifest, labels={'changed': True}), [])
        attempt = self.coordinator.dispatch('one', self.status)[0]
        record = self.result(attempt)
        record['job'] = dict(record['job'], build='a' * 64)
        with self.assertRaises(ValueError): self.coordinator.accept(record)

    def test_dependency_cycle(self):
        item = dict(self.job, depends_on=[self.job['id']])
        with self.assertRaises(ValueError): validate_experiment(dict(self.manifest, jobs=[item]))

    def test_chunk_resume_lost_ack_corruption(self):
        root = self.root / 'chunks'
        content = b'first-second'
        identity = hashlib.sha256(content).hexdigest()
        def put(start, block):
            return put_chunk(root, identity, len(content), start, base64.b64encode(block).decode(), hashlib.sha256(block).hexdigest())
        self.assertFalse(put(0, content[:5])['complete'])
        self.assertEqual(put(0, content[:5])['offset'], 5)
        self.assertEqual(offset(root, identity, len(content)), 5)
        with self.assertRaises(ValueError): put(0, b'wrong')
        self.assertTrue(put(5, content[5:])['complete'])
        self.assertTrue(put(5, content[5:])['complete'])
        (root / identity).write_bytes(b'corrupt')
        with self.assertRaises(ValueError): offset(root, identity, len(content))

    def test_ack_cleanup_recovers_after_durable_commit(self):
        import shutil
        worker=Worker(self.root/'ack-worker')
        try:
            shutil.copytree(self.root/'bundles'/self.bundle['id'],worker.root/'bundles'/self.bundle['id'])
            attempt=self.coordinator.dispatch('one',self.status)[0]
            worker.enqueue(attempt)
            directory=worker.root/'attempts'/attempt['id']
            artifact=store_artifact(directory/'attempt.json',worker.root/'spool')
            atomic_json(directory/'record.json',{'artifacts':[artifact]})
            worker.db.execute("UPDATE queue SET state='acknowledged'")
            worker.acknowledge(attempt['id'],attempt['token'])
            self.assertFalse(directory.exists())
            self.assertFalse((worker.root/'spool'/artifact['sha256']).exists())
        finally:worker.close()

    def test_worker_queue_idempotency_and_disk_stop(self):
        import shutil
        worker = Worker(self.root / 'worker')
        try:
            shutil.copytree(self.root / 'bundles' / self.bundle['id'], worker.root / 'bundles' / self.bundle['id'])
            attempt = self.coordinator.dispatch('one', self.status)[0]
            self.assertTrue(worker.enqueue(attempt)['enqueued'])
            self.assertTrue(worker.enqueue(attempt)['enqueued'])
            self.assertEqual(worker.status()['counts'], {'queued': 1})
            worker.control('fixture', 'draining'); worker.tick()
            self.assertEqual(worker.status()['counts'], {'queued': 1})
            worker.control('fixture', 'cancelled'); worker.tick()
            self.assertEqual(worker.status()['counts'], {'cancelled': 1})
            worker.configure({'disk_reserve_bytes': 10**30})
            self.assertFalse(worker.status()['accepting'])
        finally: worker.close()


class AnalysisTests(unittest.TestCase):
    def team(self, number, **extra):
        return {'team':number,'alliance':number+1,'alive':True,'outcome':'unresolved',
                'prestige':0,'units':4,'buildings':1,'warrior_attack':0,'warrior_hp':0,
                'warriors':0,'eliminated_tick':-1, **extra}

    def test_cap_policies_ties_and_engine_authority(self):
        from tools.tournaments.analysis import adjudicate
        result={'termination':'tick_cap','winning_teams':[],
                'teams':[self.team(0,prestige=2),self.team(1,warrior_attack=10)]}
        self.assertEqual(adjudicate(result)['winners'],[0])
        self.assertEqual(adjudicate(result,'military')['winners'],[1])
        self.assertEqual(adjudicate(result,'survivor_draw')['winners'],[0,1])
        result['winning_teams']=[0]
        for policy in ('prestige','military','survivor_draw'):
            self.assertEqual(adjudicate(result,policy)['winners'],[0])
        result['winning_teams']=[0,1]
        self.assertEqual(adjudicate(result)['placements'],{0:1.5,1:1.5})

    def test_roster_adjudication_and_mixed_competitor_names(self):
        from tools.tournaments.analysis import adjudicate,observations
        teams=[self.team(0,alliance=1,prestige=2),self.team(1,alliance=1),
               self.team(2,alliance=2,prestige=1),self.team(3,alliance=2,prestige=1)]
        result={'termination':'tick_cap','winning_teams':[],'ticks':100,'teams':teams,
                'players':[{'team':i,'ai':name} for i,name in enumerate(['cortex','nicowar','maxima','maxima'])]}
        self.assertEqual(adjudicate(result,alliances=True)['placements'],{1:1.5,2:1.5})
        result['winning_teams']=[0,1]
        for policy in ('prestige','military','survivor_draw'):
            self.assertEqual(adjudicate(result,policy,alliances=True)['winners'],[1])
        record={'category':'success','result':result,'job':job('game','a'*64,labels={'format':'2v2'})}
        self.assertEqual(observations([record],'prestige')[0]['competitors'],['cortex+nicowar','maxima+maxima'])

    def test_all_ties_fairness_has_no_spurious_significance(self):
        from tools.tournaments.analysis import fairness
        row={'build':'a'*64,'map':'ties','generator':15,'teams':[self.team(0),self.team(1)],
             'winners':[0,1],'rotation':0,'engine_outcome':False,'symmetric_control':True}
        report=fairness([row])
        self.assertIsNone(report['maps'][0]['start']['q_bh'])
        self.assertIsNone(report['aggregates'][0]['rms_position_points'])

    def test_elo_semantics(self):
        from tools.tournaments.analysis import elo_update
        ratings={}
        elo_update(ratings,['a','b'],[1,2])
        self.assertEqual(ratings,{'a':1516,'b':1484})
        ratings={}
        elo_update(ratings,['a','b','c','d'],[1,2,3,4])
        self.assertAlmostEqual(sum(ratings.values()),6000)
        self.assertEqual(ratings['a'],1516)
        self.assertAlmostEqual(ratings['b'],1500+16/3)
        ratings={}
        elo_update(ratings,['a','b'],[1.5,1.5])
        self.assertEqual(ratings,{'a':1500,'b':1500})

    def test_planning_balance_and_ablations(self):
        from tools.tournaments.experiments import Planner
        bundle={'id':'a'*64,'capabilities':{'ais':[{'id':1,'name':'numbi'},{'id':2,'name':'castor'}]}}
        config={'id':'balance','ais':['numbi','castor'],'formats':['1v1'],'map_seeds':[1,2]}
        manifest=Planner('ai_comparison',config,[bundle]).plan()
        games=[j for j in manifest['jobs'] if j['type']=='game']
        self.assertEqual(len(games),8)
        for seed in (1,2):
            block=[j for j in games if j['labels']['map_seed']==seed]
            self.assertEqual(sum(j['config']['players'][0]=='numbi' for j in block),2)
        config={'id':'paired','map_seeds':[1],'held_out_map_seeds':[2],
                'players':['cortex','nicowar'],'one_parameter':{'swarmWorkerCap':[4,7]}}
        manifest=Planner('ablations',config,[bundle]).plan()
        pairs={}
        for j in manifest['jobs']:
            if j['type']=='game': pairs.setdefault(j['labels']['pair'],[]).append(j)
        self.assertEqual(len(pairs),4)
        for paired in pairs.values():
            self.assertEqual(len(paired),3)
            self.assertEqual(len({j['inputs']['map']['job'] for j in paired}),1)

    def test_manifest_order_offline_reanalysis(self):
        from tools.tournaments.analysis import rate,observations
        from tools.tournaments.results import Results
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'results').mkdir()
            jobs=[job('game','a'*64,inputs={'map':{'sha256':'b'*64}},seeds={'game':i},config={'players':['a','b']},job_id=f'j{i}') for i in range(2)]
            atomic_json(root/'experiment.json',{'schema_version':1,'id':'order','jobs':jobs})
            for i in (1,0):
                result={'ticks':100,'termination':'engine_end','winning_teams':[i],
                        'teams':[self.team(0,outcome='won' if i==0 else 'lost'),self.team(1,outcome='won' if i==1 else 'lost')],
                        'players':[{'team':0,'ai':'a'},{'team':1,'ai':'b'}]}
                atomic_json(root/'results'/f'j{i}.json',{'job':jobs[i],'category':'success','result':result})
            records=list(Results(root))
            self.assertEqual([r['job']['id'] for r in records],['j0','j1'])
            rows=observations(records,'prestige')
            self.assertNotEqual(rate(rows),rate(rows[::-1]))


class WorkerIntegration(unittest.TestCase):
    setUp = Fixture.setUp
    tearDown = Fixture.tearDown

    def test_external_watchdog_retries_once(self):
        from tools.tournaments.transport import Transport
        value=dict(self.job,limits={'timeout_seconds':0.05},outputs={'reports':['slow']})
        manifest=dict(self.manifest,id='watchdog',jobs=[value],settings={'heartbeat_seconds':0.1,'lease_seconds':30})
        c=Coordinator.submit(self.root/'watchdog',manifest,[self.root/'bundles'/self.bundle['id']])
        host={'name':'local','transport':'local','directory':str(self.root/'watchdog-worker'),'slots':1}
        try:
            state=c.run([host])
            self.assertEqual(state['jobs'],{'failed':1})
            self.assertEqual(c.db.execute("SELECT count(*) FROM attempts WHERE category='timeout'").fetchone()[0],2)
        finally:
            Transport(host,c.root/'worker.pyz').rpc('stop');c.close()

    def test_real_worker_protocol_and_lost_ack(self):
        from tools.tournaments.transport import Transport
        c=self.coordinator
        # Real process startup is not a synthetic one-second lease test.
        c.settings['lease_seconds']=30
        config={'name':'local','transport':'local','directory':str(self.root/'remote'),'slots':1}
        transport=Transport(config,c.root/'worker.pyz')
        try:
            for _ in range(20):
                c.sync_host(config)
                if c.status()['jobs']=={'completed':1}: break
                time.sleep(.2)
            self.assertEqual(c.status()['jobs'],{'completed':1},c.status())
            self.assertEqual(len(list((c.root/'results').glob('*.json'))),1)
            record=next(iter(__import__('tools.tournaments.results',fromlist=['Results']).Results(c.root)))
            self.assertTrue(transport.rpc('ack',identity=record['id'],token=record['token'])['acknowledged'])
            self.assertEqual(transport.rpc('status')['counts'],{'acknowledged':1})
        finally:
            transport.rpc('stop')
            for _ in range(20):
                if not transport.rpc('status')['daemon_running']: break
                time.sleep(.1)


class DiagnosticTests(unittest.TestCase):
    def test_core_lookup_cannot_select_a_previous_use_of_pid(self):
        from unittest.mock import patch,Mock
        from tools.tournaments.diagnostics import postmortem
        with tempfile.TemporaryDirectory() as directory:
            with patch('tools.tournaments.diagnostics.shutil.which',side_effect=lambda name:'/usr/bin/coredumpctl' if name=='coredumpctl' else None), patch('tools.tournaments.diagnostics.subprocess.run',return_value=Mock(stdout=b'no core',returncode=1)) as execute:
                info=postmortem('/supplied/glob2',123,directory,True,started=1234567890)
                command=execute.call_args.args[0]
                self.assertIn('--since',command)
                self.assertEqual(command[command.index('--since')+1],'2009-02-13 23:31:30 UTC')
                self.assertFalse(info['core_available'])


class ReconCalibrationTests(unittest.TestCase):
    def test_recon_parser_preserves_simulation_and_ai_clocks(self):
        from tools.maxima_recon_calibration import parse
        row = parse("MAXIMA_TELEMETRY\t1001\t2\trecon_audit\tenemy=3\ttruth_power=9007199254740993\tgame_tick=1100\n")
        self.assertEqual(row['tick'], 1001)
        self.assertEqual(row['game_tick'], 1100)
        self.assertEqual(row['truth_power'], 9007199254740993)
        self.assertIsNone(parse("MAXIMA_TELEMETRY\t1001\t2\trecon_snapshot\tenemy=3"))

    def test_target_pairs_do_not_join_different_observers_or_games(self):
        from tools.maxima_recon_calibration import target_metrics
        row = dict(job_id='a', observer=0, tick=1, kind='siege', enemy=1, gid=42, x=8, y=9)
        choices = [dict(row, view='fog'), dict(row, view='oracle'),
                   dict(row, observer=1, view='fog'), dict(row, job_id='b', view='oracle')]
        summary, pairs = target_metrics([], choices)
        self.assertEqual(summary['paired_choices'], 1)
        self.assertEqual(summary['exact_agreement'], 1)
        self.assertIsNone(summary['raid_fraction_inside_flag'])

    def test_raid_region_comparison_wraps_map_edges(self):
        from tools.maxima_recon_calibration import target_metrics
        row = dict(job_id='a', observer=0, tick=1, kind='raid', enemy=1,
                   gid=-1, x=127, y=9, map_width=128, map_height=128)
        summary, _ = target_metrics([], [dict(row, view='fog'), dict(row, x=0, view='oracle')])
        self.assertEqual(summary['exact_agreement'], 0)
        self.assertEqual(summary['same_region_agreement'], 1)

    def test_reference_memory_reconstruction_uses_integer_decay(self):
        from tools.maxima_recon_calibration import sampled_warriors
        row = dict(warrior_age=5000, visible_warriors=0, remembered_warriors=100)
        self.assertEqual(sampled_warriors(row), 67)
        self.assertEqual(sampled_warriors(dict(row, warrior_age=10000)), 0)
        self.assertEqual(sampled_warriors(dict(row, visible_warriors=80)), 80)

    def test_fit_allowlist_excludes_hidden_labels_and_opponent_identity(self):
        from tools.maxima_recon_calibration import FEATURES
        self.assertTrue(all(not name.startswith('truth_') for name in FEATURES))
        self.assertNotIn('enemy_ai', FEATURES)
        self.assertNotIn('opponent', FEATURES)


class ReconBeliefTests(unittest.TestCase):
    def test_training_loads_explicit_exports_and_preserves_geography_blocks(self):
        from tools.maxima_recon_belief import training_rows, write_rows
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / (name + '.jsonl.gz') for name in ('first', 'second')]
            for index, path in enumerate(paths):
                write_rows(path, [dict(job_id=str(index), block='shared-geography',
                                      truth_workers=10, truth_warriors=3, truth_explorers=2)])
            rows = training_rows(paths)
            self.assertEqual([row['job_id'] for row in rows], ['0', '1'])
            self.assertTrue(all(row['block'] == 'shared-geography' for row in rows))
            self.assertTrue(all(row['truth_units'] == 15 for row in rows))
            write_rows(paths[0], [dict(truth_workers=10, truth_warriors=3, truth_explorers=2)])
            with self.assertRaises(ValueError):
                training_rows(paths)

    def row(self, tick, visible=1, **values):
        from tools.maxima_recon_calibration import FEATURES
        row = {k: 0 for k in FEATURES}
        row.update(job_id='a', observer=0, enemy=1, tick=tick, game_tick=tick*2,
                   visible_warriors=visible, visible_power=visible*100,
                   truth_warriors=visible+3, truth_power=(visible+3)*100,
                   truth_units=visible+10)
        row.update(values)
        return row

    def test_future_sightings_and_labels_do_not_leak_into_cutoff_features(self):
        from tools.maxima_recon_belief import examples, features, BELIEF_FEATURES
        source = self.row(1)
        ordinary = examples([source, self.row(2001)], horizons=(2000,))[0]
        changed = examples([source, self.row(2001, visible=900, truth_warriors=5000)],
                           horizons=(2000,))[0]
        self.assertEqual(features(ordinary, BELIEF_FEATURES), features(changed, BELIEF_FEATURES))
        self.assertNotEqual(ordinary['truth_warriors'], changed['truth_warriors'])
        self.assertEqual(ordinary['forecast_horizon'], 2000)
        self.assertEqual(ordinary['label_game_tick'], 4002)

    def test_history_is_causal_and_separate_for_each_enemy_observer_and_game(self):
        from tools.maxima_recon_belief import histories
        rows = [self.row(1, 9), self.row(1001, 3), self.row(2001, 100),
                self.row(1001, 1, enemy=2), self.row(1001, 2, observer=1),
                self.row(1001, 4, job_id='b')]
        result = {(r['job_id'], r['observer'], r['enemy'], r['tick']): r for r in histories(rows)}
        before = result['a', 0, 1, 1001]
        self.assertEqual(before['visible_warriors_peak'], 9)
        self.assertEqual(before['visible_warriors_previous'], 9)
        self.assertEqual(before['visible_warriors_rate'], -6)
        self.assertEqual(result['a', 0, 2, 1001]['visible_warriors_peak'], 1)
        self.assertEqual(result['a', 1, 1, 1001]['visible_warriors_peak'], 2)
        self.assertEqual(result['b', 0, 1, 1001]['visible_warriors_peak'], 4)

    def test_blackout_labels_are_never_invented_past_game_end(self):
        from tools.maxima_recon_belief import examples
        self.assertEqual(examples([self.row(1)], horizons=(2000,)), [])
        self.assertEqual(examples([self.row(1), self.row(4001)], horizons=(2000,)), [])
        rows = examples([self.row(1), self.row(2501)], horizons=(2000,))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['forecast_horizon'], 2500)

    def test_visible_units_bound_the_present_but_can_die_during_a_blackout(self):
        from tools.maxima_recon_belief import lower_bound
        self.assertEqual(lower_bound(self.row(1, 9, forecast_horizon=0), 'truth_warriors'), 9)
        self.assertEqual(lower_bound(self.row(1, 9, forecast_horizon=2000), 'truth_warriors'), 0)

    def test_portable_prediction_uses_integer_threshold_and_signed_leaf_values(self):
        from tools.maxima_recon_belief import predict_tree_model
        model = {'base': 4096, 'trees': [[[0, 2, 1, 2], [-1, -1024, 0, 0], [-1, 2048, 0, 0]]]}
        self.assertEqual(predict_tree_model(model, [2], 1024), 3)
        self.assertEqual(predict_tree_model(model, [3], 1024), 6)

    def test_duplicate_observations_fail_instead_of_double_weighting_history(self):
        from tools.maxima_recon_belief import histories
        with self.assertRaises(ValueError):
            histories([self.row(1), self.row(1)])

    def test_belief_memory_retains_only_fog_features_and_projects_without_refreshing_them(self):
        from tools.maxima_recon_belief import ObservationMemory, BELIEF_FEATURES
        memory = ObservationMemory()
        with self.assertRaises(ValueError):
            memory.project(1)
        first = memory.observe(self.row(1, 9, truth_warriors=9000, enemy_ai='secret'))
        projected = memory.project(5001)
        self.assertEqual(projected['forecast_horizon'], 5000)
        self.assertEqual(projected['tick'], 1)
        self.assertEqual(projected['visible_warriors'], 9)
        self.assertEqual(set(projected), set(BELIEF_FEATURES))
        self.assertNotIn('truth_warriors', memory.previous)
        self.assertNotIn('enemy_ai', memory.previous)
        self.assertEqual(memory.project(1), first)
        with self.assertRaises(ValueError):
            memory.project(0)

    def test_recalibration_quantile_respects_weights_and_discrete_mass(self):
        from tools.maxima_recon_belief import weighted_quantile
        self.assertEqual(weighted_quantile([0, 10, 100], [8, 1, 1], .9), 10)
        self.assertEqual(weighted_quantile([0, 10, 100], [8, 1, 1], .95), 100)
        with self.assertRaises(ValueError):
            weighted_quantile([1], [0], .9)

    def test_recalibration_keeps_the_best_estimate_and_ordered_bounds(self):
        from tools.maxima_recon_belief import apply_calibration, TARGETS
        raw = {t: [0, 10, 20, 30] for t in TARGETS}
        calibration = {'all': {t: [2, .5] for t in TARGETS}}
        row = dict(forecast_horizon=2000, visible_warriors_peak=10, enemy_ai='irrelevant')
        result = apply_calibration(calibration, row, raw)
        for target in TARGETS:
            self.assertEqual(result[target], [0, 10, 30, 30])
        self.assertEqual(raw['truth_warriors'], [0, 10, 20, 30])

    def test_marginal_beliefs_obey_population_and_power_constraints(self):
        from tools.maxima_recon_belief import coherent
        original = {'truth_warriors': [1, 5, 10, 20],
                    'truth_units': [0, 2, 30, 40], 'truth_power': [0, 100, 200, 300]}
        result = coherent(original)
        self.assertEqual(result['truth_units'], [1, 5, 30, 40])
        self.assertEqual(result['truth_power'], [1, 100, 200, 300])
        self.assertEqual(original['truth_units'][1], 2)

    def test_persistence_reference_preserves_the_cutoff_prediction_during_blackout(self):
        from tools.maxima_recon_belief import predict, TARGETS
        model = {'features': ['visible_warriors'], 'scale': 1024,
                 'models': {t: [{'base': 0, 'trees': []} for _ in range(4)] for t in TARGETS}}
        cutoff = self.row(1, 7, forecast_horizon=0)
        blackout = dict(cutoff, forecast_horizon=5000)
        self.assertEqual(predict(model, [cutoff]), predict(model, [blackout]))
        # A true forward model may predict that those units have died.
        model['features'].append('forecast_horizon')
        self.assertEqual(predict(model, [blackout])[0]['truth_warriors'], [0, 0, 0, 0])


if __name__ == '__main__': unittest.main()
