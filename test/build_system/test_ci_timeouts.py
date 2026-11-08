"""Every workflow job that runs steps has a timeout.

Without one a hung job holds its runner for GitHub's six-hour limit, and queued
runs behind it are cancelled. Set `timeout-minutes` to two or three times the
job's slowest recent successful run. Jobs that call a reusable workflow (`uses:`)
cannot take a timeout; the called workflow's jobs carry their own.
"""
import glob
import unittest
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
HOSTED_LIMIT = 360


class JobTimeoutTest(unittest.TestCase):
    def test_every_steps_job_has_a_bounded_timeout(self):
        missing, excessive = [], []
        for path in sorted(glob.glob(str(ROOT / '.github/workflows/*.yml'))):
            jobs = yaml.safe_load(Path(path).read_text()).get('jobs') or {}
            for name, job in jobs.items():
                if 'uses' in job:
                    continue
                where = f'{Path(path).name}: {name}'
                timeout = job.get('timeout-minutes')
                if timeout is None:
                    missing.append(where)
                elif isinstance(timeout, int) and timeout > HOSTED_LIMIT:
                    excessive.append(f'{where} ({timeout})')
        self.assertEqual(missing, [], 'jobs without timeout-minutes')
        self.assertEqual(excessive, [], f'timeouts above the {HOSTED_LIMIT}-minute hosted limit')


if __name__ == '__main__':
    unittest.main()
