#!/usr/bin/env python3
"""Collect unapproved generator fingerprints without changing expected rows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def rows(text, platform_tag):
    result = {}
    for line in text.splitlines():
        if not line.startswith(platform_tag + ' '):
            continue
        fields = line.split()
        if len(fields) != 9 or fields[7] not in ('ok', 'invalid', 'failed'):
            raise ValueError(f'Malformed generator row: {line}')
        numbers = tuple(int(value) for value in fields[1:7])
        value = int(fields[8])
        if numbers in result:
            raise ValueError(f'Repeated generator row: {line}')
        result[numbers] = (fields[7], value)
    if not result:
        raise ValueError(f'No generator rows for {platform_tag}')
    return result


def check_inventory(observed, reference):
    if observed.keys() != reference.keys():
        raise ValueError(f'Generator inventory differs: missing={sorted(reference.keys() - observed.keys())}, extra={sorted(observed.keys() - reference.keys())}')


def inventory(text, platform_tag):
    result = {}
    for line in text.splitlines():
        if not line.startswith(platform_tag + ' '):
            continue
        fields = line.split()
        if len(fields) != 7:
            raise ValueError(f'Malformed generator inventory row: {line}')
        key = tuple(int(value) for value in fields[1:])
        if key in result:
            raise ValueError(f'Repeated generator inventory row: {line}')
        result[key] = None
    if not result:
        raise ValueError(f'No generator inventory for {platform_tag}')
    return result


def require_reference_inventory(requests, reference):
    missing = reference.keys() - requests.keys()
    if missing:
        raise ValueError(f'Generator inventory omits committed reference rows: {sorted(missing)}')


def compare(first, second):
    a = json.loads((first / 'manifest.json').read_text())
    b = json.loads((second / 'manifest.json').read_text())
    for key in ('platform', 'source', 'expected_tables', 'inventory_sha256'):
        if a[key] != b[key]:
            raise ValueError(f'Generator evidence inputs differ: {key}')
    observed = rows((first / 'rows.txt').read_text(), a['platform'])
    candidate = rows((second / 'rows.txt').read_text(), b['platform'])
    check_inventory(candidate, observed)
    if candidate != observed:
        raise ValueError('Generator fingerprints differ; investigate before accepting output')
    return len(observed)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect(binary, output, platform_tag):
    binary = binary.resolve()
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = json.loads(subprocess.check_output([sys.executable, str(ROOT / 'test/build_provenance.py')], cwd=ROOT))
    tables = [ROOT / 'test/map-generator-golden.txt',
              ROOT / 'test/fixtures/map-generators/pre-resource-epoch-golden.txt',
              ROOT / 'test/map-generator-resource-epoch.json']
    before = {str(path.relative_to(ROOT)): digest(path) for path in tables}
    reference = rows(tables[0].read_text(), 'linux-x86_64')
    binary_before = digest(binary)
    observed = None
    commands = []
    with tempfile.TemporaryDirectory(prefix='glob2-generator-inventory-') as profile:
        command = [str(binary), profile, '--inventory']
        commands.append(command)
        with (output / 'inventory.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, check=True, timeout=120, stdout=log,
                           stderr=subprocess.STDOUT, env=dict(os.environ,
                           SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
    requests = inventory((output / 'inventory.log').read_text(), platform_tag)
    require_reference_inventory(requests, reference)
    for index in range(2):
        with tempfile.TemporaryDirectory(prefix='glob2-generator-evidence-') as profile:
            command = [str(binary), profile, '--print']
            commands.append(command)
            with (output / f'print-{index}.log').open('w') as log:
                subprocess.run(command, cwd=ROOT, check=True, timeout=1200, stdout=log,
                               stderr=subprocess.STDOUT, env=dict(os.environ,
                               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
        current = rows((output / f'print-{index}.log').read_text(), platform_tag)
        check_inventory(current, requests)
        if observed is not None and current != observed:
            raise ValueError('Fresh-process generator fingerprints differ')
        observed = current
    after_source = json.loads(subprocess.check_output([sys.executable, str(ROOT / 'test/build_provenance.py')], cwd=ROOT))
    if source != after_source:
        raise ValueError('Source inputs changed during generator collection')
    after = {str(path.relative_to(ROOT)): digest(path) for path in tables}
    if digest(binary) != binary_before:
        raise ValueError('Generator binary changed during collection')
    if before != after:
        raise ValueError('Expected or historical generator inputs changed during collection')
    serialized = '\n'.join(f'{platform_tag} ' + ' '.join(map(str, key)) + f' {status} {value}'
                           for key, (status, value) in sorted(observed.items())) + '\n'
    (output / 'rows.txt').write_text(serialized)
    compiler = subprocess.check_output(['clang++', '--version'], text=True)
    (output / 'manifest.json').write_text(json.dumps(dict(
        source=source, platform=platform_tag, host=platform.platform(), host_compiler=compiler,
        source_binary_association='unverified by collector; inspect the producing build job',
        binary_sha256=binary_before, expected_tables=before, commands=commands,
        inventory_sha256=hashlib.sha256(json.dumps(sorted(requests)).encode()).hexdigest(),
        row_count=len(observed), acceptance='unapproved; same-host fresh-process repeatability only',
        github_run_id=os.environ.get('GITHUB_RUN_ID'), github_job=os.environ.get('GITHUB_JOB'),
    ), indent=2) + '\n')
    print(f'Collected {len(observed)} unapproved rows; expected tables unchanged')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    capture = sub.add_parser('collect')
    capture.add_argument('binary', type=Path)
    capture.add_argument('output', type=Path)
    capture.add_argument('--platform', required=True)
    comparison = sub.add_parser('compare')
    comparison.add_argument('first', type=Path)
    comparison.add_argument('second', type=Path)
    args = parser.parse_args()
    if args.mode == 'collect':
        collect(args.binary, args.output, args.platform)
    else:
        print(f'{compare(args.first, args.second)} collected rows agree; verify independent job provenance before acceptance')


if __name__ == '__main__':
    main()
