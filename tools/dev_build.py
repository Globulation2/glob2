#!/usr/bin/env python3
"""Build the full-featured fast development client; remaining arguments go to SCons.

--generator=cmake instead runs the CMake + Ninja dev preset (native Linux
only): it configures build/cmake/dev when needed, then builds the remaining
arguments as CMake targets (default: glob2; "." is glob2 and the four test
binaries; -k keeps going after errors; -jN, default -j8). SCons stays the
default and the release and packaging build.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def command(arguments):
    options = {word.split('=', 1)[0]: word.split('=', 1)[1] for word in arguments if '=' in word}
    # perf=1 is the optimized measurement tier: no fast-development flags, and the
    # platform linker so its output directory is build/<toolchain>/client/perf[/o3].
    perf = options.get('perf', '0').lower() in ('1', 'true', 'yes', 'on')
    defaults = ['dev_fast=1'] if 'dev_fast' not in options and not perf else []
    if 'linker' not in options and not perf:
        defaults.append('linker=auto')
    if options.get('target') == 'web' and 'web_variant' not in options:
        defaults.append('web_variant=threaded')
    if not any(word == '-j' or word.startswith('-j') or word.startswith('--jobs') for word in arguments):
        # The machine-wide token pool (scons/jobserver.py) shares the CPUs between
        # concurrent builds, so each lane may ask for all of them.
        defaults.append('-j' + str(os.cpu_count() or 1))
    return ['scons', *defaults, *arguments]


CMAKE_DIRECTORY = ROOT / 'build/cmake/dev'


# "." is the everyday edit/build/test loop: glob2 and the four test binaries
# (the `tests` alias, with their provenance sidecars).
CMAKE_EVERYTHING = ['glob2', 'tests']


def cmake_commands(arguments):
    """Configure (only when the build directory has no build files) and build."""
    jobs, targets, keep_going = [], [], False
    for word in arguments:
        if word.startswith('-j') and word[2:].isdecimal():
            jobs.append(word)
        elif word in ('-k', '--keep-going'):
            keep_going = True
        elif word == '.':
            targets += CMAKE_EVERYTHING
        elif word.startswith('-') or '=' in word:
            raise SystemExit('--generator=cmake accepts only targets, ".", -k and -jN, not ' + word
                             + ' (SCons options such as perf=1 have no CMake equivalent; use SCons)')
        else:
            targets.append(word)
    commands = []
    if not (CMAKE_DIRECTORY / 'build.ninja').exists():
        commands.append(['cmake', '--preset', 'dev'])
    # Ninja does not draw from the SCons CPU token pool (scons/jobserver.py);
    # default to a share of the machine rather than all of it.
    build = ['cmake', '--build', '--preset', 'dev']
    if targets:
        build += ['--target', *dict.fromkeys(targets)]
    build += ['--', jobs[-1] if jobs else '-j8']
    if keep_going:
        build += ['-k', '0']
    commands.append(build)
    return commands


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache-stats', action='store_true', help='show managed compiler-cache statistics and exit')
    parser.add_argument('--generator', choices=('scons', 'cmake'), default='scons',
                        help='build system (cmake: the native Linux CMake + Ninja dev preset)')
    args, remaining = parser.parse_known_args()
    sys.path.insert(0, str(ROOT / 'scons'))
    import dev_store
    environment = dict(os.environ)
    environment.setdefault('CCACHE', '1')
    if args.cache_stats:
        if 'CCACHE_DIR' not in environment:
            import ccache
            managed = dev_store.cache(ROOT, 'ccache', lease=False)
            ccache.configure_managed(managed)
            environment['CCACHE_DIR'] = str(managed)
        raise SystemExit(subprocess.call(['ccache', '--show-stats'], env=environment))
    if args.generator == 'cmake':
        for step in cmake_commands(remaining):
            status = subprocess.call(step, cwd=ROOT, env=environment)
            if status:
                raise SystemExit(status)
        raise SystemExit(0)
    raise SystemExit(subprocess.call(command(remaining), cwd=ROOT, env=environment))


if __name__ == '__main__':
    main()
