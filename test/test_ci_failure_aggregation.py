#!/usr/bin/env python3
"""Verify independent CI failures are all reported and still fail the run."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


HERE = Path(__file__).resolve().parent


class CIFailureAggregationTests(unittest.TestCase):
    def test_multiple_failures_are_reported_in_one_run(self):
        with tempfile.TemporaryDirectory() as directory:
            summary = Path(directory) / 'summary.md'
            env = dict(os.environ, GITHUB_STEP_SUMMARY=str(summary))
            commands = subprocess.run([
                sys.executable, str(HERE / 'ci_run_commands.py'),
                '--check', 'first failure', 'exit 7',
                '--check', 'passing check', 'exit 0',
                '--check', 'second failure', 'exit 9',
            ], env=env, capture_output=True, text=True)
            self.assertEqual(commands.returncode, 1)
            self.assertIn('first failure (7)', commands.stdout)
            self.assertIn('second failure (9)', commands.stdout)
            self.assertIn('passing check', commands.stdout)

            env['CI_STEPS_JSON'] = json.dumps({
                'first': {'outcome': 'failure', 'conclusion': 'success'},
                'passing': {'outcome': 'success', 'conclusion': 'success'},
                'second': {'outcome': 'failure', 'conclusion': 'success'},
            })
            result = subprocess.run([
                sys.executable, str(HERE / 'ci_step_summary.py'),
            ], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn('- Failed: `first`', result.stdout)
            self.assertIn('- Failed: `second`', result.stdout)
            self.assertIn('first failure', summary.read_text())
            self.assertIn('second failure', summary.read_text())


if __name__ == '__main__':
    unittest.main()
