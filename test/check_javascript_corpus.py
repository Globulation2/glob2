#!/usr/bin/env python3
"""Run the shared scripting cases and retain results, even when a case fails."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def source_manifest():
    def git(*arguments):
        return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()
    files = {}
    for directory in ('src/script', 'third_party/quickjs-ng', 'third_party/openlibm',
                      'test/fixtures/javascript'):
        for path in sorted((ROOT / directory).rglob('*')):
            if path.is_file():
                files[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return {'revision': git('rev-parse', 'HEAD'), 'dirty': bool(git('status', '--porcelain')),
            'fixtureAndRuntimeHashes': files}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--allow-dirty', action='store_true', help='Development evidence only; never satisfies the final revision gate')
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = source_manifest()
    manifest.update(platform=platform.platform(), machine=platform.machine(), python=sys.version,
                    commands=[], tests=[])
    for name in ('identity.json', 'options.py', 'toolchain.json', 'options.json'):
        if (build / name).exists():
            manifest[name] = (build / name).read_text()
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if manifest['dirty'] and not args.allow_dirty:
        raise RuntimeError('Commit the tested revision first, or use --allow-dirty for development runs')
    env = dict(os.environ, GLOB2_TEST_ARTIFACTS_ROOT=str(output / 'corpus'))
    env.pop('GLOB2_TEST_UPDATE_FIXTURES', None)
    for name in ('glob2-unit-tests', 'glob2-engine-tests'):
        binary = build / 'test' / (name + ('.exe' if os.name == 'nt' else ''))
        command = [str(binary), '--test-suite=JavaScript*', '--reporters=junit',
                   '--out=' + str(output / (name + '.xml'))]
        manifest['commands'].append(command)
        with (output / (name + '.log')).open('w') as log:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
        manifest['tests'].append({'binary': name, 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'exitCode': code})
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'{name}: {"PASS" if code == 0 else "FAIL"}', flush=True)
    raise SystemExit(int(any(case['exitCode'] for case in manifest['tests'])))


if __name__ == '__main__':
    main()
