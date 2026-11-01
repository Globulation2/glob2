#!/usr/bin/env python3
"""Verify a browser-static package, then upload its complete runtime closure."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess

spec = importlib.util.spec_from_file_location('package_static', Path(__file__).with_name('package-static.py'))
package_static = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_static)

IMMUTABLE = 'public,max-age=31536000,immutable'
REVALIDATE = 'no-cache,max-age=0,must-revalidate'
TYPES = {'.js': 'text/javascript', '.wasm': 'application/wasm',
         '.data': 'application/octet-stream', '.html': 'text/html',
         '.css': 'text/css', '.json': 'application/json', '.ttf': 'font/ttf',
         '.txt': 'text/plain'}


def upload_plan(directory, bucket):
    directory = Path(directory)
    package_static.verify(directory)
    version = json.loads((directory / 'package.json').read_text())['version']
    names = [line.split('  ', 1)[1] for line in (directory / 'SHA256SUMS').read_text().splitlines()]
    names = sorted(name for name in names if not name.endswith('.gz') and name != 'package.json')
    # Resolve every MIME type before any upload; never publish a partial new entry point.
    commands = []
    ordered = [name for name in names if not name.endswith('.html')]
    ordered += [name for name in names if name.endswith('.html') and name != 'index.html']
    for name in ordered + ['index.html']:
        suffix = Path(name).suffix
        mime = TYPES.get(suffix, 'text/plain') if name.startswith('licenses/') else TYPES[suffix]
        versioned = re.search(r'(?:^|-)' + re.escape(version) + r'(?:-|\.)', Path(name).name)
        addressed = re.fullmatch(r'assets/[a-z0-9-]+\.[0-9a-f]{16}\.data', name)
        cache = IMMUTABLE if versioned or addressed else REVALIDATE
        commands.append(['gcloud', 'storage', 'cp', str(directory / (name + '.gz')),
                         bucket.rstrip('/') + '/' + name, '--content-type=' + mime,
                         '--content-encoding=gzip', '--cache-control=' + cache])
    return commands


def publish(directory, bucket):
    if not bucket.startswith('gs://') or not bucket[5:].strip('/'):
        raise ValueError('Expected a gs:// bucket destination')
    for command in upload_plan(directory, bucket):
        subprocess.run(command, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('bucket')
    args = parser.parse_args()
    publish(args.directory, args.bucket)
