"""Every workflow job that runs steps has a timeout.

Without one a hung job holds its runner for GitHub's six-hour limit, and queued
runs behind it are cancelled. Set `timeout-minutes` to two or three times the
job's slowest recent successful run. Jobs that call a reusable workflow (`uses:`)
cannot take a timeout; the called workflow's jobs carry their own.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOSTED_LIMIT = 360


def jobs(text):
    """Job name -> its block, from the top-level `jobs:` mapping (two-space keys)."""
    body = text.split('\njobs:\n', 1)[1] if '\njobs:\n' in text else ''
    found = {}
    for match in re.finditer(r'^  ([A-Za-z0-9_-]+):\n((?:(?:    .*|\s*)\n)*)', body + '\n', re.M):
        found[match.group(1)] = match.group(2)
    return found


class JobTimeoutTest(unittest.TestCase):
    def test_every_steps_job_has_a_bounded_timeout(self):
        missing, excessive, checked = [], [], 0
        for path in sorted((ROOT / '.github/workflows').glob('*.yml')):
            for name, block in jobs(path.read_text()).items():
                if re.search(r'^    uses:', block, re.M):
                    continue
                checked += 1
                where = f'{path.name}: {name}'
                timeout = re.search(r'^    timeout-minutes:\s*(\S+)', block, re.M)
                if not timeout:
                    missing.append(where)
                elif timeout.group(1).isdigit() and int(timeout.group(1)) > HOSTED_LIMIT:
                    excessive.append(f'{where} ({timeout.group(1)})')
        self.assertGreater(checked, 100, 'the workflow parser found too few jobs')
        self.assertEqual(missing, [], 'jobs without timeout-minutes')
        self.assertEqual(excessive, [], f'timeouts above the {HOSTED_LIMIT}-minute hosted limit')

    def test_parser_sees_a_missing_timeout(self):
        text = 'on: push\njobs:\n  a:\n    runs-on: x\n    steps:\n      - run: true\n  b:\n    uses: ./w.yml\n'
        found = jobs(text)
        self.assertEqual(set(found), {'a', 'b'})
        self.assertNotIn('timeout-minutes', found['a'])


if __name__ == '__main__':
    unittest.main()
