#!/usr/bin/env python3
"""Freeze and build one-factor release controls, retaining every build log."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
PLAN = json.loads((OUT / 'campaign-progress.json').read_text())
BASE = PLAN['base_commit']
MASK = '0-5,9-11,15-21,25-27,31'

def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()

def save(value):
    path = OUT / 'variant-builds.json'
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(value, indent=2) + '\n')
    tmp.replace(path)

state = {'base': BASE, 'state': 'preparing', 'variants': {}, 'jobs': 20, 'mask': MASK}
save(state)
for lane in ('worker', 'snapshot', 'telemetry'):
    directory = OUT / 'variants' / lane / 'source'
    directory.mkdir(parents=True, exist_ok=False)
    archive = OUT / 'variants' / lane / 'base.tar'
    with archive.open('wb') as f:
        subprocess.run(['git', 'archive', BASE], cwd=ROOT, stdout=f, check=True)
    subprocess.run(['tar', '-xf', str(archive), '-C', str(directory)], check=True)
    archive.unlink()
    for name in PLAN['candidate_lanes'][lane]:
        destination = directory / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
    state['variants'][lane] = {'root': str(directory), 'source_sha256': {
        name: sha(directory / name) for name in PLAN['candidate_lanes'][lane]}, 'state': 'prepared'}
    save(state)

environment = dict(os.environ, CCACHE='1', CCACHE_MAXSIZE='12G',
    GLOB2_SDL3_PREFIX='/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix')
for lane, record in state['variants'].items():
    directory = Path(record['root'])
    command = ['taskset', '-c', MASK, 'scons', 'release=1', 'CC=gcc-13', 'CXX=g++-13',
        'LINKFLAGS=-L/usr/lib/gcc/x86_64-linux-gnu/15', '-j20', 'build/linux/client/release/src/glob2']
    record.update(state='building', command=command)
    state['state'] = 'building'
    save(state)
    started = time.monotonic()
    with (directory.parent / 'build.log').open('w') as f:
        result = subprocess.run(command, cwd=directory, env=environment, stdout=f, stderr=subprocess.STDOUT)
    record.update(returncode=result.returncode, wall_seconds=time.monotonic()-started)
    binary = directory / 'build/linux/client/release/src/glob2'
    if result.returncode:
        record['state'] = state['state'] = 'failed'
        save(state)
        raise SystemExit(f'{lane} failed; see {directory.parent / "build.log"}')
    record.update(state='complete', binary=str(binary), binary_sha256=sha(binary))
    save(state)
    print(lane, 'built', record['wall_seconds'], flush=True)
state['state'] = 'complete'
save(state)
