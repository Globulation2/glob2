#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render prepared scenes in a CPU-limited Blender 2.34 container, resuming whole chunks."""
import argparse
import concurrent.futures
import json
import hashlib
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True, help='Host work directory containing scenes/manifest.json')
    parser.add_argument('--container-work', required=True, help='Same work directory inside the container')
    parser.add_argument('--container', default='glob2-sprite-render')
    parser.add_argument('--blender', default='/work/blender-2.34-linux-glibc2.2.5-i386-static/blender')
    parser.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    args = parser.parse_args()
    if args.workers < 1:
        parser.error('--workers must be positive')
    root = args.work.resolve()
    manifest = json.loads((root / 'scenes/manifest.json').read_text())
    (root / 'logs').mkdir(exist_ok=True)
    # Use a dedicated render container: this caps all renderer processes together.
    subprocess.run(['docker', 'update', '--cpus=' + str(args.workers), args.container], check=True)
    jobs = [(s['name'], first, min(first + 31, s['last']))
            for s in manifest for first in range(s['first'], s['last'] + 1, 32)]
    settings = dict(workers=args.workers, container_cpu_limit=args.workers,
                    container=args.container, blender=args.blender, container_work=args.container_work)
    (root / 'job-settings.json').write_text(json.dumps(settings, indent=2) + '\n')
    scene_hashes = {s['name']: hashlib.sha256((root / 'scenes' / (s['name'] + '.blend')).read_bytes()).hexdigest()
                    for s in manifest}
    started = time.time()

    def run(job):
        name, first, last = job
        tag = '%s-%04d-%04d' % job
        marker = root / 'logs' / (tag + '.done')
        files = [root / 'rendered' / name / ('%04d.png' % n) for n in range(first, last + 1)]
        signature = json.dumps(dict(scene_sha256=scene_hashes[name], blender=args.blender,
                                    first=first, last=last), sort_keys=True) + '\n'
        if marker.exists() and marker.read_text() == signature and all(p.exists() for p in files):
            return tag
        command = ['docker', 'exec', '-e', 'LD_LIBRARY_PATH=/opt/legacy/usr/lib',
                   '-e', 'OMP_NUM_THREADS=1', args.container, 'qemu-i386', args.blender,
                   '-b', args.container_work.rstrip('/') + '/scenes/' + name + '.blend',
                   '-s', str(first), '-e', str(last), '-a']
        with (root / 'logs' / (tag + '.log')).open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        if not all(p.exists() for p in files):
            raise RuntimeError('Missing rendered frames: ' + tag)
        marker.write_text(signature)
        return tag

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(run, job) for job in jobs]
        for count, future in enumerate(concurrent.futures.as_completed(futures), 1):
            tag = future.result()
            state = dict(completed_jobs=count, total_jobs=len(jobs),
                         elapsed_seconds=round(time.time() - started), last_job=tag)
            (root / 'progress.json').write_text(json.dumps(state, indent=2) + '\n')
            print(json.dumps(state), flush=True)
    (root / 'COMPLETE').write_text('All render jobs completed successfully.\n')


if __name__ == '__main__':
    main()
