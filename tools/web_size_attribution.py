#!/usr/bin/env python3
"""Attribute a browser runtime's WebAssembly code bytes to source directories.

An edition build (buildsys/edition.py) links with --emit-symbol-map, which writes
index.js.symbols beside index.wasm: one `index:name` line per function. This tool
reads each function body's size from the wasm code section, finds the object file
that defines the function (llvm-nm over the build's objects) and sums the bytes by
source path, so the cost of a feature or a group of map generators is a number
rather than a guess. Inlined code counts toward the function it was inlined into;
template and library code without a defining game object is reported by name.

    python3 tools/web_size_attribution.py build/emscripten/client/release/lite \\
        --depth 3 --match map/generator/generators
"""

import argparse
import collections
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def leb128(data, offset):
    result = shift = 0
    while True:
        byte = data[offset]
        offset += 1
        result |= (byte & 0x7f) << shift
        if not byte & 0x80:
            return result, offset
        shift += 7


def function_sizes(wasm):
    """{function index: body bytes}, indexes counting imported functions first."""
    data = wasm.read_bytes()
    if data[:4] != b'\0asm':
        raise SystemExit(f'{wasm} is not WebAssembly')
    offset, imported, sizes = 8, 0, {}
    while offset < len(data):
        section, offset = data[offset], offset + 1
        length, offset = leb128(data, offset)
        end = offset + length
        if section == 2:  # imports: count the functions, which take the first indexes
            count, cursor = leb128(data, offset)
            for _ in range(count):
                for _ in range(2):  # module and field names
                    size, cursor = leb128(data, cursor)
                    cursor += size
                kind, cursor = data[cursor], cursor + 1
                if kind == 0:
                    imported += 1
                    _, cursor = leb128(data, cursor)
                elif kind == 1:  # table: reftype, limits
                    cursor += 1
                    flags, cursor = leb128(data, cursor)
                    _, cursor = leb128(data, cursor)
                    if flags & 1:
                        _, cursor = leb128(data, cursor)
                elif kind == 2:  # memory: limits
                    flags, cursor = leb128(data, cursor)
                    _, cursor = leb128(data, cursor)
                    if flags & 1:
                        _, cursor = leb128(data, cursor)
                elif kind == 3:  # global: type, mutability
                    cursor += 2
                elif kind == 4:  # tag: attribute, type
                    cursor += 1
                    _, cursor = leb128(data, cursor)
        elif section == 10:  # code
            count, cursor = leb128(data, offset)
            for index in range(count):
                size, cursor = leb128(data, cursor)
                sizes[imported + index] = size
                cursor += size
        offset = end
    return sizes


def symbol_map(path):
    names = {}
    for line in path.read_text(errors='replace').splitlines():
        index, _, name = line.partition(':')
        if index.isdigit():
            names[int(index)] = name
    return names


def defining_objects(build, nm):
    """{symbol: source path} for every function a game or library object defines."""
    owners = {}
    objects = sorted(build.glob('CMakeFiles/glob2_web_serial*.dir/**/*.o'))
    if not objects:
        raise SystemExit(f'no serial runtime objects under {build}/CMakeFiles')
    for chunk in range(0, len(objects), 200):
        batch = objects[chunk:chunk + 200]
        # Demangled, as the symbol map names functions.
        output = subprocess.run([nm, '--defined-only', '--print-file-name', '--demangle', *map(str, batch)],
                                capture_output=True, text=True, check=False).stdout
        for line in output.splitlines():
            location, _, rest = line.partition(': ')
            parts = rest.split(None, 2)
            if len(parts) < 3 or parts[1] not in 'TtWw':
                continue
            source = location.split('.dir/', 1)[-1].removesuffix('.o')
            owners.setdefault(parts[2], source)
    return owners


def pinned_nm(build):
    """LLVM's nm beside the pinned SDK's em++ (emnm is a wrapper script)."""
    cache = build / 'CMakeCache.txt'
    for line in cache.read_text(errors='replace').splitlines() if cache.is_file() else ():
        if line.startswith('CMAKE_CXX_COMPILER:'):
            nm = Path(line.split('=', 1)[1]).resolve().parent.parent / 'bin/llvm-nm'
            if nm.exists():
                return str(nm)
    # The managed SDK (platforms/web/toolchain.json) under the development store.
    for nm in sorted(Path.home().glob('.local/share/glob2/development/toolchains/emscripten/*/upstream/bin/llvm-nm')):
        return str(nm)
    return 'llvm-nm'


def group(path, depth):
    parts = Path(path).parts
    return '/'.join(parts[:depth])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build', type=Path, help='browser build directory with index.wasm and index.js.symbols')
    parser.add_argument('--depth', type=int, default=2, help='path components to group by')
    parser.add_argument('--match', help='only report paths containing this text, grouped by file')
    parser.add_argument('--top', type=int, default=40)
    parser.add_argument('--json', type=Path, help='also write {path: bytes} here')
    parser.add_argument('--nm', default=None, help='llvm-nm (default: the pinned SDK one)')
    args = parser.parse_args()
    wasm, symbols = args.build / 'index.wasm', args.build / 'index.js.symbols'
    if not symbols.is_file():
        raise SystemExit(f'{symbols} is missing; build an edition (it links with --emit-symbol-map)')
    nm = args.nm or pinned_nm(args.build)
    sizes = function_sizes(wasm)
    names = symbol_map(symbols)
    owners = defining_objects(args.build, nm)
    totals = collections.Counter()
    for index, size in sizes.items():
        name = names.get(index, '')
        source = owners.get(name)
        if source is None:
            source = ('(unattributed) ' + ('libc++/runtime' if name.startswith(('std::', '__')) or ' std::' in name[:40] else 'other'))
        totals[source] += size
    if args.json:
        args.json.write_text(json.dumps(dict(totals), indent=1, sort_keys=True))
    total = sum(totals.values())
    print(f'code section functions: {total / 1e6:.2f} MB raw over {len(sizes)} functions')
    grouped = collections.Counter()
    for source, size in totals.items():
        if args.match:
            if args.match in source:
                grouped[source] += size
        else:
            grouped[source if source.startswith('(') else group(source, args.depth)] += size
    shown = sum(grouped.values())
    for name, size in grouped.most_common(args.top):
        print(f'{size / 1e3:10.1f} kB  {100 * size / total:5.1f}%  {name}')
    if args.match:
        print(f'{shown / 1e3:10.1f} kB  {100 * shown / total:5.1f}%  total matching {args.match}')


if __name__ == '__main__':
    main()
