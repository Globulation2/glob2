"""Immutable supplied builds. No source builds or dependency installation."""
import json
import os
import hashlib
import importlib.resources
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
from .common import lock, atomic_json, canonical, digest, file_hash, hash_id, inside, read_json
from .resources import classify_cli_capability


def validate_cli(executable, directory):
    """A bounded, asset-free probe; CLI identity is independent of the catalog."""
    try:
        description = json.loads(subprocess.check_output(
            [str(executable), 'help', '--format', 'json'], cwd=directory, timeout=10))
    except (subprocess.SubprocessError, OSError, ValueError) as error:
        raise ValueError('Unable to probe Glob2 CLI 2: ' + str(error)) from error
    if (type(description) is not dict or type(description.get('cli_version')) is not int
            or description['cli_version'] != 2 or type(description.get('schema_version')) is not int
            or description['schema_version'] != 1 or not isinstance(description.get('commands'), list)):
        raise ValueError('Unsupported Glob2 CLI: requires CLI 2 and description schema 1')
    classify_cli_capability(description)  # Malformed advertised marker must not become legacy support.
    return description


def platform_identity():
    machine = platform.machine().lower()
    return {'os': platform.system().lower(), 'arch': {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(machine, machine)}


def package_identity():
    root = importlib.resources.files(__package__)
    return digest({p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted(root.iterdir(), key=lambda p: p.name) if p.name.endswith('.py')})


def inspect_bundle(directory, verify=True):
    directory = Path(directory).resolve()
    manifest = read_json(directory / 'bundle.json')
    if manifest.get('cli_version') != 2:
        raise ValueError('bundle requires Glob2 CLI 2; register a new bundle')
    identity = manifest['id']
    hash_id(identity)
    if digest({k: v for k, v in manifest.items() if k != 'id'}) != identity:
        raise ValueError('bundle manifest identity mismatch')
    if verify:
        for entry in manifest['files']:
            path = inside(directory, entry['path'])
            if path.is_symlink() or not path.is_file() or path.stat().st_size != entry['bytes'] or file_hash(path) != entry['sha256']:
                raise ValueError('bundle file mismatch: ' + entry['path'])
        executable = inside(directory, manifest['executable'])
        if not os.access(executable, os.X_OK):
            raise ValueError('bundle executable is not executable')
    return manifest | {'directory': str(directory)}


def register_bundle(source, destination, executable, revision, options=None, dirty_identity=None,
                    target_platform=None, capabilities=None, cli_description=None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    exe = inside(source, executable)
    if not exe.is_file():
        raise ValueError('executable missing from supplied bundle')
    if destination == source or destination.is_relative_to(source):
        raise ValueError('bundle destination must be outside supplied source')
    target_platform = target_platform or platform_identity()
    if capabilities is None:
        cli_description = validate_cli(exe, source)
        if target_platform != platform_identity():
            raise ValueError('cross-platform registration requires supplied capabilities JSON')
        capabilities = json.loads(subprocess.check_output([str(exe), 'info', 'catalog', '--format', 'json'], cwd=source, timeout=60))
    manifest = {'schema_version': 1, 'cli_version': 2, 'source_revision': revision, 'dirty_identity': dirty_identity,
                'build_options': options or {}, 'platform': target_platform, 'executable': executable,
                'capabilities': capabilities, 'files': []}
    if cli_description is not None:
        support = classify_cli_capability(cli_description)
        if support == 'unsupported_cli':
            raise ValueError('Unsupported supplied CLI description')
        manifest['cli_description'] = cli_description
        manifest['execution_support'] = support
    # Supplied cross-platform catalogs alone do not advertise execution-profile flags.
    for path in sorted(source.rglob('*')):
        if path.is_symlink():
            raise ValueError('supplied bundles must contain regular files, not symlinks: ' + str(path))
        if path.is_file() and path.name != 'bundle.json':
            manifest['files'].append({'path': path.relative_to(source).as_posix(), 'sha256': file_hash(path),
                                      'bytes': path.stat().st_size, 'mode': path.stat().st_mode & 0o777})
    if not manifest['files'] or capabilities.get('schema_version') != 1:
        raise ValueError('bundle must support version 1 headless commands')
    manifest['id'] = digest(manifest)
    destination.mkdir(parents=True, exist_ok=True)
    final = destination / manifest['id']
    if final.exists():
        inspect_bundle(final)
        return manifest
    temporary = Path(tempfile.mkdtemp(prefix='.bundle-', dir=destination))
    try:
        for entry in manifest['files']:
            target = inside(temporary, entry['path'])
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(inside(source, entry['path']), target)
        atomic_json(temporary / 'bundle.json', manifest)
        inspect_bundle(temporary)
        with lock(destination / (manifest['id'] + '.lock'), blocking=True):
            if final.exists():
                inspect_bundle(final)
            else:
                os.rename(temporary, final)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return manifest


def eligible(bundle, host, kind):
    return (bundle['platform'] == host['platform']
            and kind in bundle['capabilities'].get('commands', [])
            and (not host.get('builds') or bundle['id'] in host['builds']))
