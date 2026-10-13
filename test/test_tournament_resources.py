"""Pure allocation contracts: no engine processes or host CPU discovery."""
import copy
from dataclasses import FrozenInstanceError
import unittest
from tools.tournaments.resources import (Allocation, AllocationPlan, PROFILES,
    classify_cli_capability, plan_allocations, validate_frozen_attempt)


class TournamentResources(unittest.TestCase):
    def test_sorted_deterministic_nonoverlapping_budget(self):
        plan = plan_allocations([19, 3, 20, 4, 5, 21, 8], 3,
                                memory_planning_budget_bytes=10)
        self.assertEqual([a.cpu_ids for a in plan.allocations], [(3, 4, 5), (8, 19), (20, 21)])
        self.assertEqual([a.participants for a in plan.allocations], [3, 2, 2])
        self.assertEqual([a.memory_planning_bytes for a in plan.allocations], [4, 3, 3])
        self.assertEqual(plan, plan_allocations(list(reversed(plan.cpu_ids)), 3,
                                               memory_planning_budget_bytes=10))
        self.assertTrue(all(a.record()['participants_include_simulation_owner'] for a in plan.allocations))
        self.assertEqual(plan.record()['scope'], 'caller_supplied_budget_only')

    def test_scalar_participants_preserve_six_cpu_two_game_masks(self):
        cpus=[0,1,2,16,17,18]
        serial=plan_allocations(cpus,2,participants_per_game=1)
        three=plan_allocations(cpus,2,participants_per_game=3)
        self.assertEqual([a.cpu_ids for a in serial.allocations],[a.cpu_ids for a in three.allocations])
        self.assertEqual([a.participants for a in serial.allocations],[1,1])
        self.assertEqual([a.participants for a in three.allocations],[3,3])
        self.assertEqual(three,plan_allocations(cpus,2))
        for value in [True,0,-1,1.5,4]:
            with self.subTest(value=value),self.assertRaises(ValueError):
                plan_allocations(cpus,2,participants_per_game=value)
        with self.assertRaises(ValueError):
            plan_allocations(cpus,2,participants_per_game=1,explicit_allocations=serial.allocations)

    def test_no_memory_claim_without_budget(self):
        plan = plan_allocations([7], 1)
        self.assertEqual(plan.allocations, (Allocation((7,), 1),))
        self.assertIsNone(plan.record()['machine_budget']['memory_planning_bytes'])
        self.assertEqual(plan.allocations[0].record()['memory_enforcement'], 'soft_planning_only')

    def test_invalid_resource_ids_and_counts(self):
        for cpus in [[], None, [True], [-1], [1.0], [1, 1], '0,1']:
            with self.subTest(cpus=cpus), self.assertRaises(ValueError):
                plan_allocations(cpus, 1)
        for count in [True, 0, -1, 1.0, '1', 3]:
            with self.subTest(count=count), self.assertRaises(ValueError):
                plan_allocations([1, 2], count)
        for memory in [False, 0, -1, 1.0, 1]:
            with self.subTest(memory=memory), self.assertRaises(ValueError):
                plan_allocations([1, 2], 2, memory_planning_budget_bytes=memory)
        with self.assertRaises(ValueError):
            plan_allocations([1], 1, measured=1)

    def test_explicit_allocations_are_preserved_not_redistributed(self):
        first = Allocation((9, 2), 1, 20)
        second = Allocation((7,), 1, 10)
        plan = plan_allocations([2, 7, 9, 11], 2, explicit_allocations=[first, second],
                                memory_planning_budget_bytes=40, measured=True)
        self.assertEqual(plan.allocations, (first, second))
        self.assertEqual(plan.allocations[0].cpu_ids, (9, 2))
        self.assertNotIn(11, [cpu for a in plan.allocations for cpu in a.cpu_ids])
        with self.assertRaises(FrozenInstanceError):
            plan.measured = False
        with self.assertRaises(FrozenInstanceError):
            first.participants = 2

    def test_overlapping_outside_missing_and_overbudget_allocations(self):
        for allocations in [[Allocation((1,), 1), Allocation((1,), 1)],
                            [Allocation((1,), 1), Allocation((9,), 1)],
                            [Allocation((1,), 1)], [None, None]]:
            with self.subTest(allocations=allocations), self.assertRaises(ValueError):
                plan_allocations([1, 2], 2, explicit_allocations=allocations)
        for allocations in [[Allocation((1,), 1), Allocation((2,), 1, 5)],
                            [Allocation((1,), 1, 6), Allocation((2,), 1, 5)]]:
            with self.subTest(allocations=allocations), self.assertRaises(ValueError):
                plan_allocations([1, 2], 2, explicit_allocations=allocations,
                                 memory_planning_budget_bytes=10)
        for participants in [True, 0, -1, 3]:
            with self.assertRaises(ValueError):
                Allocation((1, 2), participants)
        with self.assertRaises(ValueError):
            AllocationPlan((1,), ())

    def test_frozen_attempt_manifest_and_mutable_json_export(self):
        plan = plan_allocations([2, 9], 2, measured=True)
        record = plan.attempt_record('attempt-1', 0)
        self.assertEqual(record['requested_execution_profile'], 'efficient-throughput')
        self.assertEqual(record['policy_version'], 1)
        self.assertTrue(record['measured'] and record['allocation_frozen'])
        self.assertEqual(record['resolved_plan_status'], 'pending_engine_result')
        validate_frozen_attempt(record, plan, 'attempt-1', 0)
        changed = copy.deepcopy(record)
        changed['allocation']['participants'] = 2
        with self.assertRaises(ValueError):
            validate_frozen_attempt(changed, plan, 'attempt-1', 0)
        boolean_count = copy.deepcopy(record)
        boolean_count['allocation']['participants'] = True
        with self.assertRaises(ValueError):
            validate_frozen_attempt(boolean_count, plan, 'attempt-1', 0)
        exported = plan.record()
        exported['allocations'][0]['cpu_ids'].append(99)
        self.assertEqual(plan.allocations[0].cpu_ids, (2,))
        for identity, slot in [('..', 0), ('bad/path', 0), ('', 0), ('okay', True), ('okay', -1), ('okay', 2)]:
            with self.subTest(identity=identity, slot=slot), self.assertRaises(ValueError):
                plan.attempt_record(identity, slot)

    def test_parsed_cli_capability_explicit_legacy_and_new(self):
        old = {'cli_version': 2, 'schema_version': 1, 'commands': ['game']}
        self.assertEqual(classify_cli_capability(old), 'legacy_cli2')
        new = dict(old, execution_profiles={'policy_version': 1,
                   'resource_plan_schema_version': 1, 'profiles': list(PROFILES)})
        self.assertEqual(classify_cli_capability(new), 'execution_profiles_v1')
        bad = copy.deepcopy(new)
        bad['execution_profiles']['policy_version'] = True
        self.assertEqual(classify_cli_capability(bad), 'unsupported_execution_contract')
        bad = copy.deepcopy(new)
        bad['execution_profiles']['profiles'].pop()
        self.assertEqual(classify_cli_capability(bad), 'unsupported_execution_contract')
        for desc in [dict(old, cli_version=True), dict(old, cli_version=1), {}]:
            self.assertEqual(classify_cli_capability(desc), 'unsupported_cli')
        with self.assertRaises(ValueError):
            classify_cli_capability(dict(old, execution_profiles='yes'))
        with self.assertRaises(ValueError):
            classify_cli_capability(dict(old, execution_profiles={'policy_version': 1,
                'resource_plan_schema_version': 1, 'profiles': ['auto', 'auto']}))



class TournamentResourceRuntime(unittest.TestCase):
    """Coordinator/worker admission with supplied files and mocked probes only."""
    def setUp(self):
        import tempfile
        from pathlib import Path
        from unittest.mock import patch
        from tools.tournaments.bundles import register_bundle
        from tools.tournaments.common import store_artifact
        from tools.tournaments.coordinator import Coordinator
        from tools.tournaments.model import job
        from tools.tournaments.worker import Worker
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        source = self.root / 'source';source.mkdir()
        executable = source / 'glob2';executable.write_text('not executed');executable.chmod(0o755)
        self.description = {'cli_version': 2, 'schema_version': 1, 'commands': [
            {'path': 'game run', 'available': True, 'options': [{'name': '--execution-profile'},
             {'name': '--compute-threads'}, {'name': '--memory-budget-bytes'}]}],
            'execution_profiles': {'policy_version': 1, 'resource_plan_schema_version': 1, 'profiles': list(PROFILES)}}
        self.bundle = register_bundle(source, self.root / 'bundles', 'glob2', 'test',
            capabilities={'schema_version': 1, 'commands': ['game']}, cli_description=self.description)
        self.bundle['directory'] = str(self.root / 'bundles' / self.bundle['id'])
        fixture = self.root / 'fixture.map';fixture.write_bytes(b'fixture')
        artifact = store_artifact(fixture, self.root / 'experiment' / 'artifacts')
        jobs = [job('game', self.bundle['id'], inputs={'map': artifact}, seeds={'game': i},
                    config={'players': ['nicowar', 'nicowar'], 'ticks': 16}) for i in range(4)]
        self.coordinator = Coordinator.submit(self.root / 'experiment',
            {'schema_version': 1, 'id': 'resources', 'jobs': jobs, 'settings': {'measured': True}},
            [self.root / 'bundles' / self.bundle['id']])
        self.addCleanup(self.coordinator.close)
        self.worker = Worker(self.root / 'worker')
        self.addCleanup(self.worker.close)
        self.affinity = patch('tools.tournaments.worker.os.sched_getaffinity', return_value={3, 4, 19, 20})
        self.affinity.start();self.addCleanup(self.affinity.stop)
        self.probe = patch('tools.tournaments.worker.installed_bundle', return_value=self.bundle)
        self.probe.start();self.addCleanup(self.probe.stop)
        self.worker.configure({'slots': 2, 'cpu_ids': [3, 4, 19, 20], 'memory_planning_budget_bytes': 8192})
        import shutil
        shutil.copyfile(self.root / 'experiment' / 'artifacts' / artifact['sha256'],
                        self.worker.root / 'objects' / artifact['sha256'])

    def test_new_game_identity_includes_default_execution_legacy_is_explicit(self):
        from tools.tournaments.model import job
        current = job('game', 'a'*64)
        legacy = job('game', 'a'*64, execution=None)
        self.assertEqual(current['execution']['requested_execution_profile'], 'efficient-throughput')
        self.assertNotIn('execution', legacy)
        self.assertNotEqual(current['id'], legacy['id'])
        self.assertNotIn('execution', job('generate_map', 'a'*64))

    def test_two_slot_admission_frozen_and_nonoverlapping_with_no_prefetch(self):
        attempts = self.coordinator.dispatch('host', self.worker.status())
        self.assertEqual(len(attempts), 2)
        masks = [a['execution_allocation']['allocation']['cpu_ids'] for a in attempts]
        self.assertFalse(set(masks[0]).intersection(masks[1]))
        self.assertEqual(sum(a['execution_allocation']['allocation']['memory_planning_bytes'] for a in attempts), 8192)
        for attempt in attempts:
            self.assertTrue(attempt['execution_allocation']['measured'])
            self.assertTrue(self.worker.enqueue(attempt)['enqueued'])
        self.assertEqual(self.coordinator.dispatch('host', self.worker.status()), [])
        with self.assertRaisesRegex(ValueError, 'frozen'):
            self.worker.configure({'slots': 1, 'cpu_ids': [3, 4, 19, 20], 'memory_planning_budget_bytes': 8192})

    def test_worker_rejects_collision_and_changed_plan_before_any_launch(self):
        attempts = self.coordinator.dispatch('host', self.worker.status())
        first = attempts[0]
        self.assertTrue(self.worker.enqueue(first)['enqueued'])
        other = copy.deepcopy(attempts[1])
        plan = plan_allocations([3, 4, 19, 20], 2, memory_planning_budget_bytes=8192, measured=True)
        other['execution_allocation'] = plan.attempt_record(other['id'], 0)
        self.assertEqual(self.worker.enqueue(other), {'enqueued': False, 'reason': 'resource_busy'})
        bad = copy.deepcopy(attempts[1]);bad['execution_allocation']['allocation']['participants'] = True
        with self.assertRaises(ValueError):
            self.worker.enqueue(bad)
        missing = copy.deepcopy(attempts[1]);del missing['execution_allocation']
        with self.assertRaises((KeyError, ValueError)):
            self.worker.enqueue(missing)

    def test_explicit_host_budget_required_and_no_implicit_fullmachine_claim(self):
        status = self.worker.status();status['resource_plan'] = None
        with self.assertRaisesRegex(ValueError, 'explicit host CPU budget'):
            self.coordinator.dispatch('host', status)
        with self.assertRaises(ValueError):
            self.worker.configure({'slots': 5, 'cpu_ids': [3, 4, 19, 20]})
        with self.assertRaises(ValueError):
            self.worker.configure({'slots': 1, 'cpu_ids': [99]})

    def test_reclaimed_completed_reservation_and_measured_retry_same_slot(self):
        attempts = self.coordinator.dispatch('host', self.worker.status(), now=1)
        self.worker.enqueue(attempts[0])
        self.worker.db.execute("UPDATE queue SET state='executed' WHERE id=?", (attempts[0]['id'],))
        # Result backlog does not remain a CPU reservation. Coordinator snapshot
        # reports executed, so the next job can reuse the released slot.
        again = self.coordinator.dispatch('host', self.worker.status(), now=2)
        self.assertEqual(len(again), 1)
        self.assertEqual(again[0]['execution_allocation']['slot'], attempts[0]['execution_allocation']['slot'])

    def test_cpu_affinity_and_resolved_provenance_required_for_success_acceptance(self):
        from tools.tournaments.bundles import package_identity
        attempt = self.coordinator.dispatch('host', self.worker.status())[0]
        record = {'schema_version': 1, 'id': attempt['id'], 'token': attempt['token'],
            'host': 'host', 'package_id': package_identity(), 'job': attempt['job'],
            'category': 'success', 'artifacts': [], 'missing_artifacts': [],
            'execution_allocation': attempt['execution_allocation'],
            'result': {'tournament_execution': {'attempt_allocation': attempt['execution_allocation']}}}
        with self.assertRaisesRegex(ValueError, 'affinity proof'):
            self.coordinator.accept(record)
        record['cpu_affinity_proof'] = {'verified': True,
            'actual_cpu_ids': sorted(attempt['execution_allocation']['allocation']['cpu_ids'])}
        with self.assertRaisesRegex(ValueError, 'resolved execution-resource plan'):
            self.coordinator.accept(record)
        from tools.tournaments.jobs import EngineJob
        record['result']['compute_threads'] = 2
        record['result']['compute_resolved_threads'] = 2
        record['result']['execution_resources'] = {'schema_version': 1, 'policy_version': 1,
            'requested_execution_profile': 'efficient-throughput', 'selected_execution_profile': 'efficient-throughput',
            'profile_source': 'explicit_profile', 'profile_tuning_pending': True,
            'resolved_resources': {'compute_participants': 2, 'planning_memory_budget_bytes': 4096,
                                   'recoverable_memory_budget_bytes': 0},
            'device_capabilities': {'allocated_cpu_participants': 2, 'allocated_memory_bytes': None}}
        record['result'] = EngineJob('game').join_execution_result(attempt['job'], self.bundle,
            self.worker.root/'attempts'/attempt['id'], attempt['execution_allocation'], record['result'])
        from tools.tournaments.worker import outcome_only
        record['result']['compute_requested_threads'] = '2'
        record['result'] = outcome_only(record['result'])
        self.assertEqual(record['result']['compute_threads'], 2)
        self.assertEqual(record['result']['compute_resolved_threads'], 2)
        self.assertEqual(record['result']['compute_requested_threads'], '2')
        self.assertTrue(self.coordinator.accept(record))

    def test_outcome_only_keeps_resource_plan_and_allocation_provenance(self):
        from tools.tournaments.worker import outcome_only
        result = {'schema_version': 1, 'status': 'completed', 'execution_resources': {'policy_version': 1},
                  'tournament_execution': {'resolved_plan_status': 'reported_engine_plan'}}
        compact = outcome_only(result)
        self.assertEqual(compact['execution_resources'], result['execution_resources'])
        self.assertEqual(compact['tournament_execution'], result['tournament_execution'])

    def test_supervisor_enforces_inherited_mask_before_mocked_child(self):
        from unittest.mock import Mock, patch
        from tools.tournaments.common import atomic_json, read_json
        from tools.tournaments.worker import execute
        attempt = self.coordinator.dispatch('host', self.worker.status())[0]
        self.worker.enqueue(attempt)
        directory = self.worker.root / 'attempts' / attempt['id']
        (directory / 'output').mkdir()
        plan = {'schema_version': 1, 'policy_version': 1,
            'requested_execution_profile': 'efficient-throughput', 'selected_execution_profile': 'efficient-throughput',
            'profile_source': 'explicit_profile', 'profile_tuning_pending': True,
            'resolved_resources': {'compute_participants': 2, 'planning_memory_budget_bytes': 4096,
                                   'recoverable_memory_budget_bytes': 0},
            'device_capabilities': {'allocated_cpu_participants': 2, 'allocated_memory_bytes': None}}
        atomic_json(directory / 'output/result.json', {'schema_version': 1, 'status': 'completed',
                                                        'execution_resources': plan, 'compute_threads': 2, 'compute_resolved_threads': 2})
        child = Mock();child.poll.return_value = 0;child.wait.return_value = 0;child.pid = 123
        expected = attempt['execution_allocation']['allocation']['cpu_ids']
        def spawn(*args, **kwargs):
            enforce.assert_called_once_with(0, tuple(expected))
            return child
        with patch('tools.tournaments.worker.os.sched_getaffinity', side_effect=[{3,4,19,20}, set(expected)]), \
             patch('tools.tournaments.worker.os.sched_setaffinity') as enforce, \
             patch('tools.tournaments.worker.subprocess.Popen', side_effect=spawn) as launch:
            execute(self.worker.root, attempt['id'])
            launch.assert_called_once()
        result = read_json(directory / 'execution.json')
        self.assertEqual(result['category'], 'success')
        self.assertEqual(result['cpu_affinity_proof']['actual_cpu_ids'], sorted(expected))
        self.assertEqual(result['execution_allocation'], attempt['execution_allocation'])

    def test_failed_mask_enforcement_never_launches_mocked_child(self):
        from unittest.mock import patch
        from tools.tournaments.common import read_json
        from tools.tournaments.worker import execute
        attempt = self.coordinator.dispatch('host', self.worker.status())[0]
        self.worker.enqueue(attempt)
        with patch('tools.tournaments.worker.os.sched_setaffinity'), \
             patch('tools.tournaments.worker.subprocess.Popen') as launch:
            execute(self.worker.root, attempt['id'])
            launch.assert_not_called()
        result = read_json(self.worker.root/'attempts'/attempt['id']/'execution.json')
        self.assertEqual(result['category'], 'invalid_request')
        self.assertIn('CPU affinity enforcement failed', result['diagnostic'])

    def test_measured_retry_keeps_original_host_and_slot_with_alternative_host(self):
        first = self.coordinator.dispatch('host', self.worker.status(), now=1)
        self.coordinator.host_status('alternative', self.worker.status())
        self.coordinator.expire(now=1000)
        retried = self.coordinator.dispatch('host', self.worker.status(), now=1000)
        self.assertEqual(len(retried), 2)
        by_job = {a['job']['id']: a for a in first}
        for attempt in retried:
            old = by_job[attempt['job']['id']]
            self.assertEqual(attempt['execution_allocation']['slot'], old['execution_allocation']['slot'])
            self.assertEqual(attempt['allocation_plan'], old['allocation_plan'])
        self.assertFalse(any(a['job']['id'] in by_job for a in
                             self.coordinator.dispatch('alternative', self.worker.status(), now=1001)))

    def test_mixed_legacy_and_bounded_queue_drains_without_overlap(self):
        import time
        from unittest.mock import patch
        bounded = self.coordinator.dispatch('host', self.worker.status())[0]
        self.worker.enqueue(bounded)
        legacy = copy.deepcopy(bounded)
        legacy['id'] = 'b'*32;legacy['token'] = 'c'*32
        del legacy['execution_allocation'];del legacy['allocation_plan'];del legacy['job']['execution']
        self.assertTrue(self.worker.enqueue(legacy)['enqueued'])
        another = copy.deepcopy(bounded);another['id'] = 'd'*32;another['token'] = 'e'*32
        from tools.tournaments.resources import plan_from_record
        plan = plan_from_record(another['allocation_plan'])
        another['execution_allocation'] = plan.attempt_record(another['id'], 1)
        self.assertEqual(self.worker.enqueue(another), {'enqueued': False, 'reason': 'resource_busy'})
        launched = []
        def launch(row, command, state):
            launched.append(row['id'])
            self.worker.db.execute('UPDATE queue SET state=?,updated=?,started=? WHERE id=?',
                                   (state, time.time(), time.time(), row['id']))
        with patch.object(self.worker, 'launch', side_effect=launch):
            self.worker.tick()
            self.assertEqual(launched, [bounded['id']])
            self.worker.db.execute("UPDATE queue SET state='done' WHERE id=?", (bounded['id'],))
            self.worker.tick()
            self.assertEqual(launched, [bounded['id'], legacy['id']])

    def test_old_binary_requires_explicit_compatibility_before_submission(self):
        from tools.tournaments.bundles import register_bundle
        from tools.tournaments.coordinator import Coordinator
        from tools.tournaments.model import job
        from tools.tournaments.resources import default_execution_request
        legacy = register_bundle(self.root/'source', self.root/'older', 'glob2', 'old',
            capabilities={'schema_version': 1, 'commands': ['game']},
            cli_description={'cli_version': 2, 'schema_version': 1, 'commands': ['game']})
        current_job = job('game', legacy['id'], config={'generator': 15, 'players': ['nicowar']},
                          seeds={'map': 1, 'game': 2})
        with self.assertRaisesRegex(ValueError, 'does not advertise'):
            Coordinator.submit(self.root/'rejected', {'schema_version': 1, 'id': 'old', 'jobs': [current_job]},
                               [self.root/'older'/legacy['id']])
        compatible = job('game', legacy['id'], config=current_job['config'], seeds=current_job['seeds'],
                         execution=default_execution_request(compatibility_mode='legacy_argv'))
        coordinator = Coordinator.submit(self.root/'explicit', {'schema_version': 1, 'id': 'explicit', 'jobs': [compatible]},
                                         [self.root/'older'/legacy['id']])
        coordinator.close()

    def test_profile_cpu_delta_excludes_help_and_rss_peak_is_not_subtracted(self):
        import sys
        from types import SimpleNamespace
        from unittest.mock import Mock, patch
        from tools.tournaments.common import atomic_json, read_json
        from tools.tournaments.worker import execute
        # Both after-peak greater and equal must be covered: equality cannot
        # establish the game peak, even though CPU subtraction remains valid.
        attempts = self.coordinator.dispatch('host', self.worker.status())
        for attempt, after_rss in zip(attempts, (200, 100)):
            self.worker.enqueue(attempt)
            directory = self.worker.root/'attempts'/attempt['id'];(directory/'output').mkdir()
            plan = {'schema_version': 1, 'policy_version': 1,
                'requested_execution_profile': 'efficient-throughput', 'selected_execution_profile': 'efficient-throughput',
                'profile_source': 'explicit_profile', 'profile_tuning_pending': True,
                'resolved_resources': {'compute_participants': 2, 'planning_memory_budget_bytes': 4096,
                                       'recoverable_memory_budget_bytes': 0},
                'device_capabilities': {'allocated_cpu_participants': 2, 'allocated_memory_bytes': None}}
            atomic_json(directory/'output/result.json', {'schema_version':1, 'status':'completed',
                'execution_resources':plan,'compute_threads':2,'compute_resolved_threads':2})
            before = SimpleNamespace(ru_utime=7.0, ru_stime=3.0, ru_maxrss=100)
            after = SimpleNamespace(ru_utime=9.5, ru_stime=4.25, ru_maxrss=after_rss)
            child = Mock();child.poll.return_value=0;child.wait.return_value=0;child.pid=123
            mask = set(attempt['execution_allocation']['allocation']['cpu_ids'])
            with patch('tools.tournaments.worker.os.sched_getaffinity', side_effect=[{3,4,19,20},mask]), \
                 patch('tools.tournaments.worker.os.sched_setaffinity'), \
                 patch('tools.tournaments.worker.subprocess.Popen', return_value=child), \
                 patch('resource.getrusage', side_effect=[before,after]) as usage:
                execute(self.worker.root,attempt['id'])
                self.assertEqual(usage.call_count,2)
            result=read_json(directory/'execution.json')['resource_usage']
            scale=1 if sys.platform=='darwin' else 1024
            self.assertEqual(result['user_seconds'],2.5)
            self.assertEqual(result['system_seconds'],1.25)
            self.assertEqual(result['cpu_scope'],'game_child_waited_cpu_delta')
            self.assertEqual(result['peak_rss_bytes'],after_rss*scale)
            self.assertEqual(result['peak_scope'],'all_supervisor_children_including_probes')
            self.assertEqual(result['game_peak_known'],after_rss>100)
            self.assertEqual(result['game_peak_rss_bytes'],after_rss*scale if after_rss>100 else None)

    def test_legacy_custom_adapter_keeps_positional_command_collect_contract(self):
        from unittest.mock import Mock, patch
        from tools.tournaments import jobs
        from tools.tournaments.model import job
        from tools.tournaments.common import read_json
        from tools.tournaments.worker import execute
        calls = []
        class CustomAdapter:
            def validate(self, value):
                pass
            def command(self, value, bundle, directory, inputs):
                calls.append('command')
                return ['/not-executed']
            def collect(self, value, directory):
                calls.append('collect')
                return {'schema_version': 1, 'status': 'completed'}
        attempt = self.coordinator.dispatch('host', self.worker.status())[0]
        del attempt['execution_allocation'];del attempt['allocation_plan']
        attempt['job'] = job('fixture_adapter', self.bundle['id'])
        child=Mock();child.poll.return_value=0;child.wait.return_value=0;child.pid=123
        with patch.dict(jobs._TYPES, {'fixture_adapter': CustomAdapter()}), \
             patch('tools.tournaments.worker.subprocess.Popen', return_value=child):
            self.assertTrue(self.worker.enqueue(attempt)['enqueued'])
            execute(self.worker.root,attempt['id'])
        self.assertEqual(calls,['command','collect'])
        self.assertEqual(read_json(self.worker.root/'attempts'/attempt['id']/'execution.json')['category'],'success')

    def test_concurrent_independent_rpc_connections_cannot_reserve_same_cpus(self):
        from concurrent.futures import ThreadPoolExecutor
        from threading import Barrier
        from tools.tournaments.worker import Worker
        attempts=self.coordinator.dispatch('host',self.worker.status())
        from tools.tournaments.resources import plan_from_record
        plan=plan_from_record(attempts[0]['allocation_plan'])
        attempts[1]['execution_allocation']=plan.attempt_record(attempts[1]['id'],0)
        ready=Barrier(2)
        def enqueue(attempt):
            worker=Worker(self.worker.root)
            try:
                ready.wait(timeout=3)
                return worker.enqueue(attempt)
            finally:
                worker.close()
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures=[pool.submit(enqueue,a) for a in attempts]
            results=[future.result(timeout=5) for future in futures]
        self.assertEqual(sum(r['enqueued'] for r in results),1)
        self.assertEqual([r['reason'] for r in results if not r['enqueued']],['resource_busy'])
        self.assertEqual(self.worker.db.execute("SELECT count(*) FROM queue WHERE state='queued'").fetchone()[0],1)

    def test_live_participant_override_is_frozen_with_unchanged_mask(self):
        self.worker.configure({'slots':2,'cpu_ids':[3,4,19,20],'memory_planning_budget_bytes':8192,
                               'participants_per_game':1})
        attempts=self.coordinator.dispatch('host',self.worker.status())
        self.assertEqual([a['execution_allocation']['allocation']['participants'] for a in attempts],[1,1])
        masks=[a['execution_allocation']['allocation']['cpu_ids'] for a in attempts]
        self.assertEqual(masks,[[3,4],[19,20]])
        self.worker.enqueue(attempts[0])
        with self.assertRaisesRegex(ValueError,'frozen'):
            self.worker.configure({'slots':2,'cpu_ids':[3,4,19,20],'memory_planning_budget_bytes':8192,
                                   'participants_per_game':2})
        self.assertEqual(self.worker.status()['resource_plan']['allocations'][0]['participants'],1)

    def test_dispatch_checks_frozen_caller_participant_plan_against_host(self):
        self.coordinator.settings['caller_allocation_plan']=plan_allocations([3,4,19,20],2,
            memory_planning_budget_bytes=8192,participants_per_game=1).record()
        with self.assertRaisesRegex(ValueError,'frozen caller manifest'):
            self.coordinator.dispatch('host',self.worker.status())
        self.worker.configure({'slots':2,'cpu_ids':[3,4,19,20],'memory_planning_budget_bytes':8192,
                               'participants_per_game':1})
        attempts=self.coordinator.dispatch('host',self.worker.status())
        self.assertEqual([a['execution_allocation']['allocation']['participants'] for a in attempts],[1,1])


class LocalRunJobResources(unittest.TestCase):
    """Reject unsupported requests before any bundle probe or engine launch."""

    def invoke(self, kind, **overrides):
        from pathlib import Path
        from unittest.mock import patch
        from tools.tournaments import local
        with patch.object(local, 'supplied_binary', return_value=Path('/bundles/' + 'a' * 64)) as bundle, \
             patch.object(local, 'execute_jobs', return_value=[{'status': 'completed'}]) as execute:
            result, source=local.run_job('/unused/glob2', '/unused/data', '/unused/results', kind,
                config={'fixture': 'unchanged'}, seeds={'map': 123}, **overrides)
            self.assertEqual(result, {'status': 'completed'})
            self.assertEqual(source, [{'status': 'completed'}])
            bundle.assert_called_once()
            execute.assert_called_once()
            return execute.call_args.args[1][0], execute.call_args.kwargs

    def test_omitted_generation_requests_preserve_job_identity(self):
        from tools.tournaments.model import job
        value, options=self.invoke('generate_map')
        expected=job('generate_map', 'a' * 64, config={'fixture': 'unchanged'}, seeds={'map': 123},
                     limits={'timeout_seconds': 3600}, execution=None)
        self.assertEqual(value, expected)
        self.assertEqual(options, {'cpu_ids': None, 'memory_planning_budget_bytes': None,
                                  'participants_per_game': None})

    def test_omitted_game_profile_keeps_existing_defaults(self):
        from tools.tournaments.resources import default_execution_request
        value, options=self.invoke('game', cpu_ids=[3, 19])
        self.assertEqual(value['execution'], default_execution_request())
        self.assertEqual(options['cpu_ids'], [3, 19])

    def test_explicit_game_profile_and_compatibility_preserved(self):
        from tools.tournaments.resources import default_execution_request
        for profile, mode in [('balanced', 'require_profile'), ('fast-simulation', 'legacy_argv')]:
            with self.subTest(profile=profile, mode=mode):
                value, options=self.invoke('game', cpu_ids=[3, 19], participants_per_game=1,
                    requested_execution_profile=profile, compatibility_mode=mode)
                self.assertEqual(value['execution'], default_execution_request(
                    requested_execution_profile=profile, compatibility_mode=mode))
                self.assertEqual(options['participants_per_game'], 1)

    def test_explicit_non_game_requests_never_package_or_launch(self):
        from unittest.mock import patch
        from tools.tournaments import local
        requests={'cpu_ids': [3, 19], 'memory_planning_budget_bytes': 8192,
                  'participants_per_game': 1, 'compatibility_mode': 'require_profile',
                  'requested_execution_profile': 'efficient-throughput'}
        for kind in ['generate_map', 'verify_match']:
            for name, value in requests.items():
                with self.subTest(kind=kind, name=name), \
                     patch.object(local, 'supplied_binary') as bundle, \
                     patch.object(local, 'execute_jobs') as execute, \
                     patch.object(local, 'configure_hosts') as configure:
                    with self.assertRaisesRegex(ValueError, 'unsupported resource requests.*' + name):
                        local.run_job('/unused/glob2', '/unused/data', '/unused/results', kind,
                                      config={}, seeds={}, **{name: value})
                    bundle.assert_not_called()
                    execute.assert_not_called()
                    configure.assert_not_called()

    def test_direct_allocations_reject_any_job_without_execution(self):
        from pathlib import Path
        from unittest.mock import patch
        from tools.tournaments import local
        from tools.tournaments.model import job
        legacy=job('game', 'a' * 64, execution=None)
        generation=job('generate_map', 'a' * 64)
        current=job('game', 'a' * 64)
        for jobs in [[legacy], [generation], [current, legacy], [current, generation]]:
            for overrides in [{'cpu_ids': [3, 19]}, {'participants_per_game': 1},
                              {'memory_planning_budget_bytes': 8192}]:
                with self.subTest(jobs=[j['type'] for j in jobs], overrides=overrides), \
                     patch.object(local.Coordinator, 'submit') as submit, \
                     patch.object(local, 'configure_hosts') as configure:
                    with self.assertRaisesRegex(ValueError, 'without execution contracts'):
                        local.execute_jobs('/unused/results', jobs, Path('/unused/bundle'), **overrides)
                    submit.assert_not_called()
                    configure.assert_not_called()

    def test_direct_legacy_without_allocations_still_dispatches(self):
        from pathlib import Path
        from unittest.mock import Mock, patch
        from tools.tournaments import local
        from tools.tournaments.model import job
        jobs=[job('generate_map', 'a' * 64), job('game', 'a' * 64, execution=None)]
        coordinator=Mock()
        coordinator.run.return_value={'jobs': {'completed': 2}}
        with patch.object(local.Coordinator, 'submit', return_value=coordinator) as submit, \
             patch.object(local, 'Transport') as transport, \
             patch.object(local, 'Results') as results:
            self.assertIs(local.execute_jobs('/unused/results', jobs, Path('/unused/bundle')), results.return_value)
            self.assertEqual(submit.call_args.args[1]['jobs'], jobs)
            self.assertNotIn('caller_allocation_plan', submit.call_args.args[1]['settings'])
            coordinator.run.assert_called_once()
            coordinator.close.assert_called_once()
            transport.return_value.rpc.assert_called_once_with('stop')


if __name__ == '__main__':
    unittest.main()
