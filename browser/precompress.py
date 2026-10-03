#!/usr/bin/env python3
"""Write Brotli (.br) and gzip (.gz) copies of the browser client's large files.

Web servers that serve precompressed files (deploy/Caddyfile rewrites /play/ requests
to them; a bucket can use Content-Encoding metadata) then send them
without compressing on every request. Copies newer than their source are kept, so
unchanged content-addressed data packages are not compressed again.

    python3 browser/precompress.py [build/emscripten/client/release]
"""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

try:
    import brotli
except ImportError:
    brotli = None


RUNTIME_FILES = ('index.js', 'index.wasm', 'threaded/index.js', 'threaded/index.wasm', 'loader.js')


def targets(directory):
    # Both runtimes (serial fallback and threaded/), the loader that picks one, and
    # the data packages they share.
    files = [directory / name for name in RUNTIME_FILES if (directory / name).is_file()]
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


def compress(source, quality):
    gz, br = source.with_name(source.name + '.gz'), source.with_name(source.name + '.br')
    if not fresh(gz, source):
        write(gz, gzip.compress(source.read_bytes(), compresslevel=9, mtime=0))
    if not fresh(br, source):
        data = compress_brotli(source, quality)
        if data is not None:
            write(br, data)
    return {encoding: copy.stat().st_size for encoding, copy in (('br', br), ('gzip', gz)) if copy.is_file()}


def record_sizes(page, sizes):
    """Give the loading page the transfer size of each file per encoding.

    Responses rewritten to a precompressed copy carry no Content-Length, and
    downloads report decoded bytes, so the page scales its progress with these.
    """
    text = page.read_text()
    value = json.dumps(sizes, separators=(',', ':'), sort_keys=True)
    text, count = re.subn(r"data-wire-sizes=(?:'[^']*'|\"\{\}\"|\{\})", f"data-wire-sizes='{value}'", text, count=1)
    if count != 1:
        raise SystemExit(f'precompress: {page} lacks the data-wire-sizes placeholder')
    write(page, text.encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('directory', nargs='?', default='build/emscripten/client/release', type=Path)
    parser.add_argument('--require-brotli', action='store_true', help='fail when no Brotli encoder is available')
    arguments = parser.parse_args()
    if brotli is None and not shutil.which('brotli'):
        if arguments.require_brotli:
            sys.exit('precompress: install the brotli command or Python module')
        print('precompress: no Brotli encoder; writing gzip copies only', file=sys.stderr)
    sizes = {}
    for source in targets(arguments.directory):
        # Data packages are mostly PNG and Ogg, which barely compress; quality 11
        # would take minutes for a percent or two.
        name = source.relative_to(arguments.directory).as_posix()
        sizes[name] = compress(source, 9 if source.suffix == '.data' else 11)
        print(f'{name}: {source.stat().st_size / 1e6:.2f} MB, ' +
              ' '.join(f'{encoding} {size / 1e6:.2f} MB' for encoding, size in sizes[name].items()))
    page = arguments.directory / 'index.html'
    if page.is_file():
        record_sizes(page, sizes)
        compress(page, 11)


if __name__ == '__main__':
    main()
