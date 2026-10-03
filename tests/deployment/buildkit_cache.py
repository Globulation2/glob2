#!/usr/bin/env python3
"""Carry the image builds' BuildKit cache mounts across CI runs.

deploy/Dockerfile compiles the engine and the relay inside `RUN --mount=type=cache`
steps, so a rebuild on the same Docker host is incremental (SCons reuses the pinned
SDL3 prefix and every unchanged object file). A CI runner starts with an empty
BuildKit store, so the platform-stack job would compile everything from scratch on
every run. This script moves those cache mounts in and out of a directory that
actions/cache saves and restores:

  python3 tests/deployment/buildkit_cache.py load DIR   # before the image build
  python3 tests/deployment/buildkit_cache.py save DIR   # after a successful build

`load` seeds each mount from DIR/<id>/cache.tar when present; `save` writes each
mount (whatever the build left in it) to DIR/<id>/cache.tar. Both run a one-step
build on the default builder, which shares its BuildKit store with
`docker compose build`. Python standard library only.
"""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

# deploy/Dockerfile's UBUNTU_IMAGE: already pulled for the build, with GNU tar.
HELPER_IMAGE = 'ubuntu:24.04'


def architecture():
    machine = platform.machine().lower()
    return {'x86_64': 'amd64', 'amd64': 'amd64', 'aarch64': 'arm64', 'arm64': 'arm64'}.get(machine, machine)


def cache_ids():
    # The ids of deploy/Dockerfile's cache mounts that the platform stack builds
    # (the legacy server build is not part of it).
    arch = architecture()
    return [f'glob2-engine-build-{arch}', f'glob2-relay-build-{arch}', 'glob2-npm']


LOAD = f"""# syntax=docker/dockerfile:1.7
FROM {HELPER_IMAGE}
ARG ID
RUN --mount=type=cache,id=${{ID}},target=/cache,sharing=locked \\
    --mount=type=bind,source=.,target=/seed \\
    tar -xf /seed/cache.tar -C /cache && du -sh /cache
"""

SAVE = f"""# syntax=docker/dockerfile:1.7
FROM {HELPER_IMAGE} AS pack
ARG ID
RUN --mount=type=cache,id=${{ID}},target=/cache,sharing=locked \\
    mkdir -p /out && tar -cf /out/cache.tar --exclude=./sdl3/sources -C /cache . && du -sh /cache
FROM scratch
COPY --from=pack /out/cache.tar /
"""


def build(dockerfile, context, *extra):
    command = ['docker', 'build', '--no-cache', '--progress', 'plain', '-f', '-', *extra, str(context)]
    env = {**os.environ, 'DOCKER_BUILDKIT': '1'}
    result = subprocess.run(command, input=dockerfile, text=True, env=env)
    return result.returncode == 0


def load(directory):
    for cache_id in cache_ids():
        seed = directory / cache_id / 'cache.tar'
        if not seed.is_file():
            print(f'{cache_id}: no saved cache, the build starts cold')
            continue
        print(f'{cache_id}: loading {seed.stat().st_size // (1 << 20)} MiB')
        if build(LOAD, seed.parent, '--build-arg', f'ID={cache_id}'):
            seed.unlink()  # the runner's disk is small; save writes a fresh one
        else:
            # A broken cache only costs time: the build then starts cold.
            print(f'::warning::{cache_id}: could not load the saved BuildKit cache')
    return 0


def save(directory):
    with tempfile.TemporaryDirectory() as empty:
        for cache_id in cache_ids():
            target = directory / cache_id
            target.mkdir(parents=True, exist_ok=True)
            if build(SAVE, empty, '--build-arg', f'ID={cache_id}', '--output', f'type=local,dest={target}'):
                print(f'{cache_id}: saved {(target / "cache.tar").stat().st_size // (1 << 20)} MiB')
            else:
                # Like a failed load, a failed save only costs the next run time.
                print(f'::warning::{cache_id}: could not save the BuildKit cache')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('action', choices=('load', 'save'))
    parser.add_argument('directory', type=Path)
    arguments = parser.parse_args()
    return load(arguments.directory) if arguments.action == 'load' else save(arguments.directory)


if __name__ == '__main__':
    sys.exit(main())
