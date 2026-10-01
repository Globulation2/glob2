#!/usr/bin/env python3
"""Compare shared scripting artifacts from two executed platform evidence bundles."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
from build_provenance import build_issues


def payload(data):
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    if len(data) < 4:
        raise ValueError('Truncated save')
    offset = 4 + struct.unpack_from('>I', data)[0] + 17
    if offset + 20 > len(data):
        raise ValueError('Truncated MapHeader')
    return data[:offset] + data[offset + 20:]


def inventory(directory):
    result = {}
    for path in sorted(directory.rglob('*')):
        if not path.is_file() or not any(path.name.endswith(suffix) for suffix in
                                        ('.value', '.checksums', '.game', '.game.gz')):
            continue
        parts = path.relative_to(directory).parts
        suite = next((i for i, part in enumerate(parts) if part.startswith('JavaScript')), None)
        if suite is None:  # Ignore user profiles and unrelated runner outputs.
            continue
        key = '/'.join(parts[suite:])
        data = path.read_bytes()
        if path.name.endswith(('.game', '.game.gz')):
            data = payload(data)
        if key in result:
            raise ValueError('Duplicate artifact key: ' + key)
        result[key] = hashlib.sha256(data).hexdigest()
    for required in ('numeric-profile1.value', 'global-numbers-profile1.value'):
        if not any(key.endswith(required) for key in result):
            raise ValueError('Missing numeric results ' + required + ': ' + str(directory))
    if not any('JavaScriptSimulation/' in key for key in result):
        raise ValueError('Missing production simulation evidence: ' + str(directory))
    if not any('conversion_boundary' in key and key.endswith('.checksums') for key in result):
        raise ValueError('Missing conversion continuation evidence: ' + str(directory))
    return result


def provenance(directory):
    for name in ('manifest.json', 'result.json'):
        path = directory / name
        if path.is_file():
            return json.loads(path.read_text())
    raise ValueError('Missing runner manifest: ' + str(directory))


def provenance_issues(reference, candidate):
    issues = []
    if not reference.get('revision') or reference.get('revision') != candidate.get('revision'):
        issues.append('Source revisions are missing or different')
    if (not reference.get('sourceTreeSha256') or
            reference.get('sourceTreeSha256') != candidate.get('sourceTreeSha256')):
        issues.append('Normalized source trees are missing or different')
    for label, manifest in (('reference', reference), ('candidate', candidate)):
        if manifest.get('dirty') is not False:
            issues.append(label + ' is dirty or does not record a clean source tree')
        tests = manifest.get('tests', [])
        result = manifest.get('result', {})
        if any(test.get('exitCode') != 0 for test in tests):
            issues.append(label + ' includes failed tests')
        if 'exit' in manifest and manifest['exit'] != 0:
            issues.append(label + ' browser execution failed')
        if 'exitCode' in result and result['exitCode'] != 0:
            issues.append(label + ' iOS execution failed')
        if not tests and 'exit' not in manifest and 'exitCode' not in result:
            issues.append(label + ' does not record successful execution')
        if manifest.get('retrievalErrors') or manifest.get('provenanceIssues'):
            issues.append(label + ' includes retrieval/provenance failures')
        producer_builds = [test.get('build') for test in tests] if tests else [manifest.get('build')]
        for build in producer_builds:
            issues.extend(label + ': ' + issue for issue in build_issues(build, manifest))
        build = result.get('build', {})
        if build and (build.get('revision') != manifest.get('revision') or build.get('dirty')):
            issues.append(label + ' app build metadata differs from the runner source')
    return issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--allow-development', action='store_true', help='Compare data from development runs without satisfying the clean revision gate')
    args = parser.parse_args()
    source_reference, source_candidate = provenance(args.reference), provenance(args.candidate)
    issues = provenance_issues(source_reference, source_candidate)
    reference, candidate = inventory(args.reference), inventory(args.candidate)
    differences = {key: {'reference': reference.get(key), 'candidate': candidate.get(key)}
                   for key in sorted(reference.keys() | candidate.keys())
                   if reference.get(key) != candidate.get(key)}
    report = {'reference': str(args.reference), 'candidate': str(args.candidate),
              'comparedArtifacts': len(reference),
              'referenceRevision': source_reference.get('revision'),
              'candidateRevision': source_candidate.get('revision'),
              'provenanceIssues': issues,
              'acceptanceEligible': not differences and not issues,
              'excludedSaveMetadata': 'MapHeader SHA1 only', 'differences': differences}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    if issues and not args.allow_development:
        raise SystemExit('FAIL: source/execution provenance gate; see ' + str(args.output))
    if differences:
        raise SystemExit(f'FAIL: {len(differences)} missing or different artifacts; see {args.output}')
    print(f'PASS: {len(reference)} numeric/data/complete-trace/decoded-save artifacts match')


if __name__ == '__main__':
    main()
