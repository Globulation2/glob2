#!/usr/bin/env python3
"""Compare shared scripting artifacts from two executed platform evidence bundles."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct


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
    if not any(key.endswith('numeric-profile1.value') for key in result):
        raise ValueError('Missing numeric results: ' + str(directory))
    if not any('JavaScriptSimulation/' in key for key in result):
        raise ValueError('Missing production simulation evidence: ' + str(directory))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    reference, candidate = inventory(args.reference), inventory(args.candidate)
    differences = {key: {'reference': reference.get(key), 'candidate': candidate.get(key)}
                   for key in sorted(reference.keys() | candidate.keys())
                   if reference.get(key) != candidate.get(key)}
    report = {'reference': str(args.reference), 'candidate': str(args.candidate),
              'comparedArtifacts': len(reference),
              'excludedSaveMetadata': 'MapHeader SHA1 only', 'differences': differences}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    if differences:
        raise SystemExit(f'FAIL: {len(differences)} missing or different artifacts; see {args.output}')
    print(f'PASS: {len(reference)} numeric/data/complete-trace/decoded-save artifacts match')


if __name__ == '__main__':
    main()
