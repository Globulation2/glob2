#!/usr/bin/env python3
"""Root-owned exclusive compilation after the sealed timing queue completes."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

ART = Path(__file__).resolve().parent


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for part in iter(lambda: stream.read(1048576), b''):
            h.update(part)
    return h.hexdigest()


def load(path):
    return json.loads(path.read_text())


def write(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def check_source(source, manifest):
    for item in manifest['changes']:
        assert sha(source / item['path']) == item['candidate_sha256'], item['path']


def main():
    # Launched without taskset; keep the complete caller allocation for builds.
    cpus = sorted(os.sched_getaffinity(0))
    assert len(cpus) == 32, ('unexpected compilation allocation', cpus)
    review_path = ART / 'gpu-independent-review/review.json'
    review = load(review_path)
    review_sha = sha(review_path)
    deadline = time.monotonic() + 10800
    print('Waiting for all 68+24 timing jobs; compilation allocation:', cpus, flush=True)
    while True:
        status = load(ART / 'next-timing-phases-status.json')
        assert status['stage'] != 'failed', status
        if status['stage'] == 'complete':
            assert status['state'] == 'complete'
            break
        assert time.monotonic() < deadline, 'timing queue deadline exceeded'
        time.sleep(10)
    for folder, count in [('v2-followup', 68), ('telemetry-replication', 24)]:
        status = load(ART / folder / 'status.json')
        assert status['complete'] and status['selected_jobs'] == count and not status['errors'], status
    assert Path('/home/bradley/.codex/worktrees/a630/glob2/artifacts/engine-history-oazis/compile-dispatch-hold.flag').exists()
    for path in Path('/proc').glob('[0-9]*/cmdline'):
        try:
            args = path.read_bytes().split(b'\0')
        except OSError:
            continue
        assert not (Path(os.fsdecode(args[0])).name == 'glob2' and b'game' in args and b'run' in args), ('native game still active', path)
    for path, digest in review['source_sha256'].items():
        assert sha(path) == digest, ('reviewed source changed', path)
    spec = importlib.util.spec_from_file_location('inventory_helpers', ART / 'v2-conformance/run.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    baseline = load(ART / 'campaign.json')
    for path, digest in baseline['baseline_libraries'].items():
        assert sha(path) == digest, ('dependency changed', path)
    outcomes = []
    for name in ('gpu-sync', 'gpu-recovery'):
        folder = ART / 'variants' / name
        source = folder / 'source'
        prep = load(folder / 'source-manifest.json')
        assert prep['base_commit'] == '2ced60e06b8e7fa7a4a4ecff944c40bda494bcf4'
        assert not (folder / 'manifest.json').exists(), 'retain previous build outcomes'
        check_source(source, prep)
        prep_sha = sha(folder / 'source-manifest.json')
        command = ['scons', 'release=1', 'CC=gcc-13', 'CXX=g++-13',
                   'LINKFLAGS=-L/usr/lib/gcc/x86_64-linux-gnu/15', '-j32',
                   'build/linux/client/release/src/glob2', 'unit-tests', 'engine-tests']
        overrides = dict(CCACHE='1', CCACHE_MAXSIZE='12G',
                         GLOB2_SDL3_PREFIX='/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix')
        env = dict(os.environ, **overrides)
        started = time.monotonic()
        print('Compiling', name, 'with all 32 CPUs', flush=True)
        with (folder / 'build.log').open('xb') as log:
            subprocess.run(command, cwd=source, env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        elapsed = time.monotonic() - started
        check_source(source, prep)
        assert sha(review_path) == review_sha
        assert sha(folder / 'source-manifest.json') == prep_sha
        inventory = helper.inventory(source)
        result = dict(state='complete', root=str(source), base_commit=prep['base_commit'],
                      source_manifest_sha256=prep_sha, review_sha256=review_sha,
                      source_sha256={str(Path(p).relative_to(source)): h for p, h in inventory.items()},
                      command=command, environment_overrides=overrides, compilation_cpus=cpus,
                      build_wall_s=elapsed, build_log_sha256=sha(folder / 'build.log'),
                      compiler=subprocess.check_output(['g++-13', '--version'], text=True).splitlines()[0],
                      flags='release=1; ordinary O3/s; no dev_fast/PCH/unity',
                      dependency_hashes=baseline['baseline_libraries'], accepted=False,
                      qualification='Build complete; focused/device/game tests still required. Cached incremental build time is not a clean compile benchmark.')
        for role, relative in [('binary', 'src/glob2'), ('engine_binary', 'test/glob2-engine-tests'),
                               ('unit_binary', 'test/glob2-unit-tests')]:
            binary = source / 'build/linux/client/release' / relative
            result[role] = str(binary)
            result[role + '_sha256'] = sha(binary)
        write(folder / 'manifest.json', result)
        outcomes.append(dict(variant=name, build_wall_s=elapsed, binary_sha256=result['binary_sha256']))
        print('Built', name, 'in', round(elapsed, 2), 'seconds', flush=True)
    write(ART / 'gpu-build-complete.json', dict(complete=True, builds=outcomes, accepted=False))


if __name__ == '__main__':
    main()
