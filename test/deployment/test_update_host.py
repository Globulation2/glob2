"""deploy/update-host.sh: backup first, build before touching the running stack,
install the web client last, roll back when the new stack does not become healthy.

Runs the real script in a scratch git repository against a fake `docker` that logs
every call (no containers are started).

    python3 -m unittest test/deployment/test_update_host.py -v
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
ENTRY_FILES = ('threaded/index.wasm', 'threaded/index.js', 'index.wasm', 'index.js', 'loader.js',
               'studio.html', 'generator-studio.html', 'index.html')

FAKE_DOCKER = r'''#!/bin/sh
# Logs each call; behaviour from FAKE_* variables.
echo "$*" >> "$FAKE_LOG"
echo "${COMPOSE_PROFILES:-}" >> "$FAKE_LOG.profiles"
case "$*" in
  *"ps --services --status running"*) echo "${FAKE_RUNNING_SERVICES:-postgres}" ;;
  *"config --format json"*) echo '{"services":{"postgres":{},"ai-map-worker":{"profiles":["ai-maps"],"image":"glob2-ai-map-worker:development","build":{"target":"ai-map-worker"}},"platform-api":{"image":"glob2-platform:development","build":{"target":"platform"}}}}' ;;
  "create "*) echo c0ffee ;;
  "cp "*) mkdir -p "$3" && cp -R "$FAKE_WEB_TREE/." "$3" ;;
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
        for name in ('update-host.sh', 'build-web-client.sh', 'install-web-client.py', 'prebuilt_images.py'):
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
        self.env = {**os.environ, 'PATH': f'{bin_dir}:{os.environ["PATH"]}', 'FAKE_LOG': str(self.log), 'GLOB2_DESIGN_SYSTEM_SHA': 'ab' * 20}

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
        self.assertEqual((backup / 'target-design-system-revision').read_text().strip(), 'ab' * 20)
        self.assertTrue((backup / 'web-client.tar.gz').stat().st_size > 0)
        # Installed after the new stack was up.
        self.assertEqual((self.web / 'index.html').read_text(), 'new index.html')
        self.assertEqual((self.web / 'studio.html').read_text(), 'new studio.html')
        self.assertEqual((self.web / 'generator-studio.html').read_text(), 'new generator-studio.html')
        self.assertIn('glob2-platform:development glob2-platform:previous', '\n'.join(self.calls()))

    def prebuilt(self, **changes):
        """A manifest of CI-built images for the checkout, and the host settings to take it."""
        registry = 'europe-docker.pkg.dev/example/glob2-online'
        targets = ('platform', 'music-worker', 'ai-music-worker', 'skin-render-worker', 'ai-map-worker',
                   'ai-terrain-worker', 'ai-building-worker', 'engine-agent', 'relay', 'caddy', 'web-client')
        value = {'version': 1, 'commit': subprocess.run(['git', '-C', str(self.repo), 'rev-parse', 'HEAD'],
                                                        capture_output=True, text=True).stdout.strip(),
                 'simVersion': '127-50-' + 'ab' * 32, 'designSystem': 'cd' * 20, 'registry': registry,
                 'buildArgs': {'GLOB2_WEBSITE_URL': '', 'GLOB2_DOWNLOAD_URL': ''},
                 'images': {t: 'sha256:' + format(i, 'x') * 64 for i, t in enumerate(targets, 1)}}
        value.update(changes)
        path = self.dir / 'images.json'
        path.write_text(json.dumps(value))
        with self.env_file.open('a') as env:
            env.write(f'GLOB2_IMAGE_REGISTRY={registry}\nGLOB2_IMAGE_REGISTRY_AUTH=docker\n')
        tree = self.dir / 'ci-web'
        (tree / 'threaded').mkdir(parents=True, exist_ok=True)
        (tree / 'assets').mkdir(exist_ok=True)
        (tree / 'assets' / 'pack-ci.data').write_text('ci')
        for name in ENTRY_FILES:
            (tree / name).write_text(f'ci {name}')
        self.env.pop('GLOB2_DESIGN_SYSTEM_SHA')
        return {'GLOB2_PREBUILT_IMAGES': str(path), 'FAKE_WEB_TREE': str(tree)}, value

    def test_prebuilt_images_are_pulled_instead_of_built(self):
        fake, value = self.prebuilt()
        result = self.run_script(**fake)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        # Nothing compiled: no web client container and no compose build.
        self.assertFalse(any('run --rm' in c for c in calls), calls)
        self.assertFalse(any(c.endswith(' build') or ' build ' in c for c in calls if 'compose' in c), calls)
        platform = f'{value["registry"]}/platform@{value["images"]["platform"]}'
        self.assertIn(f'pull --quiet {platform}', calls)
        # :previous first, then the pulled image under the name compose uses.
        self.assertLess(self.index('image tag glob2-platform:development glob2-platform:previous'),
                        self.index(f'image tag {platform} glob2-platform:development'))
        self.assertLess(self.index(f'image tag {platform}'), self.index('up -d --wait'))
        self.assertIn('--no-build', self.calls()[self.index('up -d --wait')])
        # The CI-built web client is installed last, as a built one would be.
        self.assertEqual((self.web / 'index.html').read_text(), 'ci index.html')
        self.assertIn('images: prebuilt from europe-docker.pkg.dev/example/glob2-online', result.stdout)
        [backup] = self.backups()
        self.assertEqual((backup / 'target-design-system-revision').read_text().strip(), 'cd' * 20)

    def test_prebuilt_images_for_another_commit_change_nothing(self):
        fake, _ = self.prebuilt(commit='9' * 40)
        result = self.run_script(**fake)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not the checkout', result.stderr)
        self.assertIn('the build failed; the running stack is unchanged', result.stderr)
        calls = self.calls()
        self.assertFalse(any('pull' in c or 'up -d' in c for c in calls), calls)
        self.assertEqual((self.web / 'index.html').read_text(), 'old index.html')

    def test_prebuilt_images_need_this_hosts_registry_and_settings(self):
        fake, _ = self.prebuilt(registry='evil.example.org/glob2/glob2-online')
        result = self.run_script(**fake)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not this host's", result.stderr)
        self.assertFalse(any('pull' in c for c in self.calls()))

    def test_prebuilt_images_must_be_built_with_the_hosts_website_links(self):
        fake, _ = self.prebuilt()
        with self.env_file.open('a') as env:
            env.write('GLOB2_WEBSITE_URL=https://glob2.example/\n')
        result = self.run_script(**fake)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('GLOB2_WEBSITE_URL', result.stderr)
        self.assertFalse(any('pull' in c for c in self.calls()))

    def test_a_pinned_design_revision_must_match_prebuilt_images(self):
        fake, _ = self.prebuilt()
        with self.env_file.open('a') as env:
            env.write('GLOB2_DESIGN_SYSTEM_SHA=' + 'ef' * 20 + '\n')
        result = self.run_script(**fake)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('the host pins', result.stderr)

    def test_an_unhealthy_prebuilt_stack_is_rolled_back(self):
        fake, _ = self.prebuilt()
        result = self.run_script(FAKE_UP_FAIL='1', **fake)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('rolled back', result.stderr)
        self.assertIn('image tag glob2-platform:previous glob2-platform:development', self.calls())
        self.assertEqual((self.web / 'index.html').read_text(), 'old index.html')

    def test_rejects_invalid_design_revision_before_building(self):
        result = self.run_script(GLOB2_DESIGN_SYSTEM_SHA='main')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(' build' in call for call in self.calls()))

    def test_keeps_only_the_newest_backups(self):
        # Force three updates into the same timestamp, even on a slow runner.
        date = self.dir / "bin" / "date"
        date.write_text("#!/bin/sh\nprintf '20260101T000000Z\\n'\n")
        date.chmod(date.stat().st_mode | stat.S_IEXEC)
        for _ in range(3):
            self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual(len(self.backups()), 2)

    def test_retention_preserves_manual_and_scheduled_backups(self):
        backups = self.dir / 'backups'
        backups.mkdir()
        names = ('admin-dashboard-20260101T000000Z', 'ai-enable-20260101T000000Z',
                 'manual-v4-20260101T000000Z', 'retired-environment-config', 'scheduled',
                 '20000101T000000Z')
        for name in names:
            (backups / name).mkdir()
            (backups / name / 'evidence').write_text('preserve')
        for _ in range(3):
            result = self.run_script()
            self.assertEqual(result.returncode, 0, result.stderr)
        for name in names:
            self.assertEqual((backups / name / 'evidence').read_text(), 'preserve')
        deployments = [path for path in self.backups() if path.name not in names]
        self.assertEqual(len(deployments), 2)
        for path in deployments:
            self.assertTrue((path / 'target-design-system-revision').exists())

    def test_retention_preserves_current_backup_after_clock_regression(self):
        backups = self.dir / 'backups'
        backups.mkdir()
        for name in ('20990101T000000Z', '20990102T000000Z.000000'):
            (backups / name).mkdir()
            (backups / name / 'revision').write_text('future-deployment')
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.backups()), 2)
        current = [path for path in self.backups() if (path / 'glob2.dump').exists()]
        self.assertEqual(len(current), 1)
        self.assertTrue((current[0] / 'glob2.dump').exists())
        self.assertTrue((current[0] / 'target-design-system-revision').exists())

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
