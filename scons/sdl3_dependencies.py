#!/usr/bin/env python3
"""Build the pinned SDL3 family into an isolated prefix (native or Emscripten)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import urllib.request

LOCK = Path(__file__).with_name('sdl3-versions.json')


def download(work, versions):
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    for spec in versions.values():
        archive = work / spec['archive']
        if not archive.exists():
            temporary = archive.with_suffix(archive.suffix + '.download')
            with urllib.request.urlopen(spec['url'], timeout=60) as response:
                temporary.write_bytes(response.read())
            temporary.replace(archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != spec['sha256']:
            raise ValueError(f'SDL archive checksum mismatch: {archive}')


def build(prefix, work, jobs=2, emscripten=None, environment=None):
    prefix, work = Path(prefix).resolve(), Path(work).resolve()
    versions = json.loads(LOCK.read_text())
    if emscripten:
        versions = json.loads(LOCK.with_name('sdl3-vendored.json').read_text()) | versions
    identity = {'configuration': 3, 'versions': versions, 'emscripten': str(emscripten) if emscripten else None,
                'platform': platform.platform(), 'machine': platform.machine()}
    manifest = prefix / 'sdl3-manifest.json'
    libraries = ('SDL3', 'SDL3_image', 'SDL3_ttf', 'SDL3_net')
    installed = all(any((prefix / 'lib').glob('*' + name + '*')) for name in libraries)
    if installed and manifest.exists() and json.loads(manifest.read_text()) == identity:
        return prefix
    work.mkdir(parents=True, exist_ok=True)
    prefix.mkdir(parents=True, exist_ok=True)
    env = dict(environment if environment is not None else os.environ)
    env['CMAKE_PREFIX_PATH'] = str(prefix) + os.pathsep + env.get('CMAKE_PREFIX_PATH', '')
    download(work, versions)
    for name, spec in versions.items():
        archive = work / spec['archive']
        source = work / archive.name.removesuffix('.tar.gz')
        if not source.exists():
            with tarfile.open(archive) as package:
                package.extractall(work, filter='data')
        output = work / (source.name + '-build')
        command = ['cmake', '-S', str(source), '-B', str(output),
                   '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
                   '-DCMAKE_PREFIX_PATH=' + str(prefix), '-DCMAKE_INSTALL_LIBDIR=lib',
                   '-DSDL_TESTS=OFF', '-DSDL_TEST_LIBRARY=OFF', '-DSDL_EXAMPLES=OFF',
                   '-DSDLIMAGE_SAMPLES=OFF', '-DSDLIMAGE_TESTS=OFF', '-DSDLIMAGE_DEPS_SHARED=OFF',
                   '-DSDLTTF_SAMPLES=OFF', '-DSDLTTF_TESTS=OFF', '-DSDLTTF_HARFBUZZ=OFF',
                   '-DSDLNET_SAMPLES=OFF', '-DSDLNET_TESTS=OFF']
        if emscripten:
            command = [str(Path(emscripten) / 'emcmake')] + command
            command += ['-DCMAKE_FIND_ROOT_PATH=' + str(prefix),
                        '-DSDL3_DIR=' + str(prefix / 'lib/cmake/SDL3'),
                        '-DFT_DISABLE_ZLIB=ON', '-DFT_DISABLE_BZIP2=ON', '-DFT_DISABLE_PNG=ON',
                        '-DFT_DISABLE_BROTLI=ON', '-DFT_DISABLE_HARFBUZZ=ON', '-DSDL_PTHREADS=OFF',
                        '-DBUILD_SHARED_LIBS=OFF', '-DSDL_SHARED=OFF', '-DSDL_STATIC=ON',
                        '-DSDLTTF_VENDORED=OFF', '-DSDLTTF_HARFBUZZ=OFF']
        subprocess.run(command, env=env, check=True)
        subprocess.run(['cmake', '--build', str(output), '--parallel', str(jobs)], env=env, check=True)
        subprocess.run(['cmake', '--install', str(output)], env=env, check=True)
    manifest.write_text(json.dumps(identity, indent=2) + '\n')
    return prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix')
    parser.add_argument('--download-only', action='store_true')
    parser.add_argument('--work', required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--emscripten')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.download_only:
        download(args.work, json.loads(LOCK.read_text()))
    elif args.prefix:
        build(args.prefix, args.work, args.jobs, args.emscripten)
    else:
        parser.error("--prefix is required unless --download-only is used")


if __name__ == '__main__':
    main()
