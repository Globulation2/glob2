"""deploy/prebuilt_images.py: the host accepts only images built for its checkout,
registry and build settings, pulls them by digest, tags them for compose and copies
the web client out without running anything.

Runs against a fake `docker` that logs every call (nothing is pulled).

    python3 -m unittest test/deployment/test_prebuilt_images.py -v
"""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'deploy/prebuilt_images.py'
spec = importlib.util.spec_from_file_location('prebuilt_images', SCRIPT)
prebuilt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prebuilt)

COMMIT = '1' * 40
SIM = '219-75-' + 'ab' * 32
REGISTRY = 'northamerica-northeast2-docker.pkg.dev/example-project/glob2-online'
DESIGN = '2' * 40
ARGS = {'GLOB2_WEBSITE_URL': 'https://example.org/', 'GLOB2_DOWNLOAD_URL': 'https://example.org/downloads/'}


def manifest(**changes):
    images = {target: 'sha256:' + format(i, 'x') * 64 for i, target in enumerate(sorted(prebuilt.TARGETS), 1)}
    value = {'version': 1, 'commit': COMMIT, 'simVersion': SIM, 'designSystem': DESIGN,
             'registry': REGISTRY, 'buildArgs': dict(ARGS), 'images': images}
    value.update(changes)
    return value


# Logs each call; `create` names a container, `cp` copies FAKE_WEB_TREE out of it.
FAKE_DOCKER = r'''#!/bin/sh
echo "$*" >> "$FAKE_LOG"
while [ "$1" = --config ]; do
  [ -d "$2" ] && ls "$2" >> "$FAKE_LOG.config"
  shift 2
done
case "$1" in
  login) cat > /dev/null; [ "${FAKE_LOGIN_FAIL:-0}" = 1 ] && exit 1 ;;
  pull) [ "${FAKE_PULL_FAIL:-}" != "" ] && case "$*" in *"$FAKE_PULL_FAIL"*) exit 1 ;; esac ;;
  create) echo c0ffee ;;
  cp) cp -R "$FAKE_WEB_TREE/." "$3" ;;
esac
exit 0
'''

COMPOSE = {'services': {
    'platform-api': {'image': 'glob2-platform:admin-x', 'build': {'target': 'platform'}},
    'platform-worker': {'image': 'glob2-platform:admin-x', 'build': {'target': 'platform'}},
    'init': {'image': 'glob2-platform:admin-x', 'build': {'target': 'platform'}},
    'engine-agent': {'image': 'glob2-engine-agent:development', 'build': {'target': 'engine-agent'}},
    'relay': {'image': 'glob2-relay:development', 'build': {'target': 'relay'}},
    'postgres': {'image': 'postgres:16-alpine'},
}}


class ValidationTests(unittest.TestCase):
    def test_accepts_a_complete_manifest(self):
        self.assertEqual(prebuilt.validate(manifest())['commit'], COMMIT)

    def test_rejects_any_missing_extra_or_malformed_field(self):
        cases = {
            'extra field': manifest(extra=1),
            'version': manifest(version=2),
            'short commit': manifest(commit='abc'),
            'commit with newline': manifest(commit=COMMIT + '\n'),
            'sim version': manifest(simVersion='219-75-xyz'),
            'design system': manifest(designSystem='main'),
            'registry with scheme': manifest(registry='https://' + REGISTRY),
            'registry with spaces': manifest(registry='evil host/x'),
            'missing build arg': manifest(buildArgs={'GLOB2_WEBSITE_URL': ''}),
            'extra build arg': manifest(buildArgs={**ARGS, 'JOBS': '64'}),
            'build arg with newline': manifest(buildArgs={**ARGS, 'GLOB2_WEBSITE_URL': 'a\nb'}),
        }
        missing = manifest()
        del missing['images']['relay']
        cases['missing image'] = missing
        unknown = manifest()
        unknown['images']['evil'] = 'sha256:' + 'f' * 64
        cases['unknown image'] = unknown
        tag = manifest()
        tag['images']['relay'] = 'latest'
        cases['tag instead of digest'] = tag
        weak = manifest()
        weak['images']['relay'] = 'md5:' + 'a' * 32
        cases['other digest'] = weak
        del_field = manifest()
        del del_field['designSystem']
        cases['missing field'] = del_field
        for name, value in cases.items():
            with self.subTest(name), self.assertRaises(prebuilt.ManifestError):
                prebuilt.validate(value)
        for value in ([], 'x', None):
            with self.assertRaises(prebuilt.ManifestError):
                prebuilt.validate(value)

    def test_load_rejects_non_json_and_oversized_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, 'm.json')
            path.write_text('{nope')
            with self.assertRaises(prebuilt.ManifestError):
                prebuilt.load(path)
            path.write_text(' ' * (prebuilt.MAX_MANIFEST + 1))
            with self.assertRaises(prebuilt.ManifestError):
                prebuilt.load(path)
            path.write_text(json.dumps(manifest()))
            self.assertEqual(prebuilt.load(path)['registry'], REGISTRY)


class CheckTests(unittest.TestCase):
    def check(self, value=None, **kw):
        args = dict(commit=COMMIT, sim_version=SIM, registry=REGISTRY, design_system=None,
                    build_args=list(ARGS.items()))
        args.update(kw)
        return prebuilt.check(value or manifest(), **args)

    def test_returns_the_design_system_of_matching_images(self):
        self.assertEqual(self.check(), DESIGN)
        self.assertEqual(self.check(design_system=DESIGN), DESIGN)

    def test_refuses_images_built_for_something_else(self):
        for name, kw in {
            'another commit': {'commit': '3' * 40},
            'another sim version': {'sim_version': '219-76-' + 'ab' * 32},
            'another registry': {'registry': 'europe-docker.pkg.dev/other/glob2-online'},
            'no registry configured': {'registry': ''},
            'another pinned theme': {'design_system': '4' * 40},
            'another website': {'build_args': [('GLOB2_WEBSITE_URL', 'https://elsewhere/')]},
            'unset download page': {'build_args': [('GLOB2_DOWNLOAD_URL', '')]},
        }.items():
            with self.subTest(name), self.assertRaises(prebuilt.ManifestError):
                self.check(**kw)

    def test_command_line_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, 'm.json')
            path.write_text(json.dumps(manifest()))
            base = [sys.executable, str(SCRIPT), 'check', str(path), '--commit', COMMIT, '--sim-version', SIM,
                    '--registry', REGISTRY, '--build-arg', f'GLOB2_WEBSITE_URL={ARGS["GLOB2_WEBSITE_URL"]}',
                    '--build-arg', f'GLOB2_DOWNLOAD_URL={ARGS["GLOB2_DOWNLOAD_URL"]}']
            ok = subprocess.run(base, capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            self.assertEqual(ok.stdout.strip(), DESIGN)
            bad = subprocess.run(base[:-4] + ['--build-arg', 'JOBS=99'], capture_output=True, text=True)
            self.assertEqual(bad.returncode, 2)
            self.assertIn('NAME=VALUE', bad.stderr)
            wrong = subprocess.run([*base[:5], '2' * 40, *base[6:]], capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('not the checkout', wrong.stderr)


class LocalNameTests(unittest.TestCase):
    def test_maps_each_target_to_every_service_image(self):
        names = prebuilt.local_names(COMPOSE)
        self.assertEqual(names['platform'], {'glob2-platform:admin-x'})
        self.assertEqual(names['engine-agent'], {'glob2-engine-agent:development'})
        self.assertNotIn('postgres', str(names))

    def test_ignores_services_without_a_build_target_or_image(self):
        config = {'services': {'a': {'build': {'context': '.'}, 'image': 'a'}, 'b': {'build': {'target': 'relay'}},
                               'c': {'build': '.', 'image': 'c'}}}
        self.assertEqual(prebuilt.local_names(config), {})


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='prebuilt-images-'))
        bin_dir = self.dir / 'bin'
        bin_dir.mkdir()
        docker = bin_dir / 'docker'
        docker.write_text(FAKE_DOCKER)
        docker.chmod(docker.stat().st_mode | stat.S_IEXEC)
        self.log = self.dir / 'docker.log'
        self.tree = self.dir / 'tree'
        (self.tree / 'threaded').mkdir(parents=True)
        (self.tree / 'index.html').write_text('new index')
        (self.tree / 'threaded/index.wasm').write_text('wasm')
        self.web = self.dir / 'build/emscripten/client/release'
        self.web.mkdir(parents=True)
        (self.web / 'stale.js').write_text('old build')
        self.environ = mock.patch.dict(os.environ, {
            'PATH': f'{bin_dir}:{os.environ["PATH"]}', 'FAKE_LOG': str(self.log),
            'FAKE_WEB_TREE': str(self.tree)})
        self.environ.start()

    def tearDown(self):
        self.environ.stop()
        shutil.rmtree(self.dir, ignore_errors=True)

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def install(self, value=None, auth='docker', config=COMPOSE):
        with mock.patch('sys.stdout', io.StringIO()):
            prebuilt.install(value or manifest(), config, self.web, auth)

    def test_pulls_every_image_by_digest_then_tags_it_for_compose(self):
        value = manifest()
        self.install(value)
        calls = self.calls()
        pulls = [c for c in calls if c.startswith('pull')]
        self.assertEqual(len(pulls), len(prebuilt.TARGETS))
        for target, digest in value['images'].items():
            self.assertIn(f'pull --quiet {REGISTRY}/{target}@{digest}', calls)
        # Never by tag: every reference carries its digest.
        self.assertTrue(all('@sha256:' in c for c in pulls))
        platform = f'{REGISTRY}/platform@{value["images"]["platform"]}'
        self.assertIn(f'image tag {platform} glob2-platform:admin-x', calls)
        self.assertEqual(sum(c.endswith(' glob2-platform:admin-x') for c in calls), 1)  # once per name
        self.assertIn(f'image tag {REGISTRY}/relay@{value["images"]["relay"]} glob2-relay:development', calls)
        # Every pull and the web copy happen before any tag, so a failure changes no local name.
        first_tag = min(i for i, c in enumerate(calls) if c.startswith('image tag'))
        self.assertLess(max(i for i, c in enumerate(calls) if c.startswith('pull')), first_tag)
        self.assertLess(max(i for i, c in enumerate(calls) if c.startswith(('create', 'cp', 'rm'))), first_tag)

    def test_copies_the_web_client_out_without_running_it(self):
        value = manifest()
        self.install(value)
        calls = self.calls()
        self.assertIn(f'create {REGISTRY}/web-client@{value["images"]["web-client"]} none', calls)
        self.assertIn('rm c0ffee', calls)
        self.assertFalse(any(c.startswith(('run', 'start')) for c in calls))
        self.assertEqual((self.web / 'index.html').read_text(), 'new index')
        self.assertEqual((self.web / 'threaded/index.wasm').read_text(), 'wasm')
        self.assertFalse((self.web / 'stale.js').exists())  # a clean tree, not an overlay
        self.assertFalse(self.web.with_name('release.incoming').exists())

    def test_a_web_client_image_without_a_page_is_refused_and_cleaned_up(self):
        (self.tree / 'index.html').unlink()
        with self.assertRaises(prebuilt.ManifestError):
            self.install()
        self.assertIn('rm c0ffee', self.calls())
        self.assertEqual((self.web / 'stale.js').read_text(), 'old build')  # untouched
        self.assertFalse(self.web.with_name('release.incoming').exists())
        # No local name moved, so the running images stay the :previous of the next deploy.
        self.assertFalse(any(c.startswith('image tag') for c in self.calls()))

    def test_a_failed_pull_tags_nothing(self):
        os.environ['FAKE_PULL_FAIL'] = '/relay@'
        with self.assertRaises(subprocess.CalledProcessError):
            self.install()
        self.assertFalse(any(c.startswith('image tag') for c in self.calls()))

    def test_refuses_compose_targets_the_manifest_cannot_hold(self):
        config = copy.deepcopy(COMPOSE)
        config['services']['new'] = {'image': 'glob2-new:dev', 'build': {'target': 'new-worker'}}
        with self.assertRaises(prebuilt.ManifestError):
            self.install(config=config)
        self.assertEqual(self.calls(), [])

    def test_metadata_login_is_private_and_removed(self):
        with mock.patch.object(prebuilt, 'metadata_token', return_value='secret-token'):
            self.install(auth='gce-metadata')
        calls = self.calls()
        login = next(c for c in calls if ' login ' in c)
        self.assertIn('--username oauth2accesstoken --password-stdin https://northamerica-northeast2-docker.pkg.dev',
                      login)
        self.assertNotIn('secret-token', '\n'.join(calls))  # via stdin, never an argument
        config = login.split()[1]
        self.assertTrue(all(c.startswith(f'--config {config} ') for c in calls if c.startswith('--config') or
                            ' pull ' in f' {c} ' and '--config' in c))
        pulls = [c for c in calls if ' pull ' in f' {c} ']
        self.assertTrue(pulls and all(c.startswith(f'--config {config} pull') for c in pulls))
        # Tags and the web copy use no registry login.
        self.assertTrue(all(not c.startswith('--config') for c in calls if 'image tag' in c or 'create' in c))
        self.assertFalse(Path(config).exists())

    def test_the_login_directory_is_removed_when_a_pull_fails(self):
        os.environ['FAKE_PULL_FAIL'] = '/caddy@'
        with mock.patch.object(prebuilt, 'metadata_token', return_value='t'), \
                self.assertRaises(subprocess.CalledProcessError):
            self.install(auth='gce-metadata')
        config = next(c for c in self.calls() if ' login ' in c).split()[1]
        self.assertFalse(Path(config).exists())

    def test_unknown_authentication_is_refused(self):
        with self.assertRaises(prebuilt.ManifestError):
            self.install(auth='password')

    def test_command_line_reads_the_compose_configuration_from_stdin(self):
        path = self.dir / 'm.json'
        path.write_text(json.dumps(manifest()))
        result = subprocess.run([sys.executable, str(SCRIPT), 'install', str(path), '--compose-config', '-',
                                 '--web-dir', str(self.web), '--auth', 'docker'],
                                input=json.dumps(COMPOSE), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('glob2-relay:development <- relay@', result.stdout)
        bad = subprocess.run([sys.executable, str(SCRIPT), 'install', str(path), '--compose-config', '-',
                              '--web-dir', str(self.web), '--auth', 'docker'],
                             input='not json', capture_output=True, text=True)
        self.assertEqual(bad.returncode, 2)
        self.assertIn('not JSON', bad.stderr)


if __name__ == '__main__':
    unittest.main()
