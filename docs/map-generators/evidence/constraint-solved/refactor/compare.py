#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare frozen binaries and retain native artifacts for each request."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import time


NEW_TELEMETRY = ('.attempted', '.skipped', '.unjoined-components')
COMPARED_FIELDS = ('exit', 'status', 'diagnostic', 'map_sha256', 'terrain_sha256',
                   'telemetry', 'statistics', 'quality')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def compare_request(index, request, binaries, root):
    row = {'request': request, 'runs': {}}
    for label, binary in binaries.items():
        out = root / f'{index:02d}-{label}'
        params = dict(width=request['width'].bit_length() - 1,
                      height=request['height'].bit_length() - 1,
                      teams=request['teams'], **request['settings'])
        command = [binary, '--generate-map', '--generator', str(request['generator']),
                   '--map-seed', str(request['seed']), '--output-dir', str(out),
                   '--write-map', 'true', '--report', 'terrain']
        for key, value in params.items():
            command += ['--param', f'{key}={value}']
        started = time.monotonic()
        process = subprocess.run(command, capture_output=True, text=True, timeout=180)
        out.mkdir(exist_ok=True)
        (out / 'command.json').write_text(json.dumps(command))
        (out / 'run.log').write_text(process.stdout + process.stderr)
        native = json.loads((out / 'result.json').read_text())
        report = native.get('map_report', {})
        telemetry = report.get('generation', {}).get('telemetry', {}).get('records', [])
        # Compare all existing records; these bookkeeping records were added by this refactor.
        telemetry = [record for record in telemetry
                     if not record['key'].endswith(NEW_TELEMETRY)]
        row['runs'][label] = dict(
            exit=process.returncode, status=native['status'],
            seconds=round(time.monotonic() - started, 3),
            diagnostic=native.get('diagnostic', ''),
            map_sha256=digest(out / 'map-r0.map'), terrain_sha256=digest(out / 'terrain.txt'),
            telemetry=telemetry, statistics=native.get('statistics'), quality=native.get('quality'))
    before, after = row['runs']['before'], row['runs']['after']
    row['differences'] = [key for key in COMPARED_FIELDS if before[key] != after[key]]
    result = f"DIFF {row['differences']}" if row['differences'] else 'identical'
    print(index, request['generator'], request['width'], request['height'],
          request['seed'], before['status'], result, flush=True)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', required=True)
    parser.add_argument('--after', required=True)
    parser.add_argument('--requests', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True)  # Refuse to overwrite an earlier comparison.
    binaries = {'before': str(Path(args.before).resolve()),
                'after': str(Path(args.after).resolve())}
    requests = json.loads(args.requests.read_text())
    with ThreadPoolExecutor(2) as pool:
        rows = list(pool.map(lambda item: compare_request(*item, binaries, root),
                             enumerate(requests)))
    result = dict(binaries={key: dict(path=value, sha256=digest(Path(value)))
                            for key, value in binaries.items()}, rows=rows)
    (root / 'results.json.gz').write_bytes(
        gzip.compress(json.dumps(result, indent=2).encode(), mtime=0))
    different = sum(bool(row['differences']) for row in rows)
    print('requests', len(rows), 'different', different, flush=True)
    return bool(different)


if __name__ == '__main__':
    raise SystemExit(main())
