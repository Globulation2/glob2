#!/usr/bin/env python3
"""Prebuilt images for deploy/update-host.sh: the host pulls what CI built instead of
compiling on the host.

.github/workflows/deploy-online.yml builds every image of deploy/compose.yaml (and the
WebAssembly client, as a `web-client` image holding the built tree) from one public
commit, pushes them by digest and passes the host a manifest:

    {"version": 1, "commit": "<40 hex>", "simVersion": "<sim version>",
     "designSystem": "<40 hex>", "registry": "<host>/<project>/<repository>",
     "buildArgs": {"GLOB2_WEBSITE_URL": "...", "GLOB2_DOWNLOAD_URL": "..."},
     "images": {"<Dockerfile target>": "sha256:<64 hex>", ...}}

    prebuilt_images.py check   <manifest> --commit C --sim-version V --registry R
                               [--design-system D] [--build-arg NAME=VALUE ...]
    prebuilt_images.py install <manifest> --compose-config FILE|- --web-dir DIR
                               [--auth gce-metadata|docker]

`check` refuses a manifest that was not built for this checkout, this host's registry
and this host's build settings, and prints the design-system revision. `install`
pulls every image by digest (nothing is pulled by tag), tags it with the local name
compose.yaml gives each service built from that target (so `compose up` and the
:previous rollback tags work as for a local build), and copies the web client into
DIR. With --auth gce-metadata (the default), the pull authenticates as the VM's own
service account through the metadata server, with a short-lived token in a temporary
Docker configuration that is removed afterwards; --auth docker uses the caller's
Docker credentials. See docs/hosting/upgrades.md, "Prebuilt images".
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

# Every Dockerfile target compose.yaml builds, plus the web client tree.
TARGETS = frozenset({
    'platform', 'music-worker', 'ai-music-worker', 'skin-render-worker', 'ai-map-worker',
    'ai-terrain-worker', 'ai-building-worker', 'engine-agent', 'relay', 'caddy', 'web-client',
})
BUILD_ARGS = frozenset({'GLOB2_WEBSITE_URL', 'GLOB2_DOWNLOAD_URL'})
WEB_TARGET = 'web-client'
# The web-client image holds the built tree here (deploy-online.yml).
WEB_PATH = '/web-client'
COMMIT = re.compile(r'[0-9a-f]{40}')
DIGEST = re.compile(r'sha256:[0-9a-f]{64}')
# <region>-docker.pkg.dev/<project>/<repository> or another registry's host/path.
REGISTRY = re.compile(r'[a-z0-9][a-z0-9.-]*(?::[0-9]+)?(?:/[a-z0-9][a-z0-9._-]*){1,3}')
SIM_VERSION = re.compile(r'[0-9]+-[0-9]+-[0-9a-f]{64}')
METADATA_TOKEN = ('http://metadata.google.internal/computeMetadata/v1/instance/'
                  'service-accounts/default/token')
MAX_MANIFEST = 16384


class ManifestError(Exception):
    pass


def load(path):
    data = Path(path).read_bytes()
    if len(data) > MAX_MANIFEST:
        raise ManifestError('manifest is too large')
    try:
        manifest = json.loads(data)
    except ValueError as error:
        raise ManifestError(f'manifest is not JSON: {error}') from None
    return validate(manifest)


def validate(manifest):
    """The manifest, if every field has exactly the expected shape."""
    if not isinstance(manifest, dict):
        raise ManifestError('manifest is not an object')
    expected = {'version', 'commit', 'simVersion', 'designSystem', 'registry', 'buildArgs', 'images'}
    if set(manifest) != expected:
        raise ManifestError(f'manifest fields {sorted(manifest)} are not {sorted(expected)}')
    if manifest['version'] != 1:
        raise ManifestError(f'unsupported manifest version {manifest["version"]!r}')
    for key, pattern in (('commit', COMMIT), ('designSystem', COMMIT),
                         ('simVersion', SIM_VERSION), ('registry', REGISTRY)):
        value = manifest[key]
        if not isinstance(value, str) or not pattern.fullmatch(value):
            raise ManifestError(f'{key} {value!r} is malformed')
    args = manifest['buildArgs']
    if not isinstance(args, dict) or set(args) != BUILD_ARGS or \
            not all(isinstance(v, str) and len(v) < 512 and '\n' not in v for v in args.values()):
        raise ManifestError(f'buildArgs must be strings for exactly {sorted(BUILD_ARGS)}')
    images = manifest['images']
    if not isinstance(images, dict) or set(images) != TARGETS:
        missing = sorted(TARGETS - set(images)) if isinstance(images, dict) else []
        raise ManifestError(f'images must name exactly every target (missing {missing})')
    for target, digest in images.items():
        if not isinstance(digest, str) or not DIGEST.fullmatch(digest):
            raise ManifestError(f'{target} digest {digest!r} is malformed')
    return manifest


def reference(manifest, target):
    return f'{manifest["registry"]}/{target}@{manifest["images"][target]}'


def check(manifest, commit, sim_version, registry, design_system=None, build_args=()):
    """Refuses a manifest built for another commit, sim version, registry or settings."""
    if manifest['commit'] != commit:
        raise ManifestError(f'images were built for {manifest["commit"]}, not the checkout {commit}')
    if manifest['simVersion'] != sim_version:
        raise ManifestError(f'images have sim version {manifest["simVersion"]}, the checkout {sim_version}')
    if not registry:
        raise ManifestError('this host names no GLOB2_IMAGE_REGISTRY to pull from')
    if manifest['registry'] != registry:
        raise ManifestError(f'images come from {manifest["registry"]}, not this host\'s {registry}')
    if design_system and manifest['designSystem'] != design_system:
        raise ManifestError(f'images use design system {manifest["designSystem"]}, the host pins {design_system}')
    for name, value in build_args:
        if manifest['buildArgs'].get(name) != value:
            raise ManifestError(f'images were built with {name}={manifest["buildArgs"].get(name)!r}, '
                                f'the host sets {value!r}')
    return manifest['designSystem']


def local_names(compose_config):
    """Dockerfile target -> local image names of the services built from it."""
    names = {}
    for service in compose_config.get('services', {}).values():
        build = service.get('build')
        image = service.get('image')
        if isinstance(build, dict) and build.get('target') and image:
            names.setdefault(build['target'], set()).add(image)
    return names


def docker(*args, config=None, capture=False):
    command = ['docker']
    if config:
        command += ['--config', config]
    command += list(args)
    result = subprocess.run(command, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def metadata_token():
    request = urllib.request.Request(METADATA_TOKEN, headers={'Metadata-Flavor': 'Google'})
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.load(response)['access_token']


def install(manifest, compose_config, web_dir, auth='gce-metadata'):
    names = local_names(compose_config)
    unknown = sorted(set(names) - TARGETS)
    if unknown:
        raise ManifestError(f'compose builds targets the manifest cannot hold: {unknown}')
    config = None
    try:
        if auth == 'gce-metadata':
            # A private, short-lived login: removed with the directory below.
            config = tempfile.mkdtemp(prefix='glob2-pull-')
            os.chmod(config, 0o700)
            host = manifest['registry'].split('/', 1)[0]
            subprocess.run(['docker', '--config', config, 'login', '--username', 'oauth2accesstoken',
                            '--password-stdin', f'https://{host}'],
                           input=metadata_token(), text=True, check=True, stdout=subprocess.DEVNULL)
        elif auth != 'docker':
            raise ManifestError(f'unknown registry authentication {auth!r}')
        for target in sorted(TARGETS):
            docker('pull', '--quiet', reference(manifest, target), config=config)
    finally:
        if config:
            shutil.rmtree(config, ignore_errors=True)
    # The web client: the image's tree, copied out without running anything.
    web = Path(web_dir)
    staging = web.with_name(web.name + '.incoming')
    container = docker('create', reference(manifest, WEB_TARGET), 'none', capture=True)
    try:
        shutil.rmtree(staging, ignore_errors=True)
        staging.mkdir(parents=True)
        docker('cp', f'{container}:{WEB_PATH}/.', str(staging))
        if not (staging / 'index.html').is_file():
            raise ManifestError('the web-client image holds no index.html')
        shutil.rmtree(web, ignore_errors=True)
        staging.rename(web)
    finally:
        docker('rm', container, capture=True)
        shutil.rmtree(staging, ignore_errors=True)
    print(f'web client: {web} <- {WEB_TARGET}@{manifest["images"][WEB_TARGET][:19]}')
    # Local names move last: until every image and the web client are in hand,
    # the names still mean what runs, so a later :previous stays a real rollback.
    for target, images in sorted(names.items()):
        for image in sorted(images):
            docker('image', 'tag', reference(manifest, target), image)
            print(f'image: {image} <- {target}@{manifest["images"][target][:19]}')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('check')
    p.add_argument('manifest')
    p.add_argument('--commit', required=True)
    p.add_argument('--sim-version', required=True)
    p.add_argument('--registry', required=True)
    p.add_argument('--design-system', default='')
    p.add_argument('--build-arg', action='append', default=[])
    p = sub.add_parser('install')
    p.add_argument('manifest')
    p.add_argument('--compose-config', required=True)
    p.add_argument('--web-dir', required=True)
    p.add_argument('--auth', default='gce-metadata', choices=('gce-metadata', 'docker'))
    args = parser.parse_args(argv)
    try:
        manifest = load(args.manifest)
        if args.command == 'check':
            pairs = []
            for item in args.build_arg:
                name, sep, value = item.partition('=')
                if not sep or name not in BUILD_ARGS:
                    raise ManifestError(f'--build-arg {item!r} is not NAME=VALUE for {sorted(BUILD_ARGS)}')
                pairs.append((name, value))
            print(check(manifest, args.commit, args.sim_version, args.registry,
                        args.design_system or None, pairs))
        else:
            # `-`: stdin, so the resolved configuration (it holds the env file's
            # secrets) never touches the disk.
            text = sys.stdin.read() if args.compose_config == '-' else Path(args.compose_config).read_text()
            try:
                config = json.loads(text)
            except ValueError as error:
                raise ManifestError(f'compose configuration is not JSON: {error}') from None
            install(manifest, config, args.web_dir, args.auth)
    except ManifestError as error:
        print(f'prebuilt images: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
