#!/usr/bin/env python3
"""Provision the pinned build-only SPIRV-Cross translator for SDL GPU Metal shaders.

SPIRV-Cross turns the SPIR-V that pinned glslang compiles into the Metal (and
Direct3D) sources that tools/shaders/generate.py records. It is built from the
hash-verified source archive in buildsys/spirv-cross.json, the same source the
pinned Ubuntu package builds, so a Mac translates exactly as the Linux pin does
(the generator's metalSources receipts are byte-identical). It installs once into
the shared development store (or build/mobile-tools/ in isolated mode). It is a
development dependency: nothing at runtime links, loads or ships it.
``locate`` never builds; ``ensure`` builds when missing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'buildsys/spirv-cross.json'
EXECUTABLE = 'spirv-cross.exe' if os.name == 'nt' else 'spirv-cross'


def _pin():
    return json.loads(LOCK.read_text())


def install_directory():
    sys.path.insert(0, str(ROOT / 'buildsys'))
    from dev_store import home, isolated, key
    pin = _pin()
    if isolated():
        return ROOT / 'build/mobile-tools' / ('spirv-cross-' + pin['version'])
    return home() / 'toolchains/spirv-cross' / key(ROOT, ['buildsys/spirv-cross.json'])


def _digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        while block := source.read(1 << 20):
            result.update(block)
    return result.hexdigest()


def _receipt(binary):
    """The install receipt beside a provisioned binary, or None."""
    receipt = Path(binary).resolve().parent.parent / 'receipt.json'
    try:
        return json.loads(receipt.read_text())
    except (OSError, ValueError):
        return None


def verified_build(binary):
    """True when binary is a SPIRV-Cross this tool built from the pinned source."""
    recorded = _receipt(binary)
    return bool(recorded and recorded.get('source_sha256') == _pin()['source']['sha256']
                and recorded.get('binary_sha256') == _digest(binary))


def locate():
    """The installed pinned translator, or None. Verifies the install receipt."""
    binary = install_directory() / 'bin' / EXECUTABLE
    return binary if binary.is_file() and verified_build(binary) else None


def ensure(jobs=None, quiet=False):
    """Return the pinned translator, building it from verified source when absent."""
    existing = locate()
    if existing:
        return existing
    sys.path.insert(0, str(ROOT / 'buildsys'))
    from dev_store import Lease, cache
    from tool_archives import download
    pin = _pin()
    directory = install_directory()
    directory.parent.mkdir(parents=True, exist_ok=True)
    with Lease(directory, exclusive=True, track_use=False):
        existing = locate()
        if existing:
            return existing
        if not shutil.which('cmake'):
            raise ValueError('Building pinned SPIRV-Cross needs CMake on PATH')
        archive = download(pin['source'], cache(ROOT, 'downloads', lease=False))
        log = sys.stderr if not quiet else subprocess.DEVNULL
        print('Building pinned SPIRV-Cross', pin['version'], 'into', directory, file=sys.stderr, flush=True)
        with tempfile.TemporaryDirectory(dir=directory.parent, prefix='.spirv-cross-build-') as staging:
            staging = Path(staging)
            with tarfile.open(archive) as source:
                source.extractall(staging / 'src', filter='data')
            source_dir = staging / 'src' / pin['source']['directory']
            build_dir = staging / 'build'
            generator = ['-G', 'Ninja'] if shutil.which('ninja') else []
            subprocess.run(['cmake', '-S', str(source_dir), '-B', str(build_dir), *generator, *pin['cmake_options']],
                           check=True, stdout=log, stderr=log)
            subprocess.run(['cmake', '--build', str(build_dir), '--target', 'spirv-cross',
                            '--parallel', str(jobs or os.cpu_count() or 2)], check=True, stdout=log, stderr=log)
            built = build_dir / EXECUTABLE
            if not built.is_file():
                raise ValueError('SPIRV-Cross build did not produce ' + str(built))
            publish = staging / 'publish'
            (publish / 'bin').mkdir(parents=True)
            shutil.copy2(built, publish / 'bin' / EXECUTABLE)
            shutil.copy2(source_dir / 'LICENSE', publish / 'LICENSE')
            (publish / 'receipt.json').write_text(json.dumps({
                'version': pin['version'], 'source_sha256': pin['source']['sha256'],
                'binary_sha256': _digest(publish / 'bin' / EXECUTABLE),
                'host': platform.system() + '-' + platform.machine()}, indent=2) + '\n')
            if directory.exists():
                shutil.rmtree(directory)
            publish.rename(directory)
    binary = locate()
    if not binary:
        raise ValueError('Pinned SPIRV-Cross installation failed verification: ' + str(directory))
    return binary


def resolve_for_build(environment=None):
    """GLOB2_SPIRV_CROSS when set (empty disables), else an already provisioned translator."""
    environment = os.environ if environment is None else environment
    if 'GLOB2_SPIRV_CROSS' in environment:
        return environment['GLOB2_SPIRV_CROSS'] or None
    binary = locate()
    return str(binary) if binary else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', nargs='?', default='ensure', choices=('ensure', 'path', 'check'),
                        help='ensure: build if missing and print the path; path: print the path if installed; '
                             'check: exit 1 unless installed')
    parser.add_argument('--jobs', type=int)
    args = parser.parse_args()
    if args.command == 'ensure':
        print(ensure(args.jobs))
        return 0
    binary = locate()
    if not binary:
        print('Pinned SPIRV-Cross is not provisioned; run python3 buildsys/spirv_cross_tool.py', file=sys.stderr)
        return 1
    print(binary)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
