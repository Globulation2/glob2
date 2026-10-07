import json
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_with_benchmark_governor as governor


class GovernorTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.policy = self.root / 'policy0'
        self.policy.mkdir()
        (self.policy / 'scaling_governor').write_text('powersave')
        self.audit = self.root / 'audit.json'
        self.changes = []

    def run_wrapper(self, wait_result=0, change_error=None, cleanup_error=None, dry_run=False):
        def change(policy, value):
            self.changes.append(value)
            # A failed verification can follow a successful write.
            (policy / 'scaling_governor').write_text(value)
            if change_error:
                change_error(value)
        argv = ['wrapper', '--audit', str(self.audit)]
        if dry_run:
            argv.append('--dry-run')
        argv += ['--', 'engine']
        with patch.object(governor.sys, 'argv', argv), \
                patch.object(governor.os, 'geteuid', return_value=1000), \
                patch.object(governor, 'policies', return_value={self.policy: 'powersave'}), \
                patch.object(governor, 'set_governor', side_effect=change), \
                patch.object(governor.subprocess, 'Popen') as popen, \
                patch.object(governor.os, 'killpg', side_effect=cleanup_error or ProcessLookupError()):
            popen.return_value.pid = 98765
            popen.return_value.wait.side_effect = wait_result if isinstance(wait_result, BaseException) else None
            popen.return_value.wait.return_value = wait_result
            return governor.main()

    def report(self):
        return json.loads(self.audit.read_text())

    def test_success_and_nonzero_exit_restore(self):
        for code in (0, 7):
            with self.subTest(code=code):
                self.audit = self.root / f'{code}.json'
                self.changes = []
                self.assertEqual(self.run_wrapper(wait_result=code), code)
                self.assertEqual(self.changes, ['performance', 'powersave'])
                self.assertTrue(self.report()['restored'])
                self.assertEqual(self.report()['exit_code'], code)

    def test_interrupt_and_timeout_restore_and_preserve_failure(self):
        for error in (KeyboardInterrupt(), subprocess.TimeoutExpired('engine', 1)):
            with self.subTest(error=type(error).__name__):
                self.audit = self.root / f'{type(error).__name__}.json'
                with self.assertRaises(type(error)):
                    self.run_wrapper(wait_result=error)
                self.assertTrue(self.report()['restored'])
                self.assertEqual(self.report()['state'], 'failed')

    def test_partial_stabilization_failure_restores_touched_policy(self):
        def fail(value):
            if value == 'performance':
                raise RuntimeError('verification failed')
        with self.assertRaisesRegex(RuntimeError, 'verification failed'):
            self.run_wrapper(change_error=fail)
        self.assertTrue(self.report()['restored'])
        self.assertEqual(self.changes, ['performance', 'powersave'])

    def test_restore_failure_is_fatal_and_recorded(self):
        def fail(value):
            if value == 'powersave':
                raise RuntimeError('restore denied')
        previous = signal.getsignal(signal.SIGTERM)
        with self.assertRaisesRegex(RuntimeError, 'Governor restoration failed'):
            self.run_wrapper(change_error=fail)
        self.assertFalse(self.report()['restored'])
        self.assertIn('restore denied', self.report()['restoration_errors'][0])
        self.assertEqual(signal.getsignal(signal.SIGTERM), previous)

    def test_child_cleanup_failure_is_fatal_but_still_restores(self):
        with self.assertRaisesRegex(RuntimeError, 'child cleanup failed'):
            self.run_wrapper(cleanup_error=PermissionError('cannot inspect group'))
        self.assertTrue(self.report()['restored'])
        self.assertIn('cannot inspect group', self.report()['child_cleanup_error'])

    def test_dry_run_never_writes_governor(self):
        self.assertEqual(self.run_wrapper(dry_run=True), 0)
        self.assertEqual(self.changes, [])
        self.assertEqual(self.report()['state'], 'prepared')

    def test_shared_policy_outside_selected_cpus_is_rejected(self):
        cpu_root = self.root / 'cpu'
        policy = cpu_root / 'cpufreq/policy0'
        policy.mkdir(parents=True)
        (policy / 'related_cpus').write_text('0 1')
        (cpu_root / 'cpu0').mkdir()
        (cpu_root / 'cpu0/cpufreq').symlink_to(policy)
        with patch.object(governor, 'CPU_ROOT', cpu_root):
            with self.assertRaisesRegex(RuntimeError, 'beyond authorized CPUs'):
                governor.policies([0])


if __name__ == '__main__':
    unittest.main()
