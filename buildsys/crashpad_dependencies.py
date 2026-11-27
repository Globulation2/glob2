#!/usr/bin/env python3
"""Build Crashpad for the desktop Linux client (docs/multiplayer/crash-reports.md).

Crashpad comes from the vcpkg port at the pinned baseline the mobile builds use
(platforms/mobile/vcpkg.json), through this repository's overlay
(buildsys/vcpkg-ports/crashpad), which drops libcurl so the game links no second
OpenSSL. The prefix holds include/crashpad, the client archives and the handler
executable (bin/crashpad_handler); crashpad-manifest.json records what built it,
and a prefix whose manifest matches is reused.

    python3 buildsys/crashpad_dependencies.py --prefix build/crashpad-ci/prefix --work build/crashpad-ci/work

Then build with GLOB2_CRASHPAD_PREFIX pointing at the prefix. Only Linux builds
that send reports use it; Windows writes minidumps with dbghelp and macOS uses
MetricKit.
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

ROOT = Path(__file__).resolve().parents[1]
ARCHIVES = ('libvcpkg_crashpad_client.a', 'libvcpkg_crashpad_client_common.a', 'libvcpkg_crashpad_util.a',
            'libvcpkg_crashpad_base.a')


def triplet():
    machine = platform.machine().lower()
    arch = {'x86_64': 'x64', 'amd64': 'x64', 'aarch64': 'arm64', 'arm64': 'arm64'}.get(machine)
    if platform.system() != 'Linux' or not arch:
        raise SystemExit('Crashpad prefixes are built for desktop Linux on x86_64 or arm64 only')
    return 'glob2-' + arch + '-linux'


def revision():
    return json.loads((ROOT / 'platforms/mobile/vcpkg.json').read_text())['builtin-baseline']


def recipe():
    """What the prefix depends on: the baseline, the overlay port, the triplet and this script."""
    digest = hashlib.sha256()
    files = [Path(__file__), ROOT / 'buildsys/triplets' / (triplet() + '.cmake')]
    files += sorted((ROOT / 'buildsys/vcpkg-ports/crashpad').glob('*'))
    for path in files:
        digest.update(path.relative_to(ROOT).as_posix().encode() + b'\0' + path.read_bytes())
    clang = clang_directory()
    compiler = subprocess.run([str(clang / 'clang++') if clang else 'clang++', '--version'],
                              capture_output=True, text=True).stdout.splitlines()[:1]
    return {'baseline': revision(), 'triplet': triplet(), 'recipe': digest.hexdigest(), 'compiler': compiler}


def clang_directory():
    """mini_chromium compiles Linux with clang and clang++ from PATH: the plain
    names, or the newest versioned LLVM installation (Debian and Ubuntu)."""
    if shutil.which('clang++') and shutil.which('clang'):
        return None
    candidates = sorted(Path('/usr/lib').glob('llvm-*/bin/clang++'),
                        key=lambda path: int(path.parent.parent.name.split('-')[1]) if path.parent.parent.name.split('-')[1].isdigit() else 0)
    if not candidates:
        raise SystemExit('Crashpad needs clang and clang++ (apt install clang)')
    return candidates[-1].parent


def vcpkg(work):
    checkout = work / 'vcpkg'
    if not (checkout / '.git').is_dir():
        shutil.rmtree(checkout, ignore_errors=True)
        subprocess.run(['git', 'clone', '--filter=blob:none', '--no-checkout',
                        'https://github.com/microsoft/vcpkg.git', str(checkout)], check=True)
    subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', '--quiet', revision()], check=True)
    if not (checkout / 'vcpkg').is_file():
        subprocess.run([str(checkout / 'bootstrap-vcpkg.sh'), '-disableMetrics'], check=True)
    return checkout


def build(prefix, work, jobs=2):
    prefix, work = Path(prefix).resolve(), Path(work).resolve()
    identity = recipe()
    manifest = prefix / 'crashpad-manifest.json'
    if manifest.is_file() and json.loads(manifest.read_text()).get('identity') == identity:
        verify(prefix)
        return prefix
    work.mkdir(parents=True, exist_ok=True)
    tool = vcpkg(work)
    project = work / 'manifest'
    project.mkdir(exist_ok=True)
    (project / 'vcpkg.json').write_text(json.dumps({
        'name': 'glob2-crashpad', 'version': '1', 'builtin-baseline': revision(),
        'dependencies': ['crashpad']}, indent=2) + '\n')
    environment = dict(os.environ, VCPKG_ROOT=str(tool), VCPKG_DISABLE_METRICS='1',
                       VCPKG_MAX_CONCURRENCY=str(jobs), VCPKG_DOWNLOADS=str(work / 'downloads'))
    if clang := clang_directory():
        environment['PATH'] = str(clang) + os.pathsep + environment.get('PATH', '')
    installed = work / 'installed'
    subprocess.run([str(tool / 'vcpkg'), 'install', '--triplet=' + triplet(),
                    '--overlay-triplets=' + str(ROOT / 'buildsys/triplets'),
                    '--overlay-ports=' + str(ROOT / 'buildsys/vcpkg-ports'),
                    '--x-manifest-root=' + str(project), '--x-install-root=' + str(installed),
                    '--x-buildtrees-root=' + str(work / 'buildtrees'),
                    '--x-packages-root=' + str(work / 'packages')],
                   env=environment, check=True)
    source = installed / triplet()
    staging = prefix.with_name(prefix.name + '.staging')
    shutil.rmtree(staging, ignore_errors=True)
    (staging / 'lib').mkdir(parents=True)
    (staging / 'bin').mkdir()
    shutil.copytree(source / 'include/crashpad', staging / 'include/crashpad')
    for name in ARCHIVES:
        shutil.copy2(source / 'lib' / name, staging / 'lib' / name)
    shutil.copy2(source / 'tools/crashpad/crashpad_handler', staging / 'bin/crashpad_handler')
    subprocess.run(['strip', '--strip-unneeded', str(staging / 'bin/crashpad_handler')], check=True)
    (staging / 'bin/crashpad_handler').chmod(0o755)
    shutil.copy2(source / 'share/crashpad/copyright', staging / 'LICENSE.crashpad')
    files = {str(p.relative_to(staging)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(staging.rglob('*')) if p.is_file()}
    (staging / 'crashpad-manifest.json').write_text(json.dumps({'identity': identity, 'files': files}, indent=2) + '\n')
    shutil.rmtree(prefix, ignore_errors=True)
    staging.rename(prefix)
    verify(prefix)
    return prefix


def verify(prefix):
    """Every recorded file is present and unchanged."""
    manifest = json.loads((Path(prefix) / 'crashpad-manifest.json').read_text())
    for name, digest in manifest['files'].items():
        path = Path(prefix) / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit('Crashpad prefix changed since it was built: ' + str(path))
    for name in ARCHIVES:
        if 'lib/' + name not in manifest['files']:
            raise SystemExit('Crashpad prefix lacks lib/' + name)
    if 'bin/crashpad_handler' not in manifest['files'] or not os.access(Path(prefix) / 'bin/crashpad_handler', os.X_OK):
        raise SystemExit('Crashpad prefix lacks an executable bin/crashpad_handler')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    print('Crashpad prefix:', build(args.prefix, args.work, args.jobs))
    return 0


if __name__ == '__main__':
    sys.exit(main())
