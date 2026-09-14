"""Immutable supplied builds. No source builds or dependency installation."""
import os
import hashlib
import importlib.resources
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
from .common import lock, atomic_json, canonical, digest, file_hash, hash_id, inside, read_json


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
                    target_platform=None, capabilities=None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    exe = inside(source, executable)
    if not exe.is_file():
        raise ValueError('executable missing from supplied bundle')
    if destination == source or destination.is_relative_to(source):
        raise ValueError('bundle destination must be outside supplied source')
    target_platform = target_platform or platform_identity()
    if capabilities is None:
        if target_platform != platform_identity():
            raise ValueError('cross-platform registration requires supplied capabilities JSON')
        import json
        capabilities = json.loads(subprocess.check_output([str(exe), '--headless-catalog'], cwd=source, timeout=60))
    manifest = {'schema_version': 1, 'source_revision': revision, 'dirty_identity': dirty_identity,
                'build_options': options or {}, 'platform': target_platform, 'executable': executable,
                'capabilities': capabilities, 'files': []}
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
