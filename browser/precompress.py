#!/usr/bin/env python3
"""Write Brotli (.br) and gzip (.gz) copies of the browser client's large files.

Web servers that serve precompressed files (Caddy's `file_server { precompressed }`
in deploy/Caddyfile, or a bucket with Content-Encoding metadata) then send them
without compressing on every request. Copies newer than their source are kept, so
unchanged content-addressed data packages are not compressed again.

    python3 browser/precompress.py [build/emscripten/client/release]
"""
import argparse
import gzip
import os
from pathlib import Path
import shutil
import subprocess
import sys

try:
    import brotli
except ImportError:
    brotli = None


def targets(directory):
    files = [directory / name for name in ('index.js', 'index.wasm') if (directory / name).is_file()]
    return files + sorted((directory / 'assets').glob('*.data'))


def fresh(copy, source):
    return copy.is_file() and copy.stat().st_mtime_ns >= source.stat().st_mtime_ns


def write(path, data):
    temporary = path.with_name('.' + path.name + '.tmp')
    temporary.write_bytes(data)
    os.replace(temporary, path)


def compress_brotli(source, quality):
    if brotli is not None:
        return brotli.compress(source.read_bytes(), quality=quality, lgwin=24)
    tool = shutil.which('brotli')
    if not tool:
        return None
    return subprocess.run([tool, '-c', '-q', str(quality), '-w', '24', str(source)],
                          check=True, capture_output=True).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('directory', nargs='?', default='build/emscripten/client/release', type=Path)
    parser.add_argument('--require-brotli', action='store_true', help='fail when no Brotli encoder is available')
    arguments = parser.parse_args()
    if brotli is None and not shutil.which('brotli'):
        if arguments.require_brotli:
            sys.exit('precompress: install the brotli command or Python module')
        print('precompress: no Brotli encoder; writing gzip copies only', file=sys.stderr)
    for source in targets(arguments.directory):
        # Data packages are mostly PNG and Ogg, which barely compress; quality 11
        # would take minutes for a percent or two.
        quality = 9 if source.suffix == '.data' else 11
        size = source.stat().st_size
        gz, br = source.with_name(source.name + '.gz'), source.with_name(source.name + '.br')
        if not fresh(gz, source):
            write(gz, gzip.compress(source.read_bytes(), compresslevel=9, mtime=0))
        if not fresh(br, source):
            data = compress_brotli(source, quality)
            if data is not None:
                write(br, data)
        sizes = ' '.join(f'{p.suffix[1:]} {p.stat().st_size / 1e6:.2f} MB' for p in (br, gz) if p.is_file())
        print(f'{source.relative_to(arguments.directory)}: {size / 1e6:.2f} MB, {sizes}')


if __name__ == '__main__':
    main()
