"""Contracts for the machine-wide CPU token pool (buildsys/jobserver.py)."""
import os
import signal
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
import jobserver  # noqa: E402

HOLDER = textwrap.dedent('''
    import os, sys, time
    sys.path.insert(0, {buildsys!r})
    import jobserver
    from pathlib import Path
    folder, log, hold = Path(sys.argv[1]), Path(sys.argv[2]), float(sys.argv[3])
    with jobserver.acquire(4, folder):
        with log.open('a') as out:
            out.write(f'start {{time.monotonic()}}\\n')
        time.sleep(hold)
        with log.open('a') as out:
            out.write(f'stop {{time.monotonic()}}\\n')
''')


@unittest.skipIf(os.name == 'nt', 'the pool is disabled on Windows')
class JobserverTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.folder = Path(self.directory.name) / 'tokens'
        self.folder.mkdir()
        self.script = Path(self.directory.name) / 'holder.py'
        self.script.write_text(HOLDER.format(buildsys=str(ROOT / 'buildsys')))
        self.environment = {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_JOBSERVER')}

    def tearDown(self):
        self.directory.cleanup()

    def spawn(self, log, hold):
        return subprocess.Popen([sys.executable, str(self.script), str(self.folder), str(log), str(hold)],
                                env=self.environment)

    def test_concurrency_never_exceeds_the_pool(self):
        log = Path(self.directory.name) / 'events'
        processes = [self.spawn(log, 0.15) for _ in range(16)]
        for process in processes:
            self.assertEqual(process.wait(timeout=60), 0)
        events = sorted((float(time_), kind) for kind, time_ in (line.split() for line in log.read_text().splitlines()))
        running = peak = 0
        for _, kind in events:
            running += 1 if kind == 'start' else -1
            peak = max(peak, running)
        self.assertLessEqual(peak, 4)
        self.assertEqual(sum(1 for _, kind in events if kind == 'start'), 16)

    def test_a_killed_holder_releases_its_token(self):
        log = Path(self.directory.name) / 'events'
        holders = [self.spawn(log, 60) for _ in range(4)]
        deadline = time.monotonic() + 30
        while (not log.exists() or log.read_text().count('start') < 4) and time.monotonic() < deadline:
            time.sleep(0.05)
        for holder in holders:
            holder.send_signal(signal.SIGKILL)
            holder.wait()
        started = time.monotonic()
        with jobserver.acquire(4, self.folder):
            self.assertLess(time.monotonic() - started, 5)

    def test_children_of_holders_and_disabled_pools_acquire_nothing(self):
        previous = dict(os.environ)
        try:
            os.environ[jobserver.HELD] = '1'
            self.assertIsNone(jobserver.acquire(1, self.folder).handle)
            del os.environ[jobserver.HELD]
            os.environ['GLOB2_JOBSERVER'] = '0'
            self.assertIsNone(jobserver.acquire(1, self.folder).handle)
            self.assertEqual(jobserver.child_environment({})[jobserver.HELD], '1')
        finally:
            os.environ.clear()
            os.environ.update(previous)


if __name__ == '__main__':
    unittest.main()
