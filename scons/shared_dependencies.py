"""Leased, content-verified dependency installations shared across checkouts."""
import hashlib
import inspect
import json
import os
import platform
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time

import dev_store as store


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fingerprint(builder, arguments):
    module = inspect.getmodule(builder)
    recipe = Path(inspect.getfile(builder)).resolve()
    inputs = [recipe, Path(__file__), recipe.parent / 'sdl3_dependencies.py']
    inputs += sorted(recipe.parent.glob('*versions.json'))
    inputs += sorted(recipe.parent.glob('*vendored.json'))
    inputs += sorted((recipe.parent / 'vcpkg-ports').glob('**/*.patch'))
    environment = arguments.get('environment') or os.environ
    tools = {}
    commands = [arguments.get(name) for name in ('cc', 'cxx', 'ar', 'ranlib')]
    if arguments.get('emscripten'):
        commands += [str(Path(arguments['emscripten']) / name) for name in ('emcc', 'em++', 'emar')]
    commands += ['cmake', 'make', 'nasm', 'pkg-config']
    for command in filter(None, commands):
        words = shlex.split(str(command))
        binary = shutil.which(words[0], path=environment.get('PATH'))
        if binary:
            tools[str(command)] = {'binary': digest(binary), 'version': subprocess.run(
                [*words, '--version'], env=environment, capture_output=True, text=True).stdout}
    platform_state = {}
    if arguments.get('target') == 'linux':
        for package in ('libva', 'libva-drm'):
            probe = subprocess.run(['pkg-config', '--modversion', package], env=environment, capture_output=True, text=True)
            platform_state[package] = [probe.returncode, probe.stdout]
    if arguments.get('target') == 'darwin' and not arguments.get('sdk_identity'):
        platform_state['sdk'] = [subprocess.check_output(['xcrun', '--show-sdk-' + name], env=environment, text=True) for name in ('path', 'version')]
    options = {key: value for key, value in arguments.items() if key not in ('environment', 'jobs')}
    ambient = {name: environment.get(name, '') for name in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET', 'DEVELOPER_DIR', 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR')}
    return {'schema': 1, 'host': [platform.system(), platform.machine()], 'builder': module.__name__, 'recipes': {p.name: digest(p) for p in inputs},
            'tools': tools, 'options': options, 'environment': ambient, 'platform': platform_state}


def contents(prefix):
    return {str(path.relative_to(prefix)): digest(path) for path in sorted(prefix.rglob('*'))
            if path.is_file() and path.name != '.shared-install.json'}


def verify(prefix, identity):
    try:
        record = json.loads((prefix / '.shared-install.json').read_text())
        return record['identity'] == identity and bool(record['files']) and record['files'] == contents(prefix)
    except (OSError, ValueError, KeyError):
        return False


def relocate(prefix, destination):
    # Upstream installs embed their configure prefix in pkg-config/CMake metadata.
    # Archive contents and application/runtime paths must never be rewritten.
    for path in prefix.rglob('*'):
        if path.is_file() and path.suffix in ('.pc', '.cmake', '.la'):
            text = path.read_text()
            installed = destination.as_posix() if path.suffix == '.pc' else str(destination)
            if path.suffix == '.pc':
                # pkg-config emits these values as shell/compiler arguments.
                installed = ''.join('\\' + char if char.isspace() else char for char in installed)
            # macOS resolves /tmp to /private/tmp inside upstream builders.
            # Replace the longest alias first to avoid leaving /private behind.
            aliases = {str(prefix), prefix.as_posix(), str(prefix.resolve()), prefix.resolve().as_posix()}
            for alias in sorted(aliases, key=len, reverse=True):
                text = text.replace(alias, installed)
            path.write_text(text)


def ensure(builder, local_prefix, work, *, explicit=False, execute=True, **arguments):
    """Return a verified shared installation, or a local/explicit installation.

    Query-only invocations never provision libraries. They resolve the same key
    as a real build so compilation databases use its eventual include paths.
    """
    started = time.monotonic()
    if explicit or store.isolated():
        prefix = store.adopt(local_prefix)
        if execute:
            builder(prefix, work, **arguments)
        return prefix
    identity = json.loads(json.dumps(fingerprint(builder, arguments), sort_keys=True, default=str))
    name = 'build-' + hashlib.sha256(json.dumps(identity, sort_keys=True, default=str).encode()).hexdigest()[:24]
    entry = store.home() / 'dependencies' / name
    prefix = entry / 'prefix'
    if not execute:
        return prefix
    if entry.resolve() in store._HELD:
        if not verify(prefix, identity):
            raise ValueError('In-use dependency installation changed: ' + str(prefix))
        return prefix
    # Valid installations need only a reader lease: other active consumers must
    # not serialize compatible builds. Release it before repairing/publishing.
    while True:
        store.hold(entry)
        if verify(prefix, identity):
            break
        store._HELD.pop(entry.resolve()).close()
        with store.Lease(entry, exclusive=True):
            if not verify(prefix, identity):
                entry.parent.mkdir(parents=True, exist_ok=True)
                # Upstream Makefiles (notably x264) do not quote install prefixes.
                # Build outside the managed home, whose macOS default contains
                # "Application Support", then publish on the destination volume.
                build_root = '/tmp' if os.name != 'nt' else None
                with tempfile.TemporaryDirectory(prefix='glob2-dependency-', dir=build_root) as temporary:
                    staging = Path(temporary) / 'entry'
                    staged_prefix = staging / 'prefix'
                    builder(staged_prefix, Path(temporary) / 'sources', **arguments)
                    relocate(staged_prefix, prefix)
                    files = contents(staged_prefix)
                    if not files or not list(staged_prefix.glob('*manifest.json')):
                        raise ValueError('Dependency builder produced no validated installation')
                    (staged_prefix / '.shared-install.json').write_text(json.dumps({'identity': identity, 'files': files}, sort_keys=True, default=str))
                    with tempfile.TemporaryDirectory(prefix='.dependency-', dir=entry.parent) as publication:
                        published = Path(publication) / 'entry'
                        shutil.copytree(staging, published)
                        if not verify(published / 'prefix', identity):
                            raise ValueError('Copied dependency installation failed verification')
                        if entry.exists():
                            shutil.rmtree(entry)
                        published.replace(entry)
    log = os.environ.get('GLOB2_BUILD_TIMING_LOG')
    if log:
        with open(log, 'a') as output:
            output.write(json.dumps({'phase': 'dependency', 'builder': builder.__module__, 'seconds': time.monotonic() - started, 'prefix': str(prefix)}) + '\n')
    return prefix
