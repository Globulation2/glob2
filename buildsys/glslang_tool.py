#!/usr/bin/env python3
"""Provision the pinned build-only glslang compiler for offline SDL GPU SPIR-V.

The compiler is built from the hash-verified source archive in
buildsys/glslang.json and installed once into the shared development store
(or build/mobile-tools/ in isolated mode). It is a development dependency:
nothing at runtime links, loads or ships it; only the SPIR-V it produces is
packaged. ``locate`` never builds; ``ensure`` builds when missing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'buildsys/glslang.json'
EXECUTABLE = 'glslang.exe' if os.name == 'nt' else 'glslang'


def _pin():
    return json.loads(LOCK.read_text())


def install_directory():
    sys.path.insert(0, str(ROOT / 'buildsys'))
    from dev_store import home, isolated, key
    pin = _pin()
    if isolated():
        return ROOT / 'build/mobile-tools' / ('glslang-' + pin['version'])
    return home() / 'toolchains/glslang' / key(ROOT, ['buildsys/glslang.json'])


def _digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        while block := source.read(1 << 20):
            result.update(block)
    return result.hexdigest()


def check_version(binary, expected=None):
    """Return the binary's --version text when it reports the pinned version."""
    expected = expected or _pin()['version']
    text = subprocess.check_output([str(binary), '--version'], text=True)
    if not re.search(r'^Glslang Version: \d+:' + re.escape(expected) + r'$', text, re.M):
        raise ValueError(str(binary) + ' is not pinned glslang ' + expected)
    return text


def locate():
    """The installed pinned compiler, or None. Verifies the install receipt."""
    directory = install_directory()
    binary = directory / 'bin' / EXECUTABLE
    receipt = directory / 'receipt.json'
    if not binary.is_file() or not receipt.is_file():
        return None
    try:
        recorded = json.loads(receipt.read_text())
    except ValueError:
        return None
    pin = _pin()
    if recorded.get('source_sha256') != pin['source']['sha256'] or recorded.get('binary_sha256') != _digest(binary):
        return None
    return binary


def ensure(jobs=None, quiet=False):
    """Return the pinned compiler, building it from verified source when absent."""
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
            raise ValueError('Building pinned glslang needs CMake on PATH')
        archive = download(pin['source'], cache(ROOT, 'downloads', lease=False))
        log = sys.stderr if not quiet else subprocess.DEVNULL
        print('Building pinned glslang', pin['version'], 'into', directory, file=sys.stderr, flush=True)
        with tempfile.TemporaryDirectory(dir=directory.parent, prefix='.glslang-build-') as staging:
            staging = Path(staging)
            with tarfile.open(archive) as source:
                source.extractall(staging / 'src', filter='data')
            source_dir = staging / 'src' / pin['source']['directory']
            build_dir = staging / 'build'
            generator = ['-G', 'Ninja'] if shutil.which('ninja') else []
            subprocess.run(['cmake', '-S', str(source_dir), '-B', str(build_dir), *generator,
                            '-DCMAKE_INSTALL_PREFIX=' + str(staging / 'install'), *pin['cmake_options']],
                           check=True, stdout=log, stderr=log)
            subprocess.run(['cmake', '--build', str(build_dir), '--target', 'glslang-standalone',
                            '--parallel', str(jobs or os.cpu_count() or 2)], check=True, stdout=log, stderr=log)
            built = build_dir / 'StandAlone' / EXECUTABLE
            if not built.is_file():
                raise ValueError('glslang build did not produce ' + str(built))
            version = check_version(built, pin['version'])
            publish = staging / 'publish'
            (publish / 'bin').mkdir(parents=True)
            shutil.copy2(built, publish / 'bin' / EXECUTABLE)
            # glslangValidator is the historical name the shader manifest uses.
            shutil.copy2(built, publish / 'bin' / ('glslangValidator' + ('.exe' if os.name == 'nt' else '')))
            shutil.copy2(source_dir / 'LICENSE.txt', publish / 'LICENSE.txt')
            (publish / 'receipt.json').write_text(json.dumps({
                'version': pin['version'], 'commit': pin['commit'],
                'source_sha256': pin['source']['sha256'],
                'binary_sha256': _digest(publish / 'bin' / EXECUTABLE),
                'host': platform.system() + '-' + platform.machine(),
                'reported': version.strip()}, indent=2) + '\n')
            if directory.exists():
                shutil.rmtree(directory)
            publish.rename(directory)
    binary = locate()
    if not binary:
        raise ValueError('Pinned glslang installation failed verification: ' + str(directory))
    return binary


def resolve_for_build(environment=None):
    """GLOB2_GLSLANG when set (empty disables), else an already provisioned compiler."""
    environment = os.environ if environment is None else environment
    if 'GLOB2_GLSLANG' in environment:
        return environment['GLOB2_GLSLANG'] or None
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
        print('Pinned glslang is not provisioned; run python3 buildsys/glslang_tool.py', file=sys.stderr)
        return 1
    print(binary)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
