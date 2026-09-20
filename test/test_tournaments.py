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
from tools.tournaments.worker import Worker, pack

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
        process = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
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

    def test_the_win_probability_rule_is_off_unless_the_design_asks(self):
        """It changes the outcome that gets measured, so it must never arrive by
        default -- and when it is asked for it has to reach the engine."""
        from tools.tournaments.experiments import Planner
        from tools.tournaments.jobs import EngineJob
        bundle = {'id': 'a'*64, 'capabilities': {
            'ais': [{'id': 1, 'name': 'numbi'}, {'id': 2, 'name': 'castor'}],
            'generators': [{'method': 15, 'editorOnly': False}]}}
        base = {'id': 'sample', 'sample_games': 5, 'sample_seed': 3}
        plain = Planner('ai_comparison', dict(base), [bundle]).plan()
        for job in plain['jobs']:
            self.assertNotIn('win_probability_permille', job['config'])
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
        plain_game = next(j for j in plain['jobs'] if j['type'] == 'game')
        self.assertNotIn('--win-probability',
                         EngineJob('game').command(plain_game, bundle_directory, '/tmp/attempt', {}))

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


if __name__ == '__main__': unittest.main()
