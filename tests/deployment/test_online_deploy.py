"""deploy/online-deploy.sh and deploy/online_remote.py (.github/workflows/deploy-online.yml).

Runs the host driver against a scratch host directory: a git "origin", a clone
of it as the host checkout and a fake update-host.sh, so nothing is deployed.

    python3 -m unittest tests/deployment/test_online_deploy.py -v
"""
import datetime
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'deploy/online-deploy.sh'
spec = importlib.util.spec_from_file_location('online_remote', ROOT / 'deploy/online_remote.py')
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)

# Prints what the real script prints, records the revision like it, and fails
# the way FAKE_RESULT says.
FAKE_UPDATE_HOST = r'''#!/bin/sh
set -eu
env_file=$1
ref=${2:-}
[ -n "$ref" ] && git checkout --quiet --detach "$ref"
echo "revision: $(git rev-parse --short HEAD) $(git log -1 --format=%s)"
echo "sim version: 128-51-abc"
echo "args: $*" >> "$FAKE_LOG"
sleep "${FAKE_SLEEP:-0}"
case "${FAKE_RESULT:-ok}" in
  ok)
    git rev-parse HEAD > "$(dirname "$env_file")/../deployed-revision"
    echo "update-host: $(git rev-parse --short HEAD) is running; backup in /backups/1"
    ;;
  build) echo "update-host: the build failed; the running stack is unchanged (still x)" >&2; exit 1 ;;
  rollback) echo "update-host: rolled back; the previous release is running again" >&2; exit 1 ;;
  rollback-failed) echo "update-host: the previous release did not become healthy either" >&2; exit 1 ;;
esac
'''

FAKE_SMOKE = r'''import sys, os
open(os.environ['FAKE_LOG'], 'a').write('smoke: ' + ' '.join(sys.argv[1:]) + '\n')
sys.exit(int(os.environ.get('FAKE_SMOKE_EXIT', '0')))
'''


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), '-c', 'user.email=t@example.org', '-c', 'user.name=t',
                           *args], check=True, capture_output=True, text=True).stdout.strip()


class HostDriverTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='online-deploy-'))
        origin = self.dir / 'origin'
        (origin / 'deploy').mkdir(parents=True)
        (origin / 'tests/deployment').mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(origin)], check=True)
        script = origin / 'deploy/update-host.sh'
        script.write_text(FAKE_UPDATE_HOST)
        script.chmod(0o755)
        (origin / 'tests/deployment/platform_stack_smoke.py').write_text(FAKE_SMOKE)
        git(origin, 'add', '.')
        git(origin, 'commit', '-qm', 'old')
        self.old = git(origin, 'rev-parse', 'HEAD')
        (origin / 'new.txt').write_text('new')
        git(origin, 'add', '.')
        git(origin, 'commit', '-qm', 'new release')
        self.new = git(origin, 'rev-parse', 'HEAD')
        self.host = self.dir / 'opt'
        (self.host / 'config').mkdir(parents=True)
        self.env_file = self.host / 'config/staging.env'
        self.env_file.write_text('POSTGRES_PASSWORD=x\n')
        subprocess.run(['git', 'clone', '-q', str(origin), str(self.host / 'src')], check=True)
        git(self.host / 'src', 'checkout', '-q', '--detach', self.old)
        # pgrep/flock stand-ins: the host is busy when FAKE_BUSY=1.
        bin_dir = self.dir / 'bin'
        bin_dir.mkdir()
        (bin_dir / 'pgrep').write_text('#!/bin/sh\n[ "${FAKE_BUSY:-0}" = 1 ]\n')
        (bin_dir / 'pgrep').chmod(0o755)
        self.log = self.dir / 'calls.log'
        self.env = dict(os.environ, PATH=f'{bin_dir}:{os.environ["PATH"]}', FAKE_LOG=str(self.log))

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def driver(self, *args, check=True, **env):
        result = subprocess.run(['sh', '-s', '--', *args], input=DRIVER.read_text(), capture_output=True,
                                text=True, env=dict(self.env, **env))
        if check:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def status(self, run):
        out = self.driver('status', str(self.env_file), run).stdout
        return dict(line.split('=', 1) for line in out.splitlines())

    def wait(self, run):
        for _ in range(100):
            status = self.status(run)
            if status['state'] != 'running':
                return status
            time.sleep(0.1)
        self.fail('the deploy did not finish')

    def test_deploys_detached_and_reports(self):
        record = self.host / 'deployed-revision'
        record.write_text(self.old + '\n')
        self.assertEqual(self.driver('current', str(self.env_file)).stdout.split(),
                         [f'deployed={self.old}', 'busy=no'])
        out = self.driver('start', str(self.env_file), self.new, 'gh-1-1', FAKE_SLEEP='0.5').stdout
        self.assertIn('started', out)
        self.assertIn('new release', out)
        status = self.wait('gh-1-1')
        self.assertEqual((status['state'], status['exit'], status['rollback']), ('done', '0', 'none'))
        self.assertEqual(status['commit'], self.new)
        self.assertEqual(status['previous'], self.old)
        self.assertEqual(status['deployed'], self.new)
        self.assertEqual(status['backup'], '/backups/1')
        self.assertEqual(status['sim_version'], '128-51-abc')
        self.assertTrue(status['finished'])
        self.assertIn(f'args: {self.env_file} {self.new}', self.log.read_text())
        self.assertIn('DEPLOY EXIT 0', self.driver('log', str(self.env_file), 'gh-1-1', '5').stdout)

    def test_current_says_whether_the_deployed_revision_contains_a_commit(self):
        record = self.host / 'deployed-revision'
        record.write_text(self.new + '\n')
        for commit, expected in ((self.old, 'contains=yes'), (self.new, 'contains=yes')):
            self.assertIn(expected, self.driver('current', str(self.env_file), commit).stdout.split())
        record.write_text(self.old + '\n')
        self.assertIn('contains=no', self.driver('current', str(self.env_file), self.new).stdout.split())

    def test_records_the_running_revision_before_moving_the_checkout(self):
        self.driver('start', str(self.env_file), self.new, 'gh-2-1', FAKE_RESULT='build')
        status = self.wait('gh-2-1')
        self.assertEqual(status['previous'], self.old)
        self.assertEqual((status['exit'], status['rollback']), ('1', 'unchanged'))
        self.assertEqual((self.host / 'deployed-revision').read_text().strip(), self.old)

    def test_reports_rollbacks(self):
        for result, expected in (('rollback', 'rolled-back'), ('rollback-failed', 'failed')):
            run = f'gh-{result}'
            self.driver('start', str(self.env_file), self.new, run, FAKE_RESULT=result)
            self.assertEqual(self.wait(run)['rollback'], expected)

    def test_refuses_while_another_deploy_runs(self):
        result = self.driver('start', str(self.env_file), self.new, 'gh-3-1', check=False, FAKE_BUSY='1')
        self.assertEqual(result.returncode, 75)
        self.assertIn('another deploy is running', result.stderr)
        self.assertFalse((self.host / 'deploys/gh-3-1').exists())
        self.assertEqual(git(self.host / 'src', 'rev-parse', 'HEAD'), self.old)
        self.assertIn('busy=yes', self.driver('current', str(self.env_file), FAKE_BUSY='1').stdout)

    @unittest.skipUnless(shutil.which('flock'), 'needs util-linux flock (the host has it)')
    def test_refuses_while_the_deploy_lock_is_held(self):
        self.driver('start', str(self.env_file), self.new, 'gh-7', FAKE_SLEEP='2')
        time.sleep(0.5)
        self.assertIn('busy=yes', self.driver('current', str(self.env_file)).stdout)
        result = self.driver('start', str(self.env_file), self.new, 'gh-8', check=False)
        self.assertEqual(result.returncode, 75)
        self.assertEqual(self.wait('gh-7')['exit'], '0')
        self.assertIn('busy=no', self.driver('current', str(self.env_file)).stdout)

    def test_rejects_bad_input(self):
        for args in (('start', str(self.env_file), 'master', 'gh-4'),
                     ('start', str(self.env_file), self.new[:12], 'gh-4'),
                     ('start', str(self.env_file), self.new, '../x'),
                     ('status', str(self.env_file), 'a b'),
                     ('start', str(self.dir / 'missing.env'), self.new, 'gh-4')):
            with self.subTest(args=args):
                self.assertNotEqual(self.driver(*args, check=False).returncode, 0)
        self.assertFalse((self.host / 'deploys/gh-4').exists())
        self.driver('start', str(self.env_file), self.new, 'gh-5')
        self.wait('gh-5')
        self.assertIn('already started', self.driver('start', str(self.env_file), self.new, 'gh-5').stdout)
        self.assertNotEqual(self.driver('start', str(self.env_file), self.old, 'gh-5', check=False).returncode, 0)
        self.assertEqual(self.status('never')['state'], 'missing')

    def test_smoke_runs_against_the_deployed_checkout(self):
        self.driver('start', str(self.env_file), self.new, 'gh-6')
        self.wait('gh-6')
        self.driver('smoke', str(self.env_file), 'gh-6', 'https://example.org')
        calls = self.log.read_text()
        self.assertIn(f'smoke: --attach glob2-platform --env-file {self.env_file} '
                      f'--log-dir {self.host}/deploys/gh-6/smoke --website https://example.org', calls)
        self.assertEqual((self.host / 'deploys/gh-6/smoke-exit').read_text().strip(), '0')
        failed = self.driver('smoke', str(self.env_file), 'gh-6', check=False, FAKE_SMOKE_EXIT='1')
        self.assertEqual(failed.returncode, 1)
        git(self.host / 'src', 'checkout', '-q', '--detach', self.old)
        self.assertEqual(self.driver('smoke', str(self.env_file), 'gh-6', check=False).returncode, 2)


class InstanceKeyTests(unittest.TestCase):
    now = datetime.datetime(2026, 10, 3, 12, tzinfo=datetime.timezone.utc)

    def line(self, hours, user='bradley'):
        return remote.key_line(user, 'ssh-ed25519 AAAAkey glob2-online-deploy\n',
                               self.now + datetime.timedelta(hours=hours))

    def test_key_line_is_a_guest_agent_expiring_key(self):
        self.assertEqual(self.line(3), 'bradley:ssh-ed25519 AAAAkey google-ssh '
                         '{"userName":"glob2-online-deploy","expireOn":"2026-10-03T15:00:00+0000"}')

    def test_adds_and_removes_only_its_own_keys(self):
        other = 'alice:ssh-rsa AAAAother alice'
        metadata = {'fingerprint': 'f1', 'items': [{'key': 'startup-script', 'value': 'x'},
                                                   {'key': 'ssh-keys', 'value': other}]}
        added = remote.edit_keys(metadata, self.line(3), self.now)
        self.assertEqual(added['fingerprint'], 'f1')
        self.assertEqual(added['items'][0], {'key': 'startup-script', 'value': 'x'})
        self.assertEqual(added['items'][1]['value'], other + '\n' + self.line(3))
        # An expired key of an earlier run goes when a new one is added; a live one stays.
        stale = remote.edit_keys(added, self.line(-1), self.now)
        self.assertEqual(stale['items'][1]['value'].splitlines(), [other, self.line(3), self.line(-1)])
        self.assertEqual(remote.edit_keys(stale, self.line(2), self.now)['items'][1]['value'].splitlines(),
                         [other, self.line(3), self.line(2)])
        removed = remote.edit_keys(stale, None, self.now)
        self.assertEqual(removed['items'], metadata['items'])

    def test_creates_and_drops_the_ssh_keys_item(self):
        added = remote.edit_keys({'fingerprint': 'f'}, self.line(1), self.now)
        self.assertEqual(added['items'], [{'key': 'ssh-keys', 'value': self.line(1)}])
        self.assertEqual(remote.edit_keys(added, None, self.now), {'items': [], 'fingerprint': 'f'})

    def test_ssh_goes_through_iap(self):
        class Args:
            instance, project, zone, user = 'vm', 'p', 'z', 'bradley'
        command = remote.ssh_command(Args, '/k', 'sh -s -- current /e')
        self.assertIn('ProxyCommand=gcloud compute start-iap-tunnel vm 22 --listen-on-stdin '
                      '--project=p --zone=z --verbosity=error', command)
        self.assertEqual(command[-2:], ['bradley@vm', 'sh -s -- current /e'])
        self.assertEqual(remote.quote("it's"), "'it'\\''s'")
        self.assertEqual(remote.quote('https://a.b/c'), 'https://a.b/c')
        self.assertEqual(remote.quote(''), "''")


if __name__ == '__main__':
    unittest.main()
