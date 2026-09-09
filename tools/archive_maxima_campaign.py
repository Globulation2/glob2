#!/usr/bin/env python3
"""Seal a retired campaign in place after its stage drains; never signal engines."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import time


def archive(root):
    retirement = json.loads((root / 'RETIRED.json').read_text())
    pid = retirement.get('draining_stage_pid')
    while pid:
        result = subprocess.run(['ps', '-p', str(pid), '-o', 'stat=,command='],
                                capture_output=True, text=True, check=False)
        if not result.stdout.strip() or result.stdout.lstrip().startswith('Z'):
            break
        if str(root) not in result.stdout:
            raise RuntimeError('Stage PID reused: inspect before sealing')
        time.sleep(10)
    # Remote roots remain preserved separately. This seals transferred evidence.
    target = root / 'ARCHIVE_MANIFEST.jsonl.gz'
    pending = target.with_suffix('.pending')
    excluded = {target.name, pending.name, 'ARCHIVE.json', 'archive.log', 'STATUS.json'}
    count = size = 0
    with gzip.open(pending, 'wt') as output:
        for path in sorted(root.rglob('*')):
            if not path.is_file() or path.name in excluded:
                continue
            digest = hashlib.sha256()
            before = path.stat()
            with path.open('rb') as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
            after = path.stat()
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise RuntimeError(f'Artifact still changing: {path}')
            output.write(json.dumps({'path': str(path.relative_to(root)),
                                     'bytes': after.st_size, 'sha256': digest.hexdigest()}) + '\n')
            count += 1
            size += after.st_size
    pending.replace(target)
    record = {'sealed_utc': datetime.now(timezone.utc).isoformat(),
              'mode': 'in-place evidence archive', 'exploratory_only': True,
              'files': count, 'bytes': size, 'manifest': target.name,
              'manifest_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
              'remote_evidence': 'preserved at original remote roots; not deleted'}
    (root / 'ARCHIVE.json').write_text(json.dumps(record, indent=2) + '\n')
    state = json.loads((root / 'STATUS.json').read_text())
    state.update(status='retired_archived', child_pid=None, archive=record)
    (root / 'STATUS.json').write_text(json.dumps(state, indent=2) + '\n')
    print(json.dumps(record), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign', type=Path)
    archive(parser.parse_args().campaign.resolve())
