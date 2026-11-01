#!/usr/bin/env python3
"""Protocol, durable lease acceptance, transfer corruption and offline analysis tests."""
import base64
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.bundles import package_identity, platform_identity, register_bundle
from tools.tournaments.common import atomic_json, canonical, digest, file_hash, read_json, store_artifact
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job, validate_experiment, validate_job
from tools.tournaments.transfer import put_chunk, offset
from tools.tournaments.worker import Worker, installed_bundle, outcome_only, pack
from tools.tournaments.analysis import adjudicate, paired_effects, probability_scores, observations
from tools.tournaments.probability import summary, group_summary, group_score, stopping_threshold, SCORE_METRIC

FAKE = '''#!/usr/bin/env python3
import json, pathlib, sys
if sys.argv[1:2]==['help']:
 print(json.dumps({'schema_version':1,'cli_version':2,'commands':[]}));sys.exit(0)
if sys.argv[1:3]==['info','catalog']:
 print(json.dumps({'schema_version':1,'commands':['game','generate_map'],'ais':[],'generators':[]}));sys.exit(0)
if '--report' in sys.argv and sys.argv[sys.argv.index('--report')+1]=='slow':
 import time;time.sleep(10)
out=pathlib.Path(sys.argv[sys.argv.index('--output-dir')+1]);out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps({'schema_version':1,'status':'completed','job_type':'generate_map'}))
'''


class RuleJobs(unittest.TestCase):
    def test_custom_generators_require_and_pass_frozen_packages(self):
        from tools.tournaments.jobs import EngineJob
        request = job('generate_map','a'*64,config={'generator':'example:map'},seeds={'map':91})
        with self.assertRaisesRegex(ValueError,'frozen generator package'):
            EngineJob('generate_map').validate(request)
        request['inputs']={'generator-package-0':{'sha256':'b'*64}}
        adapter=EngineJob('generate_map');adapter.validate(request)
        command=adapter.command(request,{'directory':'/bundle','executable':'glob2'},'/attempt',{'generator-package-0':'/frozen.json'})
        self.assertEqual(command[command.index('--generator-package')+1],'/frozen.json')
        self.assertEqual(command[1:4],['map','study','example:map'])

    def test_rules_are_repeatable_sorted_and_new_game_only(self):
        from tools.tournaments.jobs import EngineJob
        adapter = EngineJob('game')
        request = job('game', 'a'*64, inputs={'map': {'sha256': 'b'*64}},
                      seeds={'game': 713}, config={'players': ['numbi', 'castor'],
                      'rules': {'peaceful': 1, 'noUpgrades': 1}})
        adapter.validate(request)
        command = adapter.command(request, {'directory': '/bundle', 'executable': 'glob2'},
                                  '/attempt', {'map': '/map'})
        rules = [command[i+1] for i, arg in enumerate(command) if arg == '--rule']
        self.assertEqual(rules, ['noUpgrades=1', 'peaceful=1'])
        request['inputs'] = {'save': {'sha256': 'b'*64}}
        request['seeds'] = {}
        request['config'] = {'rules': {'noUpgrades': 1}}
        with self.assertRaisesRegex(ValueError, 'cannot override'):
            adapter.validate(request)

    def test_probability_threshold_matches_engine_range(self):
        from tools.tournaments.jobs import EngineJob
        for threshold in (None, True, -1, 1, 500, 1001):
            request = job('game', 'a'*64, config={'generator': 15,
                'players': ['numbi', 'castor'], 'win_probability_permille': threshold},
                seeds={'map': 1, 'game': 1})
            with self.assertRaisesRegex(ValueError, '501'):
                EngineJob('game').validate(request)
        for threshold in (0, 501, 970, 1000):
            request = job('game', 'a'*64, config={'generator': 15,
                'players': ['numbi', 'castor'], 'win_probability_permille': threshold},
                seeds={'map': 1, 'game': 1})
            EngineJob('game').validate(request)

    def test_rules_require_integer_values(self):
        from tools.tournaments.jobs import EngineJob
        for rules in ([1], {'noUpgrades': True}, {'scarcity': '3'}, {1: 0}):
            request = job('game', 'a'*64, inputs={'map': {'sha256': 'b'*64}},
                          seeds={'game': 713}, config={'players': ['numbi', 'castor'], 'rules': rules})
            with self.assertRaisesRegex(ValueError, 'rules must map'):
                EngineJob('game').validate(request)


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

    def test_pipeline_connection_does_not_reseed_or_repair_ledger(self):
        self.coordinator.db.execute('DELETE FROM jobs WHERE id=?', (self.job['id'],))
        with patch.object(Coordinator, 'repair_exports', side_effect=AssertionError('lane repaired exports')):
            lane = Coordinator(self.coordinator.root, repair=False, manifest=self.coordinator.manifest)
        try:
            self.assertIs(lane.manifest, self.coordinator.manifest)
            self.assertEqual(lane.db.execute('SELECT count(*) FROM jobs').fetchone()[0], 0)
        finally:
            lane.close()

    def test_dispatch_streams_past_long_ineligible_prefix(self):
        fake_build = 'b' * 64
        self.coordinator.bundles[fake_build] = {'id': fake_build}
        for ordinal in range(300):
            blocked = job('generate_map', fake_build, seeds={'map': ordinal + 2}, config={'generator': 15})
            self.coordinator.db.execute("INSERT INTO jobs VALUES (?,?,?,'pending',NULL,NULL)",
                                        (blocked['id'], ordinal - 300, canonical(blocked).decode()))
        with patch('tools.tournaments.coordinator.eligible',
                   side_effect=lambda bundle, status, kind: bundle['id'] == self.bundle['id']):
            dispatched = self.coordinator.dispatch('one', self.status)
        self.assertEqual([attempt['job']['id'] for attempt in dispatched], [self.job['id']])

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

    def test_standard_elo_pack_omits_incidental_outputs(self):
        import shutil
        worker = Worker(self.root / 'lightweight-worker')
        try:
            attempt = self.coordinator.dispatch('one', self.status)[0]
            attempt['job']['type'] = 'game'
            attempt['job']['config']['players'] = ['numbi', 'castor']
            attempt['job']['seeds']['game'] = 1
            shutil.copytree(self.root / 'bundles' / self.bundle['id'], worker.root / 'bundles' / self.bundle['id'])
            worker.enqueue(attempt)
            directory = worker.root / 'attempts' / attempt['id']
            output = directory / 'output'; output.mkdir()
            (output / 'incidental.log').write_text('not an explicit output')
            atomic_json(directory / 'execution.json', {
                'category': 'success',
                'finished': time.time(),
                'result': {'schema_version': 1, 'status': 'completed'},
            })
            root = worker.root
        finally:
            worker.close()
        pack(root, attempt['id'])
        self.assertEqual(read_json(root / 'attempts' / attempt['id'] / 'record.json')['artifacts'], [])

    def test_installed_bundle_is_not_rehashed_per_job(self):
        path = self.root / 'bundles' / self.bundle['id']
        from tools.tournaments.bundles import inspect_bundle
        with patch('tools.tournaments.worker.inspect_bundle', wraps=inspect_bundle) as inspect:
            manifest = installed_bundle(path)
        inspect.assert_called_once_with(path, verify=False)
        self.assertEqual(manifest['id'], self.bundle['id'])
        self.assertEqual(manifest['directory'], str(path.resolve()))

    def test_outcome_result_keeps_elo_and_adjudication_evidence(self):
        validate_job(dict(self.job, outputs={'result': 'outcome'}))
        full = {
            'schema_version': 1, 'status': 'completed', 'ticks': 42,
            'termination': 'tick_cap', 'winning_teams': [],
            'teams': [{'team': 0, 'alliance': 1, 'outcome': 'unresolved', 'alive': True,
                       'eliminated_tick': -1, 'prestige': 7, 'units': 3, 'buildings': 2,
                       'warrior_attack': 4, 'warrior_hp': 5, 'warriors': 1,
                       'history': [1, 2, 3]}],
            'players': [{'player': 0, 'team': 0, 'ai': 'numbi', 'history': [4, 5]}],
            'telemetry': {'large': [0] * 100},
        }
        compact = outcome_only(full)
        self.assertEqual(compact['teams'][0]['prestige'], 7)
        self.assertEqual(compact['players'], [{'player': 0, 'team': 0, 'ai': 'numbi'}])
        self.assertNotIn('history', compact['teams'][0])
        self.assertNotIn('telemetry', compact)

    def test_persistent_daemon_hot_reloads_configure(self):
        """A `configure` RPC (used by `doctor`) runs against a fresh, short-lived
        Worker instance, distinct from the one a long-running `daemon` process
        holds for its whole lifetime. Without reload_config(), the daemon's own
        copy of self.config never saw the change -- slots silently stayed at
        whatever they were when the daemon started, no matter how many times
        `doctor` rewrote host.json (this is exactly what happened in production
        on 2026-09-17: three `doctor` redeploys never took effect until the
        daemon processes were manually killed and restarted)."""
        daemon_side = Worker(self.root / 'reload-worker')
        try:
            self.assertEqual(daemon_side.config['slots'], daemon_side.config['slots'])
            original_slots = daemon_side.config['slots']
            rpc_side = Worker(self.root / 'reload-worker')
            try:
                rpc_side.configure({'slots': original_slots + 7})
            finally:
                rpc_side.close()
            # The persistent instance must not have silently mutated in lockstep.
            self.assertEqual(daemon_side.config['slots'], original_slots)
            daemon_side.tick()
            self.assertEqual(daemon_side.config['slots'], original_slots + 7)
        finally:
            daemon_side.close()


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def install(self, name, pid, package_id='pkg', slots=3, running=0, queued=0):
        from tools.tournaments.common import database
        directory = self.root / name
        worker_root = directory / 'workers' / package_id
        worker_root.mkdir(parents=True)
        atomic_json(worker_root / 'daemon.json', {'pid': pid, 'package_id': package_id})
        atomic_json(worker_root / 'host.json', {'slots': slots})
        db = database(worker_root / 'queue.sqlite')
        db.executescript('CREATE TABLE queue (state TEXT NOT NULL)')
        db.executemany('INSERT INTO queue VALUES (?)', [('running',)] * running + [('queued',)] * queued)
        db.commit(); db.close()
        return directory

    def test_discover_finds_installs_and_reports_liveness(self):
        from tools.tournaments.inventory import discover
        from tools.tournaments.transport import Transport
        import os
        live = self.install('mine', os.getpid(), running=2, queued=5)
        dead_pid = 2**30 - 1  # exceedingly unlikely to be a real running pid
        dead = self.install('abandoned', dead_pid)
        transport = Transport({'name': 'localhost', 'transport': 'local', 'directory': str(live)}, '/dev/null')
        found = {entry['directory']: entry for entry in discover(transport, root=str(self.root))}
        self.assertEqual(set(found), {str(live), str(dead)})
        self.assertTrue(found[str(live)]['alive'])
        self.assertEqual(found[str(live)]['running'], 2)
        self.assertEqual(found[str(live)]['queued'], 5)
        self.assertFalse(found[str(dead)]['alive'])

    def test_audit_flags_dead_and_idle_installs_as_stale(self):
        from tools.tournaments.inventory import audit
        import os
        self.install('mine', os.getpid(), running=2)
        dead = self.install('abandoned', 2**30 - 1)
        old = time.time() - 999999
        os.utime(dead / 'workers' / 'pkg' / 'queue.sqlite', (old, old))
        # Alive but idle for days: the coordinator that owned it is gone, but
        # nothing killed the daemon itself -- exactly the "orphaned run that
        # never recovers" case, distinct from an outright-dead process.
        idle_alive = self.install('idle-forever', os.getpid())
        os.utime(idle_alive / 'workers' / 'pkg' / 'queue.sqlite', (old, old))
        hosts = [{'name': 'localhost', 'transport': 'local', 'directory': str(self.root / 'mine')}]
        report = audit(hosts, stale_hours=1, root=str(self.root))
        installs = {i['directory']: i for i in report[0]['installs']}
        self.assertFalse(installs[str(self.root / 'mine')]['stale'])
        self.assertTrue(installs[str(dead)]['stale'])
        self.assertTrue(installs[str(idle_alive)]['stale'])
        self.assertTrue(installs[str(idle_alive)]['alive'])  # stale despite being alive
        self.assertTrue(installs[str(self.root / 'mine')]['own'])
        self.assertFalse(installs[str(dead)]['own'])

    def test_reap_requires_confirm_and_only_stops_named_pid(self):
        from tools.tournaments.inventory import reap
        import subprocess, sys
        worker_root = self.root / 'other-session' / 'workers' / 'pkg'
        process = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)',
                                    'daemon', str(worker_root)])
        try:
            directory = self.install('other-session', process.pid)
            host = {'name': 'localhost', 'transport': 'local', 'directory': str(self.root / 'mine')}
            dry_run = reap(host, str(directory), confirm=False, root=str(self.root))
            self.assertEqual(dry_run['action'], 'dry-run')
            self.assertIsNone(process.poll())  # still alive: dry run touched nothing
            result = reap(host, str(directory), confirm=True, root=str(self.root))
            self.assertEqual(result['action'], 'signalled')
            process.wait(timeout=5)
            self.assertIsNotNone(process.poll())
        finally:
            if process.poll() is None:
                process.kill(); process.wait()

    def test_reap_refuses_a_reused_pid(self):
        import subprocess
        from tools.tournaments.inventory import reap
        process = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
        try:
            directory = self.install('reused', process.pid)
            host = {'name': 'localhost', 'transport': 'local', 'directory': str(self.root / 'mine')}
            with self.assertRaisesRegex(ConnectionError, 'selected worker daemon'):
                reap(host, str(directory), confirm=True, root=str(self.root))
            self.assertIsNone(process.poll())
        finally:
            process.kill()
            process.wait()

    def test_reap_unknown_directory_raises(self):
        from tools.tournaments.inventory import reap
        self.install('mine', 999999999)
        host = {'name': 'localhost', 'transport': 'local', 'directory': str(self.root / 'mine')}
        with self.assertRaises(ValueError):
            reap(host, str(self.root / 'never-existed'), confirm=True, root=str(self.root))


class AnalysisTests(unittest.TestCase):
    def team(self, number, **extra):
        return {'team':number,'alliance':number+1,'alive':True,'outcome':'unresolved',
                'prestige':0,'units':4,'buildings':1,'warrior_attack':0,'warrior_hp':0,
                'warriors':0,'eliminated_tick':-1, **extra}

    def test_cap_policies_ties_and_engine_authority(self):
        from tools.tournaments.analysis import adjudicate
        result={'termination':'tick_cap','winning_teams':[],
                'teams':[self.team(0,prestige=2),self.team(1,warrior_attack=10)]}
        self.assertEqual(adjudicate(result,'prestige')['winners'],[0])
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
        self.assertEqual(adjudicate(result,'prestige',alliances=True)['placements'],{1:1.5,2:1.5})
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
        bundle={'id':'a'*64,'capabilities':{'ais':[{'id':1,'name':'numbi'},{'id':2,'name':'castor'}], 'generators':[{'method':15,'editorOnly':False}]}}
        config={'id':'balance','ais':['numbi','castor'],'formats':['1v1'],'map_seeds':[1,2]}
        manifest=Planner('ai_comparison',config,[bundle]).plan()
        games=[j for j in manifest['jobs'] if j['type']=='game']
        self.assertEqual(len(games),8)
        for seed in (1,2):
            block=[j for j in games if j['labels']['map_seed']==seed]
            self.assertEqual(sum(j['config']['players'][0]=='numbi' for j in block),2)
        for j in games:
            self.assertEqual(j['outputs'], {})  # standard Elo needs only the result
        explicit=Planner('ai_comparison',{**config,'outputs':{}},[bundle]).plan()
        for j in explicit['jobs']:
            if j['type']=='game': self.assertEqual(j['outputs'], {})  # caller's own outputs, not overridden
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

    def test_the_win_probability_rule_defaults_to_95_and_can_be_disabled(self):
        """The planner freezes the threshold, including explicit opt-outs."""
        from tools.tournaments.experiments import Planner
        from tools.tournaments.jobs import EngineJob
        bundle = {'id': 'a'*64, 'capabilities': {
            'ais': [{'id': 1, 'name': 'numbi'}, {'id': 2, 'name': 'castor'}],
            'generators': [{'method': 15, 'editorOnly': False}]}}
        base = {'id': 'sample', 'sample_games': 5, 'sample_seed': 3}
        plain = Planner('ai_comparison', dict(base), [bundle]).plan()
        for job in plain['jobs']:
            self.assertEqual(job['config']['win_probability_permille'], 950)
        asked = Planner('ai_comparison', dict(base, win_probability_permille=970), [bundle]).plan()
        games = [j for j in asked['jobs'] if j['type'] == 'game']
        self.assertTrue(games)
        for job in games:
            self.assertEqual(job['config']['win_probability_permille'], 970)
        # And the flag has to survive the trip into the engine's command line,
        # which is the only part the engine actually sees.
        bundle_directory = {'directory': '/bundle', 'executable': 'glob2'}
        arguments = EngineJob('game').command(games[0], bundle_directory, '/tmp/attempt', {})
        self.assertIn('--win-probability', arguments)
        self.assertEqual(arguments[arguments.index('--win-probability') + 1], '970')
        disabled = Planner('ai_comparison', dict(base, win_probability_permille=0), [bundle]).plan()
        plain_game = next(j for j in disabled['jobs'] if j['type'] == 'game')
        disabled_args = EngineJob('game').command(plain_game, bundle_directory, '/tmp/attempt', {})
        self.assertEqual(disabled_args[disabled_args.index('--win-probability') + 1], '0')
        rules_off = Planner('ai_comparison', dict(base, rules={'winProbabilityPermille': 0}), [bundle]).plan()
        self.assertTrue(all(j['config']['win_probability_permille'] == 0 for j in rules_off['jobs']))
        with self.assertRaisesRegex(ValueError, 'conflicting'):
            Planner('ai_comparison', dict(base, win_probability_permille=950,
                    rules={'winProbabilityPermille': 0}), [bundle]).plan()
        with self.assertRaisesRegex(ValueError, '501'):
            Planner('ai_comparison', dict(base, win_probability_permille=None), [bundle]).plan()

    def test_sample_games_uses_standard_duels_via_inline_generation(self):
        """The standard Elo cohort uses 128x128 duels. Sampling remains the one
        place ai_comparison departs from an exhaustive cross product."""
        from tools.tournaments.experiments import Planner
        bundle = {'id': 'a'*64, 'capabilities': {
            'ais': [{'id': 1, 'name': 'numbi'}, {'id': 2, 'name': 'castor'},
                    {'id': 3, 'name': 'warrush'}, {'id': 4, 'name': 'econo'}],
            'generators': [{'method': 15, 'editorOnly': False}, {'method': 21, 'editorOnly': False},
                           {'method': 0, 'editorOnly': True}]}}
        config = {'id': 'sample', 'sample_games': 40, 'sample_seed': 7}
        manifest = Planner('ai_comparison', config, [bundle]).plan()
        games = [j for j in manifest['jobs'] if j['type'] == 'game']
        self.assertEqual(len(games), 40)
        formats, generators = set(), set()
        for j in games:
            self.assertNotIn('map', j['inputs'])  # inline generation, no generate_map dependency
            self.assertEqual(j['depends_on'], [])
            self.assertIn('generator', j['config'])
            self.assertEqual(j['outputs'], {})  # standard Elo needs only the result
            formats.add(j['labels']['format'])
            generators.add(j['config']['generator'])
            n = 2 if j['labels']['format'] == '1v1' else 4
            self.assertEqual(len(j['config']['players']), n)
        self.assertEqual(formats, {'1v1'})
        self.assertEqual(generators, {15, 21})  # all playable generators, never editor-only
        for j in games:
            self.assertEqual(j['config']['params']['width'], 7)
            self.assertEqual(j['config']['params']['height'], 7)
        # Same seed is reproducible; a different one draws a different sample.
        again = Planner('ai_comparison', config, [bundle]).plan()
        self.assertEqual([j['id'] for j in manifest['jobs']], [j['id'] for j in again['jobs']])
        different = Planner('ai_comparison', {**config, 'sample_seed': 8}, [bundle]).plan()
        self.assertNotEqual([j['id'] for j in manifest['jobs']], [j['id'] for j in different['jobs']])
        # Restricting generators/sizes/formats is respected.
        narrow = Planner('ai_comparison', {**config, 'generators': [21], 'sizes': [{'width': 7, 'height': 8}],
                                           'formats': ['ffa'], 'sample_games': 5}, [bundle]).plan()
        for j in [job for job in narrow['jobs'] if job['type'] == 'game']:
            self.assertEqual(j['config']['generator'], 21)
            self.assertEqual(j['config']['params']['width'], 7)
            self.assertEqual(j['config']['params']['height'], 8)
            self.assertEqual(j['labels']['format'], 'ffa')

    def test_sampled_pairs_keep_parameter_sampling_and_explicit_rules(self):
        from tools.tournaments.experiments import Planner
        bundle = {'id': 'a'*64, 'capabilities': {
            'ais': [{'id': 1, 'name': 'numbi'}, {'id': 2, 'name': 'castor'}],
            'generators': [{'method': 15, 'editorOnly': False}]}}
        design = {'id': 'sample', 'sample_games': 2, 'balanced_duels': True,
                  'randomize_parameters': True, 'rules': {'peaceful': 1}}
        with patch.object(Planner, 'parameter_samples', return_value=[{'fruit': 7}]):
            games = Planner('ai_comparison', design, [bundle]).plan()['jobs']
        self.assertEqual(games[0]['config']['params'], games[1]['config']['params'])
        self.assertEqual(games[0]['config']['params']['fruit'], 7)
        self.assertEqual(games[0]['config']['rules'], {'peaceful': 1})
        for count in (0, -1, True):
            with self.assertRaisesRegex(ValueError, 'positive integer'):
                Planner('ai_comparison', dict(design, sample_games=count), [bundle]).plan()

    def test_balanced_duels_cover_all_generators_and_swapped_pairs(self):
        from collections import Counter, defaultdict
        from tools.tournaments.experiments import Planner
        bundle = {'id': 'a'*64, 'capabilities': {
            'ais': [{'id': i+1, 'name': name} for i, name in enumerate(
                ['numbi', 'castor', 'warrush', 'econo', 'cortex', 'cabino', 'nicowar', 'maxima'])],
            'generators': [{'method': i, 'editorOnly': i == 0} for i in range(67)]}}
        config = {'id': 'balanced', 'sample_games': 10000, 'sample_seed': 20260920,
                  'balanced_duels': True, 'generator_overrides': {'52': {'slant': 0}}}
        games = Planner('ai_comparison', config, [bundle]).plan()['jobs']
        self.assertEqual(len(games), 10000)
        counts = Counter(g['config']['generator'] for g in games)
        self.assertEqual(set(counts), set(range(1, 67)))
        self.assertEqual(Counter(counts.values()), {150: 16, 152: 50})
        matchups = Counter(tuple(sorted(g['config']['players'])) for g in games)
        self.assertEqual(len(matchups), 28)
        self.assertEqual(set(matchups.values()), {356, 358})
        blocks = defaultdict(list)
        for game in games:
            blocks[game['labels']['block']].append(game)
            self.assertEqual(game['config']['params'], {'width': 7, 'height': 7, 'teams': 2,
                **({'slant': 0} if game['config']['generator'] == 52 else {})})
        self.assertEqual(len(blocks), 5000)
        for a, b in blocks.values():
            self.assertEqual(a['seeds'], b['seeds'])
            self.assertEqual(a['build'], b['build'])
            self.assertEqual(a['config']['players'], b['config']['players'][::-1])
        self.assertEqual(games, Planner('ai_comparison', config, [bundle]).plan()['jobs'])
        with self.assertRaises(ValueError):
            Planner('ai_comparison', dict(config, sample_games=9999), [bundle]).plan()

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


class ParameterSampling(unittest.TestCase):
    def bundle(self):
        return {'id': 'a'*64, 'capabilities': {'generators': [{'method': 15, 'controls': [
            {'id': k, 'values': [1, 2], 'searchValues': [2]}
            for k in ('width', 'height', 'teams', 'workers', 'roads')
        ] + [{'id': 'shape', 'values': [0, 1, 2], 'searchValues': [0, 1]}]}]}}

    def test_domains_and_reproducibility(self):
        from tools.tournaments.model import generator_samples
        catalog = self.bundle()['capabilities']
        draws = generator_samples(catalog, 15, 64, 123)
        self.assertEqual(draws, generator_samples(catalog, 15, 64, 123))
        self.assertEqual({v['roads'] for v in draws}, {2})
        self.assertEqual({v['shape'] for v in draws}, {0, 1})
        self.assertTrue(all(set(v) == {'roads', 'shape'} for v in draws))
        legal = generator_samples(catalog, 15, 64, 123, 'legal')
        self.assertEqual({v['shape'] for v in legal}, {0, 1, 2})
        self.assertEqual({v['roads'] for v in legal}, {1, 2})
        with self.assertRaises(ValueError): generator_samples(catalog, 15, 1, 1, 'unknown')
        with self.assertRaises(ValueError): generator_samples(catalog, 99, 1, 1)
        for values in ([], [2, 2], [3], [True], None):
            control = catalog['generators'][0]['controls'][-1]
            if values is None: control.pop('searchValues', None)
            else: control['searchValues'] = values
            with self.assertRaises(ValueError): generator_samples(catalog, 15, 1, 1)

    def test_balancing_maps_shared_and_overrides_preserved(self):
        from tools.tournaments.experiments import Planner
        config = {'id': 'sampled', 'ais': ['cortex', 'maxima'], 'formats': ['1v1'],
                  'map_seeds': [1, 2], 'randomize_parameters': True,
                  'generator_params': {'width': 6, 'height': 8, 'workers': 7, 'roads': 1}}
        manifest = Planner('ai_comparison', config, [self.bundle()]).plan()
        self.assertEqual(manifest, Planner('ai_comparison', config, [self.bundle()]).plan())
        maps = [j for j in manifest['jobs'] if j['type'] == 'generate_map']
        self.assertEqual(len(maps), 2)
        for value in maps:
            self.assertEqual(value['config']['params'] | {'shape': 0},
                             config['generator_params'] | {'teams': 2, 'shape': 0})
            games = [j for j in manifest['jobs'] if j['depends_on'] == [value['id']]]
            self.assertEqual(len(games), 4)

    def test_stress_catalog_and_explicit_extremes(self):
        from tools.tournaments.experiments import Planner
        config = {'id': 'stress', 'randomize_parameters': True, 'samples': 16,
                  'generator_params': {'width': 6}, 'sample': {'roads': [1], 'shape': [2]}}
        manifest = Planner('generator_stress', config, [self.bundle()]).plan()
        sampled = [j for j in manifest['jobs'] if j['labels']['variant'].startswith('catalog-')]
        self.assertEqual(len(sampled), 16)
        self.assertTrue(all(j['config']['params']['roads'] == 2 for j in sampled))
        self.assertTrue(all(j['config']['params']['width'] == 6 for j in sampled))
        explicit = [j for j in manifest['jobs'] if j['labels']['variant'].startswith('sample-')]
        self.assertTrue(all(j['config']['params'] == {'width': 6, 'roads': 1, 'shape': 2} for j in explicit))
        config['parameter_domain'] = 'legal'
        manifest = Planner('generator_stress', config, [self.bundle()]).plan()
        sampled = [j for j in manifest['jobs'] if j['labels']['variant'].startswith('catalog-')]
        self.assertIn(2, {j['config']['params']['shape'] for j in sampled})


def _probability_team(n, chance=500, area=1000000, start=0, end=1000, **extra):
    return dict(team=n, alliance=n, alive=True, outcome='unresolved', prestige=100-n,
                units=50, buildings=10, eliminated_tick=-1,
                win_probability=dict(model='test-model', permille=chance, start_tick=start,
                                     end_tick=end, area_twice_permille_ticks=area),
                win_probability_history=[[start, int(area/(end-start)-chance) if end>start else chance], [end,chance]], **extra)


def _probability_row(name='baseline', mean=500, block='one', build='build'):
    teams=[_probability_team(0, mean, mean*2000), _probability_team(1, 1000-mean, (1000-mean)*2000)]
    result=dict(ticks=1000,termination='tick_cap',teams=teams,winning_teams=[])
    return dict(build=build, block=block, pair=block, held_out=False, variant=name,
                format='1v1', competitors=['subject', 'rival'], groups=[0, 1], subject_group=0,
                ticks=1000, placements=[1, 2], probabilities=[summary(t) for t in teams],
                trajectory_scores=[group_score([t],result,{'ticks':1000}) for t in teams])


class ProbabilityTests(unittest.TestCase):
    def test_actual_stopping_rule_takes_precedence_including_disabled_rules(self):
        result={'resolved': {'rules': {'winProbabilityPermille': 0}}, 'win_probability_permille': 950}
        config={'win_probability_permille': 970, 'rules': {'winProbabilityPermille': 990}}
        self.assertEqual(stopping_threshold(result, config), 0)
        result.pop('resolved')
        self.assertEqual(stopping_threshold(result, config), 950)
        self.assertEqual(stopping_threshold({}, config), 970)
        config.pop('win_probability_permille')
        self.assertEqual(stopping_threshold({}, config), 990)
        self.assertIsNone(stopping_threshold({}, {}))

    def test_terminal_coverage_is_required_for_scoring_and_cap_adjudication(self):
        stale=_probability_team(0, chance=900, area=500000, end=500)
        other=_probability_team(1, chance=100)
        self.assertFalse(summary(stale, expected_end_tick=1000)['whole_game'])
        result=dict(termination='tick_cap', ticks=1000, winning_teams=[], teams=[stale, other],
                    players=[{'team':0,'ai':'cortex'}, {'team':1,'ai':'castor'}])
        with self.assertRaisesRegex(ValueError, 'terminal tick'):
            adjudicate(result)

        # A fresh final prediction can adjudicate a cap even if its opening is missing.
        partial=_probability_team(0, chance=900, area=500000, start=500)
        result['teams'][0]=partial
        self.assertEqual(adjudicate(result)['winners'], [0])
        self.assertFalse(summary(partial, expected_end_tick=1000)['whole_game'])

        # Missing AUC coverage cannot invalidate an engine-declared winner.
        result['winning_teams']=[0]
        result['teams']=[stale, dict(other)]
        result['teams'][1].pop('win_probability')
        record={'category':'success', 'result':result, 'job':job('game','a'*64)}
        row=observations([record], 'win_probability')[0]
        self.assertEqual(row['winners'], [0])
        self.assertTrue(row['engine_outcome'])
        self.assertTrue(all(s['scored_games']==0 for s in probability_scores([row], 10)))

    def test_same_ai_ablation_keeps_subject_score_separate_from_opponent(self):
        row=_probability_row('improved', 510)
        row['competitors']=['cortex','cortex']
        scores=probability_scores([row], draws=10)
        self.assertEqual(len(scores), 2)
        subject=next(s for s in scores if s['role']=='subject')
        opponent=next(s for s in scores if s['role']=='opponent')
        self.assertAlmostEqual(subject['mean'], .51)
        self.assertAlmostEqual(opponent['mean'], .49)
        self.assertEqual((subject['wins'], opponent['losses']), (1,1))

    def test_paired_probability_intervals_keep_same_seed_rotations_together(self):
        rows=[]
        for rotation in range(2):
            for variant in ('baseline', 'improved'):
                row=_probability_row(variant, 510, 'one-seed')
                row['pair']='rotation-'+str(rotation)
                rows.append(row)
        effect=next(s for s in paired_effects(rows, 100) if s['metric']==SCORE_METRIC)
        self.assertEqual((effect['pairs'], effect['blocks']), (2,1))
        self.assertIsNone(effect['interval'])

    def test_default_reanalysis_exports_probability_beside_outcomes_without_logs(self):
        from tools.tournaments.model import job
        from tools.tournaments.analysis import reanalyze
        request = job('game', 'a'*64, seeds={'game': 42},
                      config={'players': ['castor', 'cortex'], 'ticks':1000, 'win_probability_permille': 950},
                      labels={'format': '1v1', 'block': 'paired-map'})
        result = dict(termination='tick_cap', ticks=1000, winning_teams=[],
                      teams=[_probability_team(0, 300), _probability_team(1, 700)],
                      players=[{'team':0,'ai':'castor'}, {'team':1,'ai':'cortex'}])
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'results').mkdir()
            (root/'experiment.json').write_text(json.dumps(dict(id='probability', jobs=[request])))
            record=dict(id='attempt', host='local', category='success', job=request, result=outcome_only(result, request['config']), artifacts=[])
            (root/'results'/(request['id']+'.json')).write_text(json.dumps(record))
            report=reanalyze(root, draws=10)
            self.assertEqual(report['policy']['name'], 'win_probability')
            winner=next(s for s in report['probability_scores'] if s['competitor']=='cortex')
            self.assertEqual((winner['wins'], winner['losses'], winner['draws']), (1,0,0))
            self.assertAlmostEqual(winner['mean'], .1+.4/math.log(3))
            self.assertTrue((root/'reports/win_probability/probability_scores.csv').is_file())
            text=(root/'reports/win_probability/report.md').read_text()
            self.assertIn('Adjusted win-probability trajectory score', text)
            self.assertEqual(report['score_definition']['coefficient'], 2)
            self.assertIn('W / L / D', text)

    def test_cap_uses_final_probability_instead_of_economy(self):
        result=dict(termination='tick_cap', ticks=1000, winning_teams=[], teams=[_probability_team(0, 300), _probability_team(1, 700)])
        self.assertEqual(adjudicate(result)['winners'], [1])
        self.assertEqual(adjudicate(result, 'prestige')['winners'], [0])
        result['teams'][0]['win_probability']['permille'] = 500
        result['teams'][1]['win_probability']['permille'] = 500
        self.assertEqual(adjudicate(result)['placements'], {0: 1.5, 1: 1.5})
        result['winning_teams'] = [0]
        self.assertEqual(adjudicate(result)['winners'], [0])

    def test_missing_is_unavailable_and_legacy_policy_is_explicit(self):
        old=_probability_team(0); old.pop('win_probability')
        self.assertIsNone(summary(old))
        result=dict(termination='tick_cap', teams=[old, _probability_team(1)])
        with self.assertRaisesRegex(ValueError, 'explicit legacy policy'):
            adjudicate(result)
        self.assertEqual(adjudicate(result, 'prestige')['winners'], [0])

    def test_alliances_count_once_and_do_not_sum_chances(self):
        a,b,c=_probability_team(0, 400),_probability_team(1, 400),_probability_team(2, 600)
        a['alliance']=b['alliance']=10
        self.assertEqual(group_summary([a,b])['final'], .4)
        self.assertEqual(adjudicate(dict(termination='tick_cap', ticks=1000, teams=[a,b,c]), alliances=True)['winners'], [2])
        b['win_probability']['permille']=401
        with self.assertRaisesRegex(ValueError, 'inconsistent alliance'):
            group_summary([a,b])

    def test_compaction_keeps_exact_area_but_drops_large_histories(self):
        a=_probability_team(0); a['win_probability_history']=[[0,500],[1000,500]]
        result=dict(status='completed', ticks=1000, teams=[a], players=[], termination='tick_cap',
                    resolved={'rules': {'winProbabilityPermille': 950}})
        compact=outcome_only(result)
        self.assertEqual(compact['teams'][0]['win_probability'], a['win_probability'])
        self.assertNotIn('win_probability_history', compact['teams'][0])
        self.assertEqual(summary(compact['teams'][0])['mean'], .5)
        self.assertEqual(compact['win_probability_permille'], 950)

    def test_time_normalization_and_partial_or_mixed_coverage(self):
        a=summary(_probability_team(0, area=1000000, end=1000))
        b=summary(_probability_team(0, area=2000000, end=2000))
        self.assertEqual(a['mean'], b['mean'])
        self.assertEqual(b['area'], 2*a['area'])
        partial=summary(_probability_team(0, area=500000, start=500, end=1000))
        self.assertEqual(partial['mean'], .5)
        self.assertFalse(partial['whole_game'])
        mixed=_probability_team(0); mixed['win_probability']['model']='mixed'
        self.assertFalse(summary(mixed)['whole_game'])
        for invalid in (_probability_team(0, chance=-1), _probability_team(0, area=2000001), _probability_team(0, start=1001)):
            with self.assertRaises(ValueError): summary(invalid)

    def test_stopping_rules_separate_cohorts_and_prevent_misleading_paired_effects(self):
        baseline=_probability_row()
        candidate=_probability_row('improved', 510)
        baseline['win_probability_permille']=950
        candidate['win_probability_permille']=0
        self.assertFalse(any(s['metric']==SCORE_METRIC for s in paired_effects([baseline,candidate], 10)))
        candidate['variant']='baseline'
        scores=probability_scores([baseline,candidate], 10)
        subject=[s for s in scores if s['competitor']=='subject']
        self.assertEqual({s['win_probability_permille'] for s in subject}, {0,950})
        self.assertEqual([s['games'] for s in subject], [1,1])
        rows=[]
        for block, threshold in (('one', 950), ('two', 0)):
            for variant in ('baseline', 'improved'):
                row=_probability_row(variant, 510, block)
                row['win_probability_permille']=threshold
                rows.append(row)
        effects=[s for s in paired_effects(rows, 10) if s['metric']==SCORE_METRIC]
        self.assertEqual({s['win_probability_permille'] for s in effects}, {0,950})
        self.assertTrue(all(s['pairs']==1 for s in effects))

    def test_score_detects_small_paired_improvement_without_changed_wins(self):
        rows=[_probability_row(block='one'), _probability_row('improved', 510, 'one'),
              _probability_row(block='two'), _probability_row('improved', 520, 'two')]
        effects=paired_effects(rows, draws=100, seed=42)
        probability=next(x for x in effects if x['metric']==SCORE_METRIC)
        self.assertAlmostEqual(probability['mean_effect'], .015)
        placement=next(x for x in effects if x['metric']=='placement')
        self.assertEqual(placement['mean_effect'], 0)
        scores=probability_scores(rows, draws=100, seed=42)
        improved=next(x for x in scores if x['competitor']=='subject' and x['variant']=='improved')
        self.assertEqual(improved['blocks'], 2)
        self.assertEqual((improved['wins'], improved['losses'], improved['draws']), (2,0,0))
        self.assertEqual(improved['scored_games'], improved['games'])
        self.assertAlmostEqual(improved['mean'], .515)
        self.assertEqual(scores, probability_scores(rows, draws=100, seed=42))
        # Preserve build cohorts rather than silently pool different binaries.
        rows.append(_probability_row('improved', 900, 'three', build='other'))
        self.assertEqual(len(probability_scores(rows, 0)), 6)



def _adjusted_team(n=0, points=None, outcome='unresolved', alliance=None):
    p=points or [[0,500],[1000,500]]
    area=sum((a[1]+b[1])*(b[0]-a[0]) for a,b in zip(p,p[1:]))
    return dict(team=n, alliance=n if alliance is None else alliance, alive=outcome!='lost',
                eliminated_tick=-1, outcome=outcome, prestige=0,units=10,buildings=1,
                win_probability_history=p,win_probability=dict(model='fixture',permille=p[-1][1],
                start_tick=p[0][0],end_tick=p[-1][0],area_twice_permille_ticks=area))


def _adjusted_result(teams=None, end=1000, horizon=1000, termination='tick_cap', winners=None):
    return dict(schema_version=1,status='completed',ticks=end,termination=termination,
                winning_teams=winners or [],teams=teams or [_adjusted_team(),_adjusted_team(1)],
                players=[dict(team=0,ai='maxima'),dict(team=1,ai='castor')],
                resolved=dict(tick_limit=horizon,rules=dict(winProbabilityPermille=950)))


class TrajectoryScoreTests(unittest.TestCase):
    def test_linear_rise_matches_independent_quadrature(self):
        t=_adjusted_team(points=[[0,200],[400,800],[1000,600]])
        r=_adjusted_result([t]);v=group_score([t],r)
        # Midpoint numerical integration, independent of the analytic reader.
        steps=50000;delta=1000/steps
        def probability(x):return .2+.6*x/400 if x<400 else .8-.2*(x-400)/600
        expected=sum(probability((i+.5)*delta)*2/(1000+2*(i+.5)*delta)/math.log(3)*delta for i in range(steps))
        self.assertAlmostEqual(v['score'],expected,places=9)
        self.assertEqual(v['metric'],SCORE_METRIC)
        self.assertEqual(v['coefficient'],2)

    def test_faster_real_wins_receive_more_credit_for_equal_earlier_strength(self):
        def evaluate(ticks,termination='engine_end',outcome='won'):
            t=_adjusted_team(points=[[0,600],[ticks,600]],outcome=outcome)
            return group_score([t],_adjusted_result([t],end=ticks,horizon=1000,termination=termination,winners=[0] if outcome=='won' else []))
        early=evaluate(200);late=evaluate(600);cap=evaluate(1000,'tick_cap','unresolved');loss=evaluate(200,'engine_end','lost')
        self.assertGreater(early['score'],late['score']);self.assertGreater(late['score'],cap['score']);self.assertGreater(cap['score'],loss['score'])
        self.assertAlmostEqual(early['score'],1-.4*math.log(1.4)/math.log(3))
        self.assertEqual(early['outcome_source'],'engine');self.assertEqual(loss['outcome_credit'],0)
        self.assertIsNone(cap['outcome_credit']);self.assertEqual(cap['terminal_weight'],0)

    def test_probability_stop_credits_full_predicted_outcome_and_tags_it(self):
        a=_adjusted_team(points=[[0,500],[300,960]],outcome='won');b=_adjusted_team(1,[[0,500],[300,40]],'lost')
        r=_adjusted_result([a,b],end=300,horizon=1000,termination='win_probability',winners=[0])
        winner=group_score([a],r);loser=group_score([b],r)
        self.assertEqual((winner['outcome_credit'],loser['outcome_credit']),(1,0))
        self.assertEqual(winner['outcome_source'],'probability_stop')
        self.assertAlmostEqual(winner['score']+loser['score'],1)
        actual=copy.deepcopy(r);actual['termination']='engine_end'
        self.assertEqual(group_score([a],actual)['score'],winner['score'])
        self.assertEqual(group_score([a],actual)['outcome_source'],'engine')

    def test_shared_winners_each_receive_full_credit_and_alliance_counts_once(self):
        a=_adjusted_team(points=[[0,400],[500,400]],alliance=7);b=_adjusted_team(1,[[0,400],[500,400]],alliance=7)
        r=_adjusted_result([a,b],end=500,horizon=1000,termination='engine_end',winners=[0,1])
        self.assertEqual(group_score([a,b],r)['score'],group_score([a],r)['score'])
        self.assertEqual(group_score([b],r)['outcome_credit'],1)
        # A group wins when one member is declared: do not sum their probabilities.
        r['winning_teams']=[0]
        self.assertEqual(group_score([a,b],r)['outcome_credit'],1)

    def test_compaction_preserves_score_and_actual_resolved_horizon(self):
        a=_adjusted_team(points=[[0,300],[200,800],[500,900]],outcome='won')
        r=_adjusted_result([a],end=500,horizon=1000,termination='win_probability',winners=[0])
        full=group_score([a],r);compact=outcome_only(r,{'ticks':2000})
        self.assertNotIn('win_probability_history',compact['teams'][0])
        self.assertEqual(compact['teams'][0]['win_probability'],a['win_probability'])
        self.assertEqual(group_score(compact['teams'],compact,{'ticks':2000}),full)
        self.assertEqual(outcome_only(compact),compact)

    def test_old_full_history_scores_but_old_compact_cannot_invent_log_area(self):
        r=_adjusted_result();self.assertTrue(group_score(r['teams'][:1],r)['available'])
        del r['teams'][0]['win_probability_history']
        v=group_score(r['teams'][:1],r)
        self.assertFalse(v['available']);self.assertIsNone(v['score']);self.assertEqual(v['unavailable_reason'],'missing_history')

    def test_partial_mixed_stale_or_missing_horizons_are_unavailable(self):
        for points in ([[200,500],[1000,500]],[[0,500],[900,500]]):
            t=_adjusted_team(points=points);self.assertFalse(group_score([t],_adjusted_result([t]))['available'])
        t=_adjusted_team();t['win_probability']['model']='mixed'
        self.assertFalse(group_score([t],_adjusted_result([t]))['available'])
        r=_adjusted_result();del r['resolved']
        self.assertEqual(group_score(r['teams'][:1],r)['unavailable_reason'],'missing_horizon')
        self.assertTrue(group_score(r['teams'][:1],r,{'ticks':1000})['available'])
        self.assertEqual(group_score([_adjusted_team()],_adjusted_result(horizon=500))['unavailable_reason'],'horizon_before_end')

    def test_unknown_early_end_does_not_receive_invented_outcome_credit(self):
        t=_adjusted_team(points=[[0,500],[200,500]])
        r=_adjusted_result([t],end=200,horizon=1000,termination='tick_cap')
        self.assertEqual(group_score([t],r)['unavailable_reason'],'unresolved_before_horizon')
        for termination in ('engine_end','win_probability'):
            r['termination']=termination
            self.assertEqual(group_score([t],r)['unavailable_reason'],'missing_terminal_outcome')

    def test_invalid_history_or_cached_integrals_are_rejected(self):
        for mutate in (lambda t:t['win_probability_history'].append([1000,500]),
                       lambda t:t['win_probability_history'].__setitem__(1,[1000,1001]),
                       lambda t:t['win_probability'].__setitem__('area_twice_permille_ticks',900000)):
            t=_adjusted_team();mutate(t)
            with self.assertRaises(ValueError):group_score([t],_adjusted_result([t]))
        c=outcome_only(_adjusted_result());c['teams'][0]['win_probability_log_integral']['weighted_area']=float('nan')
        with self.assertRaisesRegex(ValueError,'compact'):group_score(c['teams'][:1],c)

    def test_horizon_and_format_cohorts_do_not_pool_or_pair(self):
        rows=[]
        for horizon,fmt in ((1000,'1v1'),(2000,'1v1'),(1000,'ffa')):
            for arm in ('baseline','candidate'):
                r=_adjusted_result(horizon=horizon,termination='engine_end',winners=[0])
                j=job('game','a'*64,config={'ticks':horizon},labels={'pair':'same-map','format':fmt,'variant':arm,'block':'seed'})
                rows+=observations([dict(job=j,result=r,category='success')],'win_probability')
        effects=[e for e in paired_effects(rows,0) if e['metric']==SCORE_METRIC]
        self.assertEqual({(e['format'],e['horizon_ticks']) for e in effects},{('1v1',1000),('1v1',2000),('ffa',1000)})
        scores=probability_scores(rows,0)
        self.assertEqual(len([s for s in scores if s['role']=='subject']),6)
        mismatch=copy.deepcopy(rows[:2]);mismatch[1]['trajectory_scores'][0]['horizon_ticks']=3000
        self.assertFalse(any(e['metric']==SCORE_METRIC for e in paired_effects(mismatch,0)))


class AdjustedScoreCompatibilityTests(unittest.TestCase):
    def test_unknown_cached_or_observation_metric_never_falls_back_to_auc(self):
        compact=outcome_only(_adjusted_result())
        compact['teams'][0]['win_probability_log_integral']['metric']='future-score'
        value=group_score(compact['teams'][:1],compact)
        self.assertFalse(value['available'])
        self.assertEqual(value['unavailable_reason'],'unsupported_cached_metric')
        row=_probability_row('candidate')
        row['trajectory_scores'][0]['metric']='future-score'
        score=next(v for v in probability_scores([row],0) if v['role']=='subject')
        self.assertEqual(score['unavailable_reasons'],{'unsupported_score_metric':1})
        self.assertIsNone(score['mean'])
        self.assertEqual(score['legacy_linear_mean'],.5)
        self.assertFalse(any(v['metric']==SCORE_METRIC for v in paired_effects([_probability_row(),row],0)))

    def test_actual_predicted_and_cap_sources_are_reported_separately(self):
        rows=[]
        for block,termination,end in (('actual','engine_end',200),('predicted','win_probability',200),('cap','tick_cap',1000)):
            teams=[_adjusted_team(0,[[0,500],[end,960]],'won' if termination!='tick_cap' else 'unresolved'),
                   _adjusted_team(1,[[0,500],[end,40]],'lost' if termination!='tick_cap' else 'unresolved')]
            result=_adjusted_result(teams,end=end,termination=termination,winners=[0] if termination!='tick_cap' else [])
            request=job('game','a'*64,config={'ticks':1000},labels={'block':block})
            rows+=observations([dict(job=request,result=outcome_only(result),category='success')],'win_probability')
        scores=probability_scores(rows,0)
        self.assertEqual(len(scores),2)
        for score in scores:
            self.assertEqual(score['scored_games'],3)
            self.assertEqual(score['outcome_credit_sources'],{'engine':1,'probability_stop':1,'tick_cap':1})
            self.assertEqual(score['probability_ended_games'],1)
            self.assertEqual(score['capped_games'],1)

    def test_old_compact_reports_coverage_instead_of_a_fabricated_score(self):
        result=_adjusted_result()
        for team in result['teams']:
            del team['win_probability_history']
        request=job('game','a'*64,config={'ticks':1000})
        rows=observations([dict(job=request,result=result,category='success')],'win_probability')
        for score in probability_scores(rows,0):
            self.assertIsNone(score['mean'])
            self.assertEqual(score['unavailable_reasons'],{'missing_history':1})
            self.assertAlmostEqual(score['legacy_linear_mean'],.5)


if __name__ == '__main__': unittest.main()
