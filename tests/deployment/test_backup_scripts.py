"""deploy/backup-to-gcs.sh and deploy/restore-backup.sh: the daily upload, the
weekly/monthly promotion that the bucket's lifecycle rules rely on, and the
restore script's refusal to touch the live database.

Runs the real scripts against a fake `docker` and a fake `gcloud` that log every
call (no containers, no Cloud Storage).

    python3 -m unittest tests/deployment/test_backup_scripts.py -v
"""
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

FAKE_DOCKER = r'''#!/bin/sh
echo "docker $*" >> "$FAKE_LOG"
case "$*" in
  *"pg_dump"*) echo DUMP ;;
  *"pg_restore --list"*) cat > /dev/null; [ "${FAKE_DUMP_BAD:-0}" = 1 ] && exit 1 ;;
  *"FROM accounts WHERE status = 'deleted'"*) printf '%s\n' 11111111-1111-1111-1111-111111111111 ;;
esac
exit 0
'''

FAKE_GCLOUD = r'''#!/bin/sh
echo "gcloud $*" >> "$FAKE_LOG"
case "$*" in
  "storage ls "*"/weekly/") [ -n "${FAKE_WEEKLY:-}" ] && printf '%s\n' $FAKE_WEEKLY ;;
  "storage ls "*"/monthly/") [ -n "${FAKE_MONTHLY:-}" ] && printf '%s\n' $FAKE_MONTHLY ;;
esac
exit 0
'''

BUCKET = 'gs://glob2-backups-test'


def executable(path: Path, text: str) -> None:
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class BackupScriptTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='backup-'))
        self.addCleanup(shutil.rmtree, self.dir)
        bin_dir = self.dir / 'bin'
        bin_dir.mkdir()
        executable(bin_dir / 'docker', FAKE_DOCKER)
        executable(bin_dir / 'gcloud', FAKE_GCLOUD)
        config = self.dir / 'config'
        config.mkdir()
        self.env_file = config / 'staging.env'
        self.env_file.write_text(f'POSTGRES_PASSWORD=pw\nGLOB2_BACKUP_BUCKET={BUCKET[5:]}\n')
        self.log = self.dir / 'calls.log'
        self.env = {**os.environ, 'PATH': f'{bin_dir}:{os.environ["PATH"]}', 'FAKE_LOG': str(self.log)}

    def backup(self, now: str, **fake: str) -> subprocess.CompletedProcess:
        env = {**self.env, 'GLOB2_BACKUP_NOW': now, **fake}
        return subprocess.run([str(ROOT / 'deploy/backup-to-gcs.sh'), str(self.env_file)],
                              env=env, capture_output=True, text=True)

    def calls(self) -> list[str]:
        return self.log.read_text().splitlines() if self.log.exists() else []

    def uploads(self) -> list[str]:
        return [c for c in self.calls() if c.startswith('gcloud storage cp')]

    def test_first_backup_goes_to_every_tier(self):
        result = self.backup('20261003T031700Z')
        self.assertEqual(result.returncode, 0, result.stderr)
        uploads = self.uploads()
        self.assertEqual(len(uploads), 3)
        self.assertTrue(uploads[0].endswith(f'{BUCKET}/daily/'))
        self.assertIn('glob2-20261003T031700Z.dump', uploads[0])
        self.assertIn('glob2-20261003T031700Z.deleted-accounts.txt', uploads[0])
        self.assertTrue(uploads[1].endswith(f'{BUCKET}/weekly/'))
        self.assertIn(f'{BUCKET}/daily/glob2-20261003T031700Z.dump', uploads[1])
        self.assertTrue(uploads[2].endswith(f'{BUCKET}/monthly/'))
        # The scratch copies are removed after the upload.
        self.assertEqual(list((self.dir / 'backups/scheduled').iterdir()), [])

    def test_weekly_after_seven_days_monthly_once_a_month(self):
        weekly = f'{BUCKET}/weekly/glob2-20261003T031700Z.dump'
        monthly = f'{BUCKET}/monthly/glob2-20261003T031700Z.dump'
        for now, tiers in (('20261009T031700Z', []),                    # 6 days later
                           ('20261010T031700Z', ['weekly']),            # 7 days later
                           ('20261101T031700Z', ['weekly', 'monthly'])):
            with self.subTest(now=now):
                self.log.unlink(missing_ok=True)
                result = self.backup(now, FAKE_WEEKLY=weekly, FAKE_MONTHLY=monthly)
                self.assertEqual(result.returncode, 0, result.stderr)
                promoted = [u.rsplit('/', 2)[-2] for u in self.uploads()[1:]]
                self.assertEqual(promoted, tiers)

    def test_unreadable_dump_is_not_uploaded(self):
        result = self.backup('20261003T031700Z', FAKE_DUMP_BAD='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.uploads(), [])

    def test_needs_a_bucket(self):
        self.env_file.write_text('POSTGRES_PASSWORD=pw\n')
        result = self.backup('20261003T031700Z')
        self.assertEqual(result.returncode, 2)
        self.assertIn('GLOB2_BACKUP_BUCKET', result.stderr)

    def test_restore_refuses_the_live_database(self):
        for target in ('glob2', 'postgres', 'Bad-Name'):
            with self.subTest(target=target):
                result = subprocess.run(
                    [str(ROOT / 'deploy/restore-backup.sh'), str(self.env_file),
                     f'{BUCKET}/daily/glob2-20261003T031700Z.dump', target],
                    env=self.env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
        self.assertFalse(any('pg_restore' in c or 'CREATE DATABASE' in c for c in self.calls()))

    def test_lifecycle_matches_the_documented_retention(self):
        rules = json.loads((ROOT / 'deploy/gcs-backup-lifecycle.json').read_text())['rule']
        ages = {r['condition']['matchesPrefix'][0]: r['condition']['age'] for r in rules
                if r['action']['type'] == 'Delete'}
        self.assertEqual(ages, {'daily/': 7, 'weekly/': 35})

    def test_timer_units_install_with_paths(self):
        units = self.dir / 'units'
        units.mkdir()
        self.env_file.write_text(f'GLOB2_BACKUP_BUCKET={BUCKET[5:]}\n')
        result = subprocess.run(
            [str(ROOT / 'deploy/install-backup-timer.sh'), str(self.env_file), 'glob2ops'],
            env={**self.env, 'GLOB2_SYSTEMD_DIR': str(units)}, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        service = (units / 'glob2-backup.service').read_text()
        self.assertIn('User=glob2ops', service)
        self.assertIn(f'{ROOT}/deploy/backup-to-gcs.sh {self.env_file}', service)
        self.assertNotIn('@', service.split('[Unit]')[1])
        self.assertIn('Persistent=true', (units / 'glob2-backup.timer').read_text())


if __name__ == '__main__':
    unittest.main()
