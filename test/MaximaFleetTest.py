#!/usr/bin/env python3
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import maxima_experiment_fleet as fleet
import maxima_experiment_queue as queue


class FleetTest(unittest.TestCase):
    def test_continuous_game_uses_one_process_and_preserves_unresolved_outcome(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'run'
            protocol = {'execution_mode': 'uninterrupted', 'checkpoint_harvest_interval': 200000}
            payload = {'scenario': {'scenario_id': 'one'}, 'settings': {}}
            def spawn(args, **kwargs):
                self.assertIn('--maxima-checkpoint-harvest', args)
                (out / 'checkpoints' / 'checkpoint-200000.game').touch()
                return SimpleNamespace(pid=123, wait=lambda: 0)
            with patch.object(queue.os, 'sched_getaffinity', return_value={0}, create=True), \
                 patch.object(queue.experiment, 'command', return_value=(['engine'], {})) as command, \
                 patch.object(queue.experiment, 'read_audit', return_value={'outcome': None, 'terminal': {'tick': 200000}}), \
                 patch.object(queue.subprocess, 'Popen', side_effect=spawn) as process:
                result = queue.run_job(protocol, payload, out, 0)
            self.assertEqual(process.call_count, 1)
            self.assertEqual(command.call_args.args[3], 200000)
            self.assertIsNone(command.call_args.args[5])
            self.assertIsNone(result['outcome'])

    def test_start_records_topology_before_spawning_workers(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'protocol.json').write_text(json.dumps({'protocol_id': 'fixed'}))
            (out / 'manifest.json').write_text(json.dumps({'protocol_id': 'fixed', 'stage': 'controls',
                'jobs': [{'execution_id': 'one'}]}))
            (out / 'assignments.json').write_text(json.dumps({'a': {'weak': ['one']}}))
            def spawn(*args, **kwargs):
                q = queue.Queue(out / 'queue.sqlite')
                row = q.db.execute('SELECT * FROM hosts WHERE host=?', ('a',)).fetchone()
                self.assertEqual(json.loads(row['topology'])['simulation_cpus'], [0])
                self.assertFalse(row['enabled'])
                q.close()
                return SimpleNamespace(pid=123456789)
            with patch.object(fleet.exp, 'verify_freeze'), patch.object(fleet.exp, 'require_gates'), \
                 patch.object(fleet, 'topology', return_value={'jobs': 1, 'simulation_cpus': [0]}), \
                 patch.object(fleet.subprocess, 'Popen', side_effect=spawn):
                self.assertEqual(fleet.start(out, 'a', 'weak')['enqueued'], 1)

    def test_assignments_are_disjoint_complete_balanced_and_keep_cohorts(self):
        hosts = {'a': {'jobs': 3}, 'b': {'jobs': 14}, 'c': {'jobs': 30}}
        jobs = [{'execution_id': str(i), 'scenario': {'opponent': 1 if i < 200 else 6}}
                for i in range(400)]
        manifest = {'protocol_id': 'fixed', 'stage': 'controls', 'jobs': jobs}
        assigned = fleet.assign(manifest, hosts)
        self.assertEqual(assigned, fleet.assign(manifest, hosts))
        for cohort, expected in [('weak', set(map(str, range(200)))),
                                 ('strong', set(map(str, range(200, 400))))]:
            ids = [key for value in assigned.values() for key in value[cohort]]
            self.assertEqual(len(ids), len(set(ids)))
            self.assertEqual(set(ids), expected)
            self.assertGreater(len(assigned['c'][cohort]), len(assigned['b'][cohort]))
            self.assertGreater(len(assigned['b'][cohort]), len(assigned['a'][cohort]))

    def test_expired_coordinator_cannot_dispatch_and_recovery_needs_three_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            q = queue.Queue(Path(directory) / 'queue.sqlite')
            q.enqueue({'execution_id': 'one'})
            q.health('a', True); q.health('a', True)
            self.assertIsNone(q.claim('a', 0))
            q.health('a', True)
            q.db.execute('UPDATE hosts SET checked=?', (time.time() - 100,))
            self.assertIsNone(q.claim('a', 0))
            for _ in range(3): q.health('a', False)
            q.health('a', True); q.health('a', True)
            self.assertIsNone(q.claim('a', 0))
            q.health('a', True)
            self.assertEqual(q.claim('a', 0)['id'], 'one')
            q.enqueue({'execution_id': 'one'})
            self.assertIsNone(q.claim('a', 0))
            q.close()

    def test_completed_confirmation_publishes_final_without_pilot_budget(self):
        import maxima_win_report as report
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            values = {'protocol': {'protocol_id': 'fixed'},
                      'manifest': {'stage': 'confirmation', 'jobs': [], 'comparisons': []},
                      'assignments': {},
                      'supervisor': {'cohort': 'strong', 'started': time.time() - 1}}
            for name, value in values.items():
                (out / (name + '.json')).write_text(json.dumps(value))
            with patch.dict(fleet.exp.HOSTS, {}, clear=True), \
                 patch.object(report, 'publish_final') as final, \
                 patch.object(report, 'pilot_budget') as pilot:
                fleet.supervise(out, Path('/root'), Path('/out'))
            final.assert_called_once()
            pilot.assert_not_called()
            self.assertEqual(json.loads((out / 'STATUS.json').read_text())['status'], 'stage_complete')

    def test_stop_reaches_other_hosts_when_one_is_unreachable(self):
        with tempfile.TemporaryDirectory() as directory:
            seen = []
            def remote(host, *args, **kwargs):
                seen.append(host)
                if host == 'a': raise OSError('unreachable')
            with patch.dict(fleet.exp.HOSTS, {'a': 3, 'b': 3}, clear=True), patch.object(fleet, 'remote', remote):
                fleet.propagate_stop(Path(directory), Path('/root'), Path('/out'), 'routing mismatch')
            self.assertEqual(set(seen), {'a', 'b'})
            self.assertEqual(json.loads((Path(directory) / 'STOP_DISPATCH.json').read_text())['reason'], 'routing mismatch')


if __name__ == '__main__': unittest.main()
