"""Explicit local/SSH caller allocation contracts; no machine-wide discovery."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.cli import configure_hosts, parse_cpu_ids, positive_integer
from tools.tournaments.local import execute_jobs, run_job
from tools.tournaments.model import job


class TournamentCpuArguments(unittest.TestCase):
    def test_run_and_collect_accept_the_same_explicit_budget(self):
        from tools.tournaments import __main__ as cli
        import contextlib, io
        for command in ('run', 'collect'):
            coordinator = Mock(); coordinator.run.return_value = {}
            with patch.object(sys, 'argv', ['tournaments', command, '/experiment', '--hosts', '/hosts',
                    '--cpu-ids', '2,3,6,7', '--jobs', '2', '--compute-participants', '1']), \
                 patch.object(cli, 'read_json', return_value=[{'name': 'local', 'transport': 'local'}]), \
                 patch.object(cli, 'Coordinator', return_value=coordinator), contextlib.redirect_stdout(io.StringIO()):
                cli.main()
            host = coordinator.run.call_args.args[0][0]
            self.assertEqual((host['cpu_ids'], host['slots'], host['participants_per_game']), ([2, 3, 6, 7], 2, 1))

    def test_strict_cpu_ids_and_counts(self):
        self.assertEqual(parse_cpu_ids('7,2,0'), [7, 2, 0])
        for value in ('', '1,1', '-1', '1,', '1-3', '1, 2', '１'):
            with self.subTest(value=value), self.assertRaises(ValueError): parse_cpu_ids(value)
        for value in (True, 0, -1, '1x', '-2', 1 << 63):
            with self.subTest(value=value), self.assertRaises(ValueError): positive_integer(value)

    def test_budget_validation_does_not_mutate_inputs_or_claim_legacy_host(self):
        hosts = [{'name': 'local', 'transport': 'local', 'slots': 2, 'cpu_ids': [7, 2, 5, 1]}]
        before = copy.deepcopy(hosts)
        self.assertEqual(configure_hosts(hosts), hosts)
        self.assertEqual(hosts, before)
        self.assertEqual(configure_hosts([{'slots': 1}]), [{'slots': 1}])
        for value in ([{'cpu_ids': [1]}], [{'slots': 2, 'cpu_ids': [1]}], [{'slots': 1, 'participants_per_game': 1}]):
            with self.assertRaises(ValueError): configure_hosts(value)

    def test_known_local_claims_must_be_complete_and_disjoint(self):
        hosts = [{'transport': 'local', 'slots': 1, 'cpu_ids': [2, 7]},
                 {'transport': 'local', 'slots': 1, 'cpu_ids': [7, 8]}]
        with self.assertRaisesRegex(ValueError, 'overlapping'): configure_hosts(hosts)
        hosts[1].pop('cpu_ids')
        with self.assertRaisesRegex(ValueError, 'every local'): configure_hosts(hosts)
        hosts[1]['cpu_ids'] = [3, 8]
        self.assertEqual(configure_hosts(hosts), hosts)
        hosts[1]['transport'] = 'ssh'; hosts[1]['cpu_ids'] = [2, 7]
        self.assertEqual(configure_hosts(hosts), hosts)  # SSH aliases do not prove identity.
        with self.assertRaises(ValueError): configure_hosts(hosts, cpu_ids=[1])

    def test_local_validates_before_packaging_and_freezes_manifest(self):
        with patch('tools.tournaments.local.supplied_binary') as bundle:
            with self.assertRaises(ValueError):
                run_job('/bin', '/data', '/out', 'game', config={}, seeds={}, participants_per_game=1)
            bundle.assert_not_called()
        coordinator = Mock(); coordinator.run.return_value = {'jobs': {'completed': 1}}
        value = job('game', 'a' * 64); before = copy.deepcopy(value)
        with tempfile.TemporaryDirectory() as tmp, \
             patch('tools.tournaments.local.Coordinator.submit', return_value=coordinator) as submit, \
             patch('tools.tournaments.local.Transport'), patch('tools.tournaments.local.Results', return_value='results'):
            self.assertEqual(execute_jobs(tmp, [value], '/bundle', 2, cpu_ids=[2, 3, 7, 8], participants_per_game=1), 'results')
            host = coordinator.run.call_args.args[0][0]
            self.assertEqual(host['cpu_ids'], [2, 3, 7, 8]); self.assertEqual(host['participants_per_game'], 1)
            manifest = submit.call_args.args[1]
            self.assertEqual(manifest['settings']['caller_allocation_plan']['allocations'][0]['participants'], 1)
        self.assertEqual(value, before)


if __name__ == '__main__': unittest.main()
