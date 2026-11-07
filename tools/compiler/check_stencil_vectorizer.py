#!/usr/bin/env python3
"""Compile tools/compiler/torus_stencil_check.cpp at -O0, -O2 and -O2 without the
vectorizers and require every build to print the same checksum, twice.

A mismatch, a run-to-run difference or an AddressSanitizer report means the
compiler miscompiles the generators' stencil shape: report it and see "Compiler
checks" in docs/development/build-options.md for the decision rule.

    python3 tools/compiler/check_stencil_vectorizer.py [--cxx clang++] [--no-asan]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE = Path(__file__).with_name('torus_stencil_check.cpp')
VARIANTS = {
    'O0': ['-O0'],
    'O2': ['-O2'],
    'O2-no-vectorize': ['-O2', '-fno-vectorize', '-fno-slp-vectorize'],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('clang++') or 'c++')
    parser.add_argument('--no-asan', action='store_true')
    args = parser.parse_args()
    clang = b'clang' in subprocess.run([args.cxx, '--version'], capture_output=True).stdout
    sanitize = [] if args.no_asan or os.name == 'nt' else ['-fsanitize=address', '-fno-omit-frame-pointer']
    results = {}
    with tempfile.TemporaryDirectory(prefix='glob2-stencil-') as directory:
        for name, flags in VARIANTS.items():
            if not clang and 'no-vectorize' in name:
                flags = ['-O2', '-fno-tree-vectorize', '-fno-tree-slp-vectorize']
            binary = Path(directory) / name
            subprocess.run([args.cxx, '-std=c++20', *flags, *sanitize, str(SOURCE), '-o', str(binary)], check=True)
            outputs = {subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout.strip()
                       for _ in range(2)}
            results[name] = outputs
            print(f'{name:18s} {" ".join(sorted(outputs))}')
    distinct = set().union(*results.values())
    if len(distinct) != 1:
        print('stencil checksums differ between optimization levels or runs: the compiler miscompiles this loop',
              file=sys.stderr)
        return 1
    print('stencil checksums agree')
    return 0


if __name__ == '__main__':
    sys.exit(main())
