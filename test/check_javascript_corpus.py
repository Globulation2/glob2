#!/usr/bin/env python3
"""Run the shared scripting cases and retain results, even when a case fails."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import struct
import sys
from build_provenance import source_identity, build_issues

ROOT = Path(__file__).resolve().parents[1]


def source_manifest():
    files = {}
    for directory in ('src/script', 'third_party/quickjs-ng', 'third_party/openlibm',
                      'test/fixtures/javascript'):
        for path in sorted((ROOT / directory).rglob('*')):
            if path.is_file():
                files[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return dict(source_identity(), fixtureAndRuntimeHashes=files)


def binary_architecture(binary):
    data = binary.read_bytes()
    if data[:4] == b'\x7fELF':
        byte_order = '<' if data[5] == 1 else '>'
        machine = struct.unpack_from(byte_order + 'H', data, 18)[0]
        return {40: 'ARM', 62: 'x86-64', 183: 'ARM64'}.get(machine, f'ELF machine {machine}')
    if data[:4] == b'\xcf\xfa\xed\xfe':
        machine = struct.unpack_from('<I', data, 4)[0]
        return {0x1000007: 'x86-64', 0x100000c: 'ARM64'}.get(machine, f'Mach-O CPU {machine}')
    if data[:2] == b'MZ':
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        machine = struct.unpack_from('<H', data, offset + 4)[0]
        return {0x8664: 'x86-64', 0xaa64: 'ARM64'}.get(machine, f'PE machine {machine}')
    raise ValueError('Unrecognized native executable format: ' + str(binary))


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
        changes = '\n'.join(manifest['sourceStatus'])
        raise RuntimeError('Source checkout is dirty:\n' + changes +
                           '\nCommit the tested revision first, or use --allow-dirty for development runs')
    env = dict(os.environ, GLOB2_TEST_ARTIFACTS_ROOT=str(output / 'corpus'))
    env.pop('GLOB2_TEST_UPDATE_FIXTURES', None)
    for name in ('glob2-unit-tests', 'glob2-engine-tests'):
        binary = build / 'test' / (name + ('.exe' if os.name == 'nt' else ''))
        command = [str(binary), '--test-suite=JavaScript*', '--reporters=junit',
                   '--out=' + str(output / (name + '.xml'))]
        manifest['commands'].append(command)
        with (output / (name + '.log')).open('w') as log:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
        proofs = [json.loads(line.partition('GLOB2_TEST_PROVENANCE ')[2])
                  for line in (output / (name + '.log')).read_text().splitlines()
                  if line.startswith('GLOB2_TEST_PROVENANCE ')]
        proof = proofs[0] if len(proofs) == 1 else {}
        issues = build_issues(proof, manifest)
        manifest['tests'].append({'build': proof, 'provenanceIssues': issues, 'binary': name, 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                                  'binaryArchitecture': binary_architecture(binary), 'exitCode': code})
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'{name}: {"PASS" if code == 0 and not issues else "FAIL"}', flush=True)
    raise SystemExit(int(any(case['exitCode'] or case['provenanceIssues'] for case in manifest['tests'])))


if __name__ == '__main__':
    main()
