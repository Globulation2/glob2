#!/usr/bin/env python3
"""Build the full-featured fast development client.

On native Linux with CMake 3.25+ and Ninja installed, the dev tier builds with
the CMake + Ninja dev preset: it configures build/cmake/dev when needed, then
builds the remaining arguments as CMake targets (default: glob2; "." is glob2
and the four test binaries; -k keeps going after errors; -jN, default -j8).

Everything else goes to SCons, which stays the release, packaging and perf
build: SCons options (perf=1, target=web, compile_profile=core, --build=...,
-n and other flags), SCons file targets, targets CMake does not define, other
host systems, and hosts without CMake or Ninja. --generator=scons or
--generator=cmake forces one; GLOB2_DEV_GENERATOR sets the same default.
"""
import argparse
import os
from pathlib import Path
import platform
import re
import shutil
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


def scons_reason(arguments):
    """Why these arguments need SCons, or None when the CMake dev preset builds them."""
    if platform.system() != 'Linux':
        return 'the CMake build is native Linux only'
    for tool in ('cmake', 'ninja'):
        if not shutil.which(tool):
            return tool + ' is not installed'
    for word in arguments:
        if word in ('.', '-k', '--keep-going') or (word.startswith('-j') and word[2:].isdecimal()):
            continue
        if '=' in word or word.startswith('-'):
            return word + ' is a SCons option'
        if '/' in word or os.sep in word:
            return word + ' is a SCons file target'
    version = subprocess.run(['cmake', '--version'], capture_output=True, text=True).stdout
    found = re.search(r'(\d+)\.(\d+)', version)
    if not found or (int(found.group(1)), int(found.group(2))) < (3, 25):
        return 'the CMake build needs CMake 3.25 or later'
    return None


def unknown_targets(targets):
    """Requested targets the configured CMake build does not define."""
    return [target for target in targets
            if subprocess.run(['ninja', '-C', str(CMAKE_DIRECTORY), '-t', 'query', target],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode]


def cmake_targets(arguments):
    return [word for word in arguments if not word.startswith('-') and word != '.']


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
    parser.add_argument('--generator', choices=('auto', 'scons', 'cmake'),
                        default=os.environ.get('GLOB2_DEV_GENERATOR', 'auto'),
                        help='build system (default auto: the native Linux CMake + Ninja dev preset when it '
                             'can build the request, else SCons; GLOB2_DEV_GENERATOR sets the default)')
    args, remaining = parser.parse_known_args()
    if args.generator not in ('auto', 'scons', 'cmake'):
        parser.error('GLOB2_DEV_GENERATOR must be auto, scons or cmake, not ' + args.generator)
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
    generator = args.generator
    if generator == 'auto':
        reason = scons_reason(remaining)
        generator = 'scons' if reason else 'cmake'
        if reason:
            print('dev_build.py: building with SCons (' + reason + ')', file=sys.stderr)
    if generator == 'cmake':
        commands = cmake_commands(remaining)
        if len(commands) == 2:
            status = subprocess.call(commands.pop(0), cwd=ROOT, env=environment)
            if status:
                if args.generator == 'auto':
                    print('dev_build.py: configuring the CMake dev build failed; --generator=scons builds with SCons',
                          file=sys.stderr)
                raise SystemExit(status)
        unknown = unknown_targets(cmake_targets(remaining))
        if unknown and args.generator == 'auto':
            # SCons aliases the CMake build does not define (the relay, browser or
            # packaging aliases) still build, with SCons.
            print('dev_build.py: building with SCons (CMake defines no target ' + ', '.join(unknown) + ')',
                  file=sys.stderr)
        else:
            status = subprocess.call(commands[0], cwd=ROOT, env=environment)
            if status == 0:
                directory = CMAKE_DIRECTORY.relative_to(ROOT)
                print(f'dev_build.py: CMake dev build in {directory}; test tools that pick the newest build do '
                      f'not look there: pass --build-dir {directory} or set GLOB2_BUILD_DIR={directory}',
                      file=sys.stderr)
            raise SystemExit(status)
    raise SystemExit(subprocess.call(command(remaining), cwd=ROOT, env=environment))


if __name__ == '__main__':
    main()
