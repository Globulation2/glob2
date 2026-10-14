"""Frozen CPU shares and dispatch/worker admission, without host CPU discovery."""
import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.bundles import package_identity, register_bundle
from tools.tournaments.common import read_json
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job
from tools.tournaments.resources import (Allocation, AllocationPlan, plan_allocations,
    plan_from_record, validate_frozen_attempt, validate_game_participants)
from tools.tournaments.worker import Worker, execute, outcome_only
from test_tournaments import FAKE


class AllocationContracts(unittest.TestCase):
    def test_partition_is_sorted_disjoint_owner_inclusive_and_frozen(self):
        plan = plan_allocations([20, 4, 3, 19, 31], 2)
        self.assertEqual([a.cpu_ids for a in plan.allocations], [(3, 4, 19), (20, 31)])
        self.assertEqual([a.participants for a in plan.allocations], [3, 2])
        self.assertEqual(plan_from_record(plan.record()), plan)
        self.assertTrue(plan.allocations[0].record()['participants_include_simulation_owner'])
        record = plan.attempt_record('attempt', 1)
        self.assertEqual(validate_frozen_attempt(record, plan, 'attempt'), plan.allocations[1])
        for field, value in [('slot', True), ('attempt_id', 'changed'), ('allocation_frozen', 1)]:
            changed = copy.deepcopy(record); changed[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_frozen_attempt(changed, plan, 'attempt')

    def test_invalid_budgets_and_records(self):
        for cpus, slots in [([], 1), ([1, 1], 1), ([True], 1), ([-1], 1), ([1], True), ([1], 2)]:
            with self.subTest(cpus=cpus, slots=slots), self.assertRaises(ValueError): plan_allocations(cpus, slots)
        with self.assertRaises(ValueError): plan_allocations([1, 2, 3], 2, participants_per_game=2)
        with self.assertRaises(ValueError): AllocationPlan([1, 2], [Allocation([1], 1), Allocation([1], 1)])
        for change in ({'schema_version': True}, {'extra': 1}, {'allocation_frozen': 1}):
            with self.assertRaises(ValueError): plan_from_record(plan_allocations([1], 1).record() | change)

    def test_explicit_count_can_use_less_than_cpu_share(self):
        plan = plan_allocations([1, 2, 3, 4], 2, participants_per_game=1)
        self.assertEqual([a.participants for a in plan.allocations], [1, 1])

    def test_actual_result_counts_must_match_and_survive_outcome_projection(self):
        result = {'compute_threads': 2, 'compute_resolved_threads': 2, 'gpu_requested': False}
        allocation = Allocation([3, 4], 2)
        validate_game_participants(outcome_only(result), allocation)
        for key in ('compute_threads', 'compute_resolved_threads'):
            for value in (None, True, 1, 3):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    validate_game_participants(result | {key: value}, allocation)


class FrozenAdmission(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.mask = {3, 4, 19, 20}
        self.get_affinity = patch.object(os, 'sched_getaffinity', side_effect=lambda pid: self.mask, create=True)
        self.set_affinity = patch.object(os, 'sched_setaffinity', side_effect=lambda pid, ids: setattr(self, 'mask', set(ids)), create=True)
        self.get_affinity.start(); self.addCleanup(self.get_affinity.stop)
        self.set_affinity.start(); self.addCleanup(self.set_affinity.stop)
        source = self.root / 'source'; source.mkdir()
        (source / 'glob2').write_text(FAKE); (source / 'glob2').chmod(0o755)
        self.bundle = register_bundle(source, self.root / 'bundles', 'glob2', 'fixture')
        bundle_directory = self.root / 'bundles' / self.bundle['id']
        jobs = [job('game', self.bundle['id'], seeds={'map': 1, 'game': i},
                    config={'generator': 15, 'players': ['numbi', 'castor']}) for i in range(3)]
        manifest = {'schema_version': 1, 'id': 'cpu-test', 'jobs': jobs,
                    'settings': {'heartbeat_seconds': 1, 'lease_seconds': 10, 'measured': True}}
        self.coordinator = Coordinator.submit(self.root / 'experiment', manifest, [bundle_directory])
        self.addCleanup(self.coordinator.close)
        self.worker = Worker(self.root / 'worker'); self.addCleanup(self.worker.close)
        import shutil
        shutil.copytree(bundle_directory, self.worker.root / 'bundles' / self.bundle['id'])
        self.worker.configure({'slots': 2, 'cpu_ids': [3, 4, 19, 20], 'disk_reserve_bytes': 0})

    def test_dispatch_reserves_disjoint_slots_despite_prefetch_and_stale_status(self):
        status = self.worker.status()
        attempts = self.coordinator.dispatch('host', status, now=1)
        self.assertEqual(len(attempts), 2)
        self.assertEqual([a['execution_allocation']['slot'] for a in attempts], [0, 1])
        lane = Coordinator(self.coordinator.root, repair=False)
        try: self.assertEqual(lane.dispatch('host', status, now=2), [])
        finally: lane.close()
        for attempt in attempts: self.assertTrue(self.worker.enqueue(attempt)['enqueued'])
        self.assertEqual([a['execution_allocation']['slot'] for a in self.worker.status()['attempts']], [0, 1])

    def test_worker_rejects_overlap_mutation_missing_and_budget_change(self):
        attempts = self.coordinator.dispatch('host', self.worker.status(), now=1)
        self.worker.enqueue(attempts[0])
        other = copy.deepcopy(attempts[1]); plan = plan_from_record(other['allocation_plan'])
        other['execution_allocation'] = plan.attempt_record(other['id'], 0)
        self.assertEqual(self.worker.enqueue(other), {'enqueued': False, 'reason': 'resource_busy'})
        bad = copy.deepcopy(attempts[1]); bad['execution_allocation']['allocation']['participants'] = True
        with self.assertRaises(ValueError): self.worker.enqueue(bad)
        bad = copy.deepcopy(attempts[1]); del bad['execution_allocation']
        with self.assertRaises(ValueError): self.worker.enqueue(bad)
        with self.assertRaisesRegex(ValueError, 'live work'): self.worker.configure({'slots': 1, 'cpu_ids': [3]})
        with self.assertRaisesRegex(ValueError, 'eligible'): self.worker.configure({'slots': 1, 'cpu_ids': [99]})

    def test_cohort_plan_is_frozen_even_before_enqueue(self):
        status = self.worker.status(); self.coordinator.dispatch('host', status, now=1)
        changed = status | {'resource_plan': plan_allocations([3, 4, 19, 20], 1).record()}
        with self.assertRaisesRegex(ValueError, 'changed within'): self.coordinator.dispatch('host', changed, now=2)
        with self.assertRaisesRegex(ValueError, 'changed within'): self.coordinator.dispatch('host', status | {'resource_plan': None}, now=2)

    def test_completed_work_releases_cpu_share_before_result_collection(self):
        attempts = self.coordinator.dispatch('host', self.worker.status(), now=1)
        self.worker.enqueue(attempts[0])
        self.worker.db.execute("UPDATE queue SET state='executed' WHERE id=?", (attempts[0]['id'],))
        again = self.coordinator.dispatch('host', self.worker.status(), now=2)
        self.assertEqual(len(again), 1)
        self.assertEqual(again[0]['execution_allocation']['slot'], 0)

    def test_measured_retry_keeps_host_and_slot(self):
        attempts = self.coordinator.dispatch('host', self.worker.status(), now=1)
        self.coordinator.expire(now=12)
        other = self.coordinator.dispatch('other', self.worker.status(), now=12)
        self.assertFalse({a['job']['id'] for a in other}.intersection(a['job']['id'] for a in attempts))
        retries = self.coordinator.dispatch('host', self.worker.status(), now=12)
        self.assertEqual([a['job']['id'] for a in retries], [a['job']['id'] for a in attempts])
        self.assertEqual([a['execution_allocation']['slot'] for a in retries], [0, 1])

    def test_measured_retry_cannot_move_to_an_unbudgeted_host(self):
        attempts = self.coordinator.dispatch('host', self.worker.status(), now=1)
        self.coordinator.expire(now=12)
        unbudgeted = self.worker.status() | {'resource_plan': None}
        other = self.coordinator.dispatch('legacy', unbudgeted, now=12)
        self.assertEqual(len(other), 1)  # The previously untried job remains eligible.
        self.assertNotIn('execution_allocation', other[0])
        self.assertFalse({a['job']['id'] for a in other}.intersection(a['job']['id'] for a in attempts))
        retries = self.coordinator.dispatch('host', self.worker.status(), now=12)
        self.assertEqual([a['job']['id'] for a in retries], [a['job']['id'] for a in attempts])
        self.assertEqual([a['execution_allocation']['slot'] for a in retries], [0, 1])

    def test_success_requires_unchanged_allocation_affinity_and_actual_participants(self):
        attempt = self.coordinator.dispatch('host', self.worker.status(), now=1)[0]
        record = {'schema_version': 1, 'id': attempt['id'], 'token': attempt['token'], 'host': 'host',
            'package_id': package_identity(), 'job': attempt['job'], 'category': 'success', 'artifacts': [],
            'execution_allocation': attempt['execution_allocation'],
            'result': {'compute_threads': 2, 'compute_resolved_threads': 2}}
        with self.assertRaisesRegex(ValueError, 'affinity proof'): self.coordinator.accept(record, now=2)
        record['cpu_affinity_proof'] = {'verified': True, 'actual_cpu_ids': [3, 4]}
        bad = copy.deepcopy(record); bad['result']['compute_threads'] = 1
        with self.assertRaisesRegex(ValueError, 'assigned compute'): self.coordinator.accept(bad, now=2)
        self.assertTrue(self.coordinator.accept(record, now=2))
        (self.coordinator.root / 'transfers' / (attempt['id'] + '.json')).unlink()
        self.assertTrue(self.coordinator.accept(record, now=3))

    def test_supervisor_verifies_mask_before_launch_and_passes_explicit_count(self):
        attempt = self.coordinator.dispatch('host', self.worker.status(), now=1)[0]
        self.worker.enqueue(attempt)
        process = Mock(); process.poll.return_value = 0; process.wait.return_value = 0
        def launch(args, **kwargs):
            self.assertEqual(self.mask, {3, 4})
            self.assertEqual(args[-2:], ['--compute-threads', '2'])
            return process
        with patch('tools.tournaments.worker.subprocess.Popen', side_effect=launch), \
             patch('tools.tournaments.jobs.EngineJob.collect', return_value={
                 'status': 'completed', 'compute_threads': 2, 'compute_resolved_threads': 2}):
            execute(self.worker.root, attempt['id'])
        record = read_json(self.worker.root / 'attempts' / attempt['id'] / 'execution.json')
        self.assertEqual(record['category'], 'success')
        self.assertEqual(record['cpu_affinity_proof']['actual_cpu_ids'], [3, 4])

    def test_failed_affinity_enforcement_never_launches_game(self):
        attempt = self.coordinator.dispatch('host', self.worker.status(), now=1)[0]
        self.worker.enqueue(attempt)
        with patch.object(os, 'sched_setaffinity'), patch('tools.tournaments.worker.subprocess.Popen') as launch:
            execute(self.worker.root, attempt['id'])
            launch.assert_not_called()
        record = read_json(self.worker.root / 'attempts' / attempt['id'] / 'execution.json')
        self.assertEqual(record['category'], 'invalid_request')
        self.assertIn('affinity enforcement failed', record['diagnostic'])

    def test_requested_budget_fails_when_platform_cannot_enforce_affinity(self):
        from types import SimpleNamespace
        with patch('tools.tournaments.worker.os', SimpleNamespace()):
            with self.assertRaisesRegex(ValueError, 'affinity enforcement support'):
                self.worker.resource_plan()


if __name__ == '__main__': unittest.main()
