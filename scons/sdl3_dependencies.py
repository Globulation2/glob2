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
import tempfile
import urllib.request

LOCK = Path(__file__).with_name('sdl3-versions.json')
SDL_PATCHES = tuple(LOCK.parent / 'vcpkg-ports' / 'sdl3' / name for name in
                    ('x11-reparent-race.patch', 'x11-map-notify.patch'))


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
            raise ValueError(f'Dependency archive checksum mismatch: {archive}')


def apply_source_patches(source, patches=SDL_PATCHES):
    """Apply the same reviewed SDL fixes as vcpkg, rejecting unexpected sources."""
    source = Path(source).resolve()
    # A private repository anchors git's working tree even when Windows/MSYS
    # disagree about path separators in GIT_CEILING_DIRECTORIES. Never discover
    # the application repository or skip paths because the SDK is beneath it.
    environment = dict(os.environ)
    for variable in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE', 'GIT_COMMON_DIR'):
        environment.pop(variable, None)
    with tempfile.TemporaryDirectory(prefix='glob2-sdl-patches-') as temporary:
        subprocess.run(['git', 'init', '--bare', '--quiet', temporary],
                       env=environment, check=True)
        git = ['git', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
               '--git-dir', temporary, '--work-tree', str(source), 'apply']
        for patch in patches:
            # Windows checkout can use CRLF while checksum-verified archives use LF.
            # Write canonical bytes, avoiding Python's Windows text-mode conversion.
            normalized = Path(temporary) / 'source.patch'
            normalized.write_bytes(Path(patch).read_bytes().replace(b'\r\n', b'\n'))
            path = str(normalized)
            check = subprocess.run(git + ['--check', path], cwd=source,
                                   env=environment, capture_output=True, text=True)
            if check.returncode:
                reverse = subprocess.run(git + ['--reverse', '--check', path], cwd=source,
                                         env=environment, capture_output=True, text=True)
                if reverse.returncode:
                    raise RuntimeError(f'SDL source patch does not apply: {patch}\n{check.stderr}')
            else:
                subprocess.run(git + ['--whitespace=nowarn', path],
                               cwd=source, env=environment, check=True)


def patch_image_exports(source):
    # SDL_image3.4.6's SHELL linker option splits source paths containing spaces.
    # CMake's LINKER form preserves the exports filename as one argument.
    script = Path(source) / 'CMakeLists.txt'
    contents = script.read_text()
    script.write_text(contents.replace('SHELL:-Wl,-exported_symbols_list,', 'LINKER:-exported_symbols_list,'))


def static_webp_archive(prefix, required=True):
    prefix = Path(prefix)
    sharpyuv = next((prefix / 'lib' / filename for filename in
                     ('libsharpyuv.a', 'sharpyuv.lib')
                     if (prefix / 'lib' / filename).is_file()), None)
    if sharpyuv is None:
        if required:
            raise RuntimeError('Pinned static SharpYUV library was not installed')
        return None
    return sharpyuv


def static_webp_link_options(prefix, required=True):
    sharpyuv = static_webp_archive(prefix, required)
    if sharpyuv is None:
        return []
    libraries = [str(sharpyuv)]
    if platform.system() == 'Linux':
        libraries.append('m')
    return ['-Dwebp_LINK_LIBRARIES=' + ';'.join(libraries)]


def build(prefix, work, jobs=2, emscripten=None, environment=None, threaded=False):
    prefix, work = Path(prefix).resolve(), Path(work).resolve()
    versions = json.loads(LOCK.read_text())
    if emscripten:
        versions = json.loads(LOCK.with_name('sdl3-vendored.json').read_text()) | versions
    identity = {'configuration': 11, 'threaded': threaded, 'versions': versions, 'emscripten': str(emscripten) if emscripten else None,
                'platform': platform.platform(), 'machine': platform.machine(),
                'source_patches': {patch.name: hashlib.sha256(patch.read_bytes()).hexdigest()
                                   for patch in SDL_PATCHES}}
    if emscripten:
        # The browser runtimes use native WebAssembly exceptions; setjmp/longjmp in
        # these libraries (FreeType) must use the matching WebAssembly mechanism.
        identity['exceptions'] = 'wasm'
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
        if name == 'SDL':
            apply_source_patches(source)
        if name == 'SDL_image':
            patch_image_exports(source)
        if name == 'SDL_net':
            # Same MinGW portability patch as the pinned vcpkg overlay. SDL_net
            # 3.2.0 names its Winsock helpers read/write, colliding with io.h.
            implementation = source / 'src/SDL_net.c'
            contents = implementation.read_text()
            if 'static int write(SOCKET' in contents:
                contents = contents.replace('static int write(SOCKET', 'static int SDLNetWrite(SOCKET')
                contents = contents.replace('static int read(SOCKET', 'static int SDLNetRead(SOCKET')
                contents = contents.replace("// WSAPoll doesn't exist", "// Glob2 portability patch: keep private Winsock helpers distinct from MinGW I/O.\n#define write SDLNetWrite\n#define read SDLNetRead\n\n// WSAPoll doesn't exist")
                implementation.write_text(contents)
        output = work / (source.name + '-build')
        command = ['cmake', '-S', str(source), '-B', str(output),
                   '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
                   '-DCMAKE_PREFIX_PATH=' + str(prefix), '-DCMAKE_INSTALL_LIBDIR=lib',
                   '-DSDL_TESTS=OFF', '-DSDL_TEST_LIBRARY=OFF', '-DSDL_EXAMPLES=OFF',
                   '-DSDLIMAGE_WEBP=ON', '-DSDLIMAGE_WEBP_SAVE=OFF', '-DSDLIMAGE_SAMPLES=OFF', '-DSDLIMAGE_TESTS=OFF', '-DSDLIMAGE_DEPS_SHARED=OFF',
                   '-DSDLTTF_SAMPLES=OFF', '-DSDLTTF_TESTS=OFF', '-DSDLTTF_HARFBUZZ=OFF',
                   '-DSDLNET_SAMPLES=OFF', '-DSDLNET_TESTS=OFF']
        if name == 'SDL_image':
            # SDL_image's Findwebp omits static libwebp's private dependencies.
            # Its function table references encoders even when saving is off.
            command += static_webp_link_options(prefix)
        if name == 'webp':
            command += ['-DBUILD_SHARED_LIBS=OFF', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
                        '-DWEBP_BUILD_CWEBP=OFF', '-DWEBP_BUILD_DWEBP=OFF', '-DWEBP_BUILD_VWEBP=OFF',
                        '-DWEBP_BUILD_WEBPINFO=OFF', '-DWEBP_BUILD_IMG2WEBP=OFF', '-DWEBP_BUILD_WEBPMUX=OFF',
                        '-DWEBP_BUILD_EXTRAS=OFF', '-DWEBP_BUILD_ANIM_UTILS=OFF', '-DWEBP_BUILD_LIBWEBPMUX=ON',
                        '-DBUILD_TESTING=OFF']
        if not emscripten and platform.system() == "Linux":
            command += ['-DCMAKE_INSTALL_RPATH=$ORIGIN']
        if emscripten:
            command = [str(Path(emscripten) / 'emcmake')] + command
            command += ['-DCMAKE_FIND_ROOT_PATH=' + str(prefix),
                        '-DSDL3_DIR=' + str(prefix / 'lib/cmake/SDL3'),
                        '-DFT_DISABLE_ZLIB=ON', '-DFT_DISABLE_BZIP2=ON', '-DFT_DISABLE_PNG=ON',
                        '-DFT_DISABLE_BROTLI=ON', '-DFT_DISABLE_HARFBUZZ=ON',
                        '-DSDL_PTHREADS=' + ('ON' if threaded else 'OFF'),
                        '-DCMAKE_C_FLAGS=' + ('-pthread ' if threaded else '') + '-sSUPPORT_LONGJMP=wasm',
                        '-DCMAKE_CXX_FLAGS=' + ('-pthread ' if threaded else '') + '-fwasm-exceptions',
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
