"""deploy/update-host.sh: backup first, build before touching the running stack,
install the web client last, roll back when the new stack does not become healthy.

Runs the real script in a scratch git repository against a fake `docker` that logs
every call (no containers are started).

    python3 -m unittest test/deployment/test_update_host.py -v
"""
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENTRY_FILES = ('threaded/index.wasm', 'threaded/index.js', 'index.wasm', 'index.js', 'loader.js',
               'studio.html', 'generator-studio.html', 'index.html')

FAKE_DOCKER = r'''#!/bin/sh
# Logs each call; behaviour from FAKE_* variables.
echo "$*" >> "$FAKE_LOG"
echo "${COMPOSE_PROFILES:-}" >> "$FAKE_LOG.profiles"
case "$*" in
  *"ps --services --status running"*) echo "${FAKE_RUNNING_SERVICES:-postgres}" ;;
  *"config --format json"*) echo '{"services":{"postgres":{},"ai-map-worker":{"profiles":["ai-maps"]}}}' ;;
  *"ps --status running -q postgres"*) echo 0123abcd ;;
  *"exec -T postgres pg_dump"*) echo DUMP ;;
  *"config --images"*) printf 'glob2-platform:development\nglob2-relay:development\n' ;;
  *"compose"*" build"*) [ "${FAKE_BUILD_FAIL:-0}" = 1 ] && exit 1 ;;
  *"up -d"*)
    n=$(cat "$FAKE_LOG.up" 2>/dev/null || echo 0); n=$((n + 1)); echo "$n" > "$FAKE_LOG.up"
    [ "$n" = 1 ] && [ "${FAKE_UP_FAIL:-0}" = 1 ] && exit 1 ;;
esac
exit 0
'''


class UpdateHostTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='update-host-'))
        self.repo = self.dir / 'src'
        deploy = self.repo / 'deploy'
        deploy.mkdir(parents=True)
        for name in ('update-host.sh', 'build-web-client.sh', 'install-web-client.py'):
            shutil.copy(ROOT / 'deploy' / name, deploy / name)
        (deploy / 'sim_version.py').write_text('print("127-50-' + 'ab' * 32 + '")\n')
        (deploy / 'compose.yaml').write_text('name: fake\n')
        # What build-web-client.sh would leave in the build tree.
        release = self.repo / 'build/emscripten/client/release'
        (release / 'assets').mkdir(parents=True)
        (release / 'threaded').mkdir()
        (release / 'assets' / 'pack-new.data').write_text('new')
        for name in ENTRY_FILES:
            (release / name).write_text(f'new {name}')
        git = ['git', '-C', str(self.repo), '-c', 'user.email=t@example.org', '-c', 'user.name=t']
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        subprocess.run([*git, 'add', 'deploy'], check=True)
        subprocess.run([*git, 'commit', '-qm', 'release'], check=True)
        # The served web client of the running release.
        self.web = self.dir / 'web-client'
        (self.web / 'assets').mkdir(parents=True)
        (self.web / 'index.html').write_text('old index.html')
        self.config = self.dir / 'config'
        self.config.mkdir()
        self.env_file = self.config / 'host.env'
        self.env_file.write_text(f'GLOB2_WEB_CLIENT_DIR={self.web}\nGLOB2_BACKUP_KEEP=2\n')
        bin_dir = self.dir / 'bin'
        bin_dir.mkdir()
        docker = bin_dir / 'docker'
        docker.write_text(FAKE_DOCKER)
        docker.chmod(docker.stat().st_mode | stat.S_IEXEC)
        self.log = self.dir / 'docker.log'
        self.env = {**os.environ, 'PATH': f'{bin_dir}:{os.environ["PATH"]}', 'FAKE_LOG': str(self.log)}

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def run_script(self, **fake):
        return subprocess.run(['sh', str(self.repo / 'deploy/update-host.sh'), str(self.env_file)],
                              env={**self.env, **fake}, capture_output=True, text=True, timeout=60)

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def index(self, fragment):
        calls = self.calls()
        return next(i for i, call in enumerate(calls) if fragment in call)

    def backups(self):
        return sorted((self.dir / 'backups').iterdir())

    def test_backs_up_builds_swaps_then_installs_the_web_client(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        # Backup before any build, build before the swap.
        self.assertLess(self.index('pg_dump'), self.index('image tag glob2-platform:development'))
        self.assertLess(self.index('image tag'), self.index('run --rm'))  # web client build
        self.assertLess(self.index('run --rm'), self.index('up -d --wait'))
        self.assertLess(self.index(' build'), self.index('up -d --wait'))
        self.assertIn('--force-recreate', self.calls()[self.index('up -d --wait')])
        [backup] = self.backups()
        self.assertEqual((backup / 'glob2.dump').read_text(), 'DUMP\n')
        self.assertTrue((backup / 'web-client.tar.gz').stat().st_size > 0)
        # Installed after the new stack was up.
        self.assertEqual((self.web / 'index.html').read_text(), 'new index.html')
        self.assertEqual((self.web / 'studio.html').read_text(), 'new studio.html')
        self.assertEqual((self.web / 'generator-studio.html').read_text(), 'new generator-studio.html')
        self.assertIn('glob2-platform:development glob2-platform:previous', '\n'.join(self.calls()))

    def test_keeps_only_the_newest_backups(self):
        # Force three updates into the same timestamp, even on a slow runner.
        date = self.dir / "bin" / "date"
        date.write_text("#!/bin/sh\nprintf '20260101T000000Z\\n'\n")
        date.chmod(date.stat().st_mode | stat.S_IEXEC)
        for _ in range(3):
            self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual(len(self.backups()), 2)

    def test_a_failed_build_changes_nothing_that_runs(self):
        result = self.run_script(FAKE_BUILD_FAIL='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('the build failed; the running stack is unchanged', result.stderr)
        self.assertFalse(any('up -d' in call for call in self.calls()))
        self.assertEqual((self.web / 'index.html').read_text(), 'old index.html')

    def test_an_unhealthy_new_stack_is_rolled_back(self):
        result = self.run_script(FAKE_UP_FAIL='1')
        self.assertEqual(result.returncode, 1)
        calls = self.calls()
        ups = [call for call in calls if 'up -d' in call]
        self.assertEqual(len(ups), 2)
        self.assertIn('--no-build', ups[1])
        self.assertIn('--force-recreate', ups[1])
        self.assertIn('image tag glob2-platform:previous glob2-platform:development', calls)
        self.assertIn('rolled back; the previous release is running again', result.stderr)
        self.assertIn('pg_restore', result.stderr)
        # The web client of the running release stays.
        self.assertEqual((self.web / 'index.html').read_text(), 'old index.html')

    def test_preserves_running_optional_services(self):
        result = self.run_script(FAKE_RUNNING_SERVICES='postgres ai-map-worker')
        self.assertEqual(result.returncode, 0, result.stderr)
        profiles = self.log.with_name(self.log.name + '.profiles').read_text().splitlines()
        self.assertTrue(all('ai-maps' in value for value in profiles[2:]))

    def test_does_not_enable_idle_optional_services(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('ai-maps', self.log.with_name(self.log.name + '.profiles').read_text())

    def test_preserves_configured_profiles_alongside_running_services(self):
        with self.env_file.open('a') as stream:
            stream.write('COMPOSE_PROFILES=another-profile\n')
        result = self.run_script(FAKE_RUNNING_SERVICES='postgres ai-map-worker')
        self.assertEqual(result.returncode, 0, result.stderr)
        profiles = self.log.with_name(self.log.name + '.profiles').read_text().splitlines()
        self.assertTrue(all(value == 'another-profile,ai-maps' for value in profiles[2:]))

    def head(self):
        return subprocess.run(['git', '-C', str(self.repo), 'rev-parse', 'HEAD'], check=True,
                              capture_output=True, text=True).stdout.strip()

    def commit_next_release(self):
        (self.repo / 'deploy' / 'compose.yaml').write_text('name: fake-next\n')
        subprocess.run(['git', '-C', str(self.repo), '-c', 'user.email=t@example.org', '-c', 'user.name=t',
                        'commit', '-qam', 'next release'], check=True)
        self.log.with_name(self.log.name + '.up').unlink(missing_ok=True)

    def test_records_the_deployed_revision(self):
        self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual((self.dir / 'deployed-revision').read_text().strip(), self.head())

    def test_rolls_back_to_the_deployed_revision_not_the_checked_out_one(self):
        # The documented redeploy checks out the new release before running the
        # script; the rollback must still restore the release that was running.
        self.assertEqual(self.run_script().returncode, 0)
        deployed = self.head()
        self.commit_next_release()
        self.assertNotEqual(self.head(), deployed)
        result = self.run_script(FAKE_UP_FAIL='1')
        self.assertEqual(result.returncode, 1)
        self.assertIn(f'rolling back to {deployed[:7]}', result.stderr)
        self.assertEqual(self.head(), deployed)
        self.assertEqual((self.repo / 'deploy' / 'compose.yaml').read_text(), 'name: fake\n')
        [*_, backup] = self.backups()
        self.assertEqual((backup / 'revision').read_text().strip(), deployed)
        # The record still names the release that runs.
        self.assertEqual((self.dir / 'deployed-revision').read_text().strip(), deployed)

    def test_a_failed_build_restores_the_deployed_checkout(self):
        self.assertEqual(self.run_script().returncode, 0)
        deployed = self.head()
        self.commit_next_release()
        result = self.run_script(FAKE_BUILD_FAIL='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f'still {deployed[:7]}', result.stderr)
        self.assertEqual(self.head(), deployed)

    def test_without_a_record_the_checkout_is_assumed_to_run(self):
        result = self.run_script(FAKE_UP_FAIL='1')
        self.assertEqual(result.returncode, 1)
        self.assertIn('no record of the deployed revision', result.stderr)
        self.assertFalse((self.dir / 'deployed-revision').exists())


if __name__ == '__main__':
    unittest.main()
