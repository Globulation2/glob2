#!/usr/bin/env python3
"""Build the full-featured fast development client; remaining arguments go to SCons."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def command(arguments):
    options = {word.split('=', 1)[0]: word.split('=', 1)[1] for word in arguments if '=' in word}
    defaults = ['dev_fast=1'] if 'dev_fast' not in options else []
    if 'linker' not in options:
        defaults.append('linker=auto')
    if options.get('target') == 'web' and 'web_variant' not in options:
        defaults.append('web_variant=threaded')
    if not any(word == '-j' or word.startswith('-j') or word.startswith('--jobs') for word in arguments):
        defaults.append('-j' + str(min(os.cpu_count() or 1, 8)))
    return ['scons', *defaults, *arguments]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache-stats', action='store_true', help='show managed compiler-cache statistics and exit')
    args, remaining = parser.parse_known_args()
    sys.path.insert(0, str(ROOT / 'scons'))
    import dev_store
    environment = dict(os.environ)
    environment.setdefault('CCACHE', '1')
    environment.setdefault('CCACHE_MAXSIZE', '12G')
    if args.cache_stats:
        environment['CCACHE_DIR'] = environment.get('CCACHE_DIR', str(dev_store.cache(ROOT, 'ccache', lease=False)))
        raise SystemExit(subprocess.call(['ccache', '--show-stats'], env=environment))
    raise SystemExit(subprocess.call(command(remaining), cwd=ROOT, env=environment))


if __name__ == '__main__':
    main()
