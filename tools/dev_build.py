#!/usr/bin/env python3
"""Build glob2 for development, testing and measurement.

Arguments are SCons-style build options (key=value), targets and a few flags:

    python3 tools/dev_build.py                       # glob2, the dev tier
    python3 tools/dev_build.py . -j12                # glob2 and the four test binaries
    python3 tools/dev_build.py perf=1 engine-tests   # the perf tier
    python3 tools/dev_build.py perf=1 opt=3          # the perf-confirm tier
    python3 tools/dev_build.py release=1 glob2       # the release tier
    python3 tools/dev_build.py role=relay release=1 relay
    python3 tools/dev_build.py compile_profile=core engine-tests

Native Linux, macOS and Windows (MSYS2 MinGW, where mingw=1 is added) builds
use CMake + Ninja. The options resolve, through
buildsys/build_layout.py, to a build identity and its directory
(build/linux/client/debug/dev-..., build/linux/client/perf[/o3], ...), which
the CMake build configures with GLOB2_BUILD_ARGS. Without perf=1 or release=1
the dev tier's dev_fast=1 linker=auto are added. CC=, CXX=, CXXFLAGS= and
LINKFLAGS= set the compilers and base flags, --build=DIR another directory,
the install target installs as `scons install` does (into BINDIR=, INSTALLDIR=
and DATADIR=, a shipping build without the build prefix's rpath),
-jN the jobs (default 8), -k keeps going and -n only prints the commands.
A target may be a CMake target name or a file under the build directory
(glob2.exe names the glob2 target). On macOS the bundle and package targets
build Glob2.app and Glob2-<version>.dmg from a release build linked with the
pinned image libraries, as SConstruct's do.

Requests the CMake build does not cover yet (other targets and hosts,
recording codecs other than h264) go to SCons; --generator=scons or
--generator=cmake forces one, and GLOB2_DEV_GENERATOR sets the default.
"""
import argparse
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'buildsys'))

# Options that set the compilers, base flags and install directories rather than
# the build identity, with SConstruct's defaults.
TOOL_OPTIONS = ('CC', 'CXX', 'CXXFLAGS', 'LINKFLAGS', 'BINDIR', 'INSTALLDIR', 'DATADIR')
INSTALL_DEFAULTS = {'BINDIR': '/usr/local/bin', 'INSTALLDIR': '/usr/local/share', 'DATADIR': '/usr/local/share'}
# Identity keys the CMake build does not support yet.
SCONS_ONLY_KEYS = ('recording_codec', 'profile',
                   'portaudio', 'web_variant', 'web_legacy_gl', 'target', 'arch', 'amazon')


def enabled(value):
    return str(value).lower() in ('1', 'true', 'yes', 'on')


def windows_host():
    return platform.system() == 'Windows' or sys.platform == 'win32'


def development_defaults(options):
    """The dev tier's fast-development flags unless an optimized tier or the relay is
    asked for, and mingw=1 on Windows, whose only native toolchain is MSYS2's MinGW."""
    optimized = enabled(options.get('perf', 0)) or enabled(options.get('release', 0)) or options.get('role') == 'relay'
    defaults = ['mingw=1'] if windows_host() and 'mingw' not in options else []
    if 'dev_fast' not in options and not optimized:
        defaults.append('dev_fast=1')
    if 'linker' not in options and not optimized:
        defaults.append('linker=auto')
    return defaults


def command(arguments):
    """The SCons command line for these arguments, with the development defaults."""
    options = {word.split('=', 1)[0]: word.split('=', 1)[1] for word in arguments if '=' in word}
    # perf=1 is the optimized measurement tier: no fast-development flags, and the
    # platform linker so its output directory is build/<toolchain>/client/perf[/o3].
    perf = enabled(options.get('perf', '0'))
    defaults = ['dev_fast=1'] if 'dev_fast' not in options and not perf else []
    if 'linker' not in options and not perf:
        defaults.append('linker=auto')
    if options.get('target') == 'web' and 'web_variant' not in options:
        defaults.append('web_variant=threaded')
    if not any(word == '-j' or word.startswith('-j') or word.startswith('--jobs') for word in arguments):
        # The machine-wide token pool (buildsys/jobserver.py) shares the CPUs between
        # concurrent builds, so each lane may ask for all of them.
        defaults.append('-j' + str(os.cpu_count() or 1))
    return ['scons', *defaults, *arguments]


# "." is the everyday edit/build/test loop: glob2 and the four test binaries
# (the `tests` alias, with their provenance sidecars).
CMAKE_EVERYTHING = ['glob2', 'tests']


class Request:
    """A dev_build.py command line, split for the CMake build."""

    def __init__(self, arguments):
        self.build_args, self.tools, self.targets = [], {}, []
        self.jobs, self.keep_going, self.dry_run, self.directory = None, False, False, None
        for word in arguments:
            if word.startswith('--build='):
                self.directory = Path(word.split('=', 1)[1])
            elif word.startswith('-j') and word[2:].isdecimal():
                self.jobs = word
            elif word in ('-k', '--keep-going'):
                self.keep_going = True
            elif word == '-n':
                self.dry_run = True
            elif word == '.':
                self.targets += CMAKE_EVERYTHING
            elif word.startswith('-'):
                raise ValueError(word + ' is not a dev_build.py option')
            elif '=' in word and word.split('=', 1)[0] in TOOL_OPTIONS:
                key, value = word.split('=', 1)
                self.tools[key] = value
            elif '=' in word:
                self.build_args.append(word)
            else:
                self.targets.append(word)
        options = dict(word.split('=', 1) for word in self.build_args)
        self.build_args = development_defaults(options) + self.build_args
        self.options = dict(word.split('=', 1) for word in self.build_args)

    def identity(self):
        from build_layout import build_identity
        return build_identity(self.options)

    def build_directory(self):
        if self.directory:
            return self.directory if self.directory.is_absolute() else ROOT / self.directory
        from build_layout import default_directory
        return ROOT / default_directory(self.identity())

    def target_names(self):
        """Targets, with files under the build directory named by their CMake target."""
        directory = self.build_directory().resolve()
        names = []
        for target in self.targets:
            if '/' in target or os.sep in target:
                path = Path(target)
                path = (path if path.is_absolute() else ROOT / path).resolve()
                if directory not in path.parents:
                    raise ValueError(target + ' is not in the build directory ' + str(directory))
                names.append(path.name.removesuffix('.exe'))
            else:
                names.append(target)
        return list(dict.fromkeys(names))


def scons_reason(arguments):
    """Why these arguments need SCons, or None when the CMake build builds them."""
    host = 'mingw' if windows_host() else platform.system().lower()
    if host not in ('linux', 'darwin', 'mingw'):
        return 'the CMake build covers native Linux, macOS and MSYS2 MinGW builds'
    for tool in ('cmake', 'ninja'):
        if not shutil.which(tool):
            return tool + ' is not installed'
    try:
        request = Request(arguments)
    except ValueError as error:
        return str(error)
    for key in SCONS_ONLY_KEYS:
        if key in request.options:
            return key + '= is not supported by the CMake build yet'
    try:
        identity = request.identity()
    except ValueError as error:
        return 'SCons rejects the options too: ' + str(error)
    if identity['target'] != 'native' or identity['toolchain'] != host:
        return 'the CMake build covers native Linux, macOS and MSYS2 MinGW builds'
    version = subprocess.run(['cmake', '--version'], capture_output=True, text=True).stdout
    found = re.search(r'(\d+)\.(\d+)', version)
    if not found or (int(found.group(1)), int(found.group(2))) < (3, 25):
        return 'the CMake build needs CMake 3.25 or later'
    return None


def cached(directory, name):
    cache = directory / 'CMakeCache.txt'
    if not cache.exists():
        return None
    for line in cache.read_text().splitlines():
        if line.startswith(name + ':'):
            return line.split('=', 1)[1]
    return None


def cmake_commands(arguments):
    """Configure (when the build directory is new or its options changed) and build."""
    request = Request(arguments)
    directory = request.build_directory()
    # The install target installs as `scons install` does: a shipping build (no
    # build-prefix rpath or debug-path mapping) into BINDIR, INSTALLDIR and DATADIR.
    targets = request.target_names()
    install = 'install' in targets
    if install:
        targets = [target for target in targets if target != 'install'] or ['glob2']
    # SConstruct's defaults; restating them resets an earlier override.
    settings = {'GLOB2_BUILD_ARGS': ' '.join(request.build_args),
                'GLOB2_CXXFLAGS': request.tools.get('CXXFLAGS', '-g'),
                'GLOB2_LINKFLAGS': request.tools.get('LINKFLAGS', '-g'),
                'GLOB2_SHIPPING': 'ON' if install else 'OFF',
                # The interpreter running this script runs the build's generators; on
                # Windows FindPython would otherwise prefer a registered CPython to MSYS2's.
                'Python3_EXECUTABLE': Path(sys.executable).as_posix()}
    if platform.system() == 'Darwin':
        # SConstruct links the pinned image libraries only when the bundle is asked for.
        settings['GLOB2_MAC_BUNDLE'] = 'ON' if {'bundle', 'package'} & set(targets) else 'OFF'
    for key, default in INSTALL_DEFAULTS.items():
        settings['GLOB2_' + key] = request.tools.get(key, default)
    commands = []
    new = not (directory / 'build.ninja').exists()
    if new or any(cached(directory, name) != value for name, value in settings.items()):
        configure = ['cmake', '-S', str(ROOT), '-B', str(directory), '-G', 'Ninja',
                     '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        configure += ['-D' + name + '=' + value for name, value in settings.items()]
        commands.append(configure)
    build = ['cmake', '--build', str(directory)]
    if targets:
        build += ['--target', *targets]
    # Ninja does not draw from the SCons CPU token pool (buildsys/jobserver.py);
    # default to a share of the machine rather than all of it.
    build += ['--', request.jobs or '-j8']
    if request.keep_going:
        build += ['-k', '0']
    if request.dry_run:
        build.append('-n')
    commands.append(build)
    if install and not request.dry_run:
        commands.append(['cmake', '--install', str(directory)])
    return commands


def compiler_environment(arguments, environment):
    """CC= and CXX= reach CMake through the environment, as compilers are chosen at configure."""
    request = Request(arguments)
    result = dict(environment)
    for key in ('CC', 'CXX'):
        if key in request.tools:
            result[key] = request.tools[key]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cache-stats', action='store_true', help='show managed compiler-cache statistics and exit')
    parser.add_argument('--generator', choices=('auto', 'scons', 'cmake'),
                        default=os.environ.get('GLOB2_DEV_GENERATOR', 'auto'),
                        help='build system (default auto: CMake + Ninja for native Linux builds it covers, '
                             'else SCons; GLOB2_DEV_GENERATOR sets the default)')
    args, remaining = parser.parse_known_args()
    if args.generator not in ('auto', 'scons', 'cmake'):
        parser.error('GLOB2_DEV_GENERATOR must be auto, scons or cmake, not ' + args.generator)
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
        try:
            commands = cmake_commands(remaining)
            environment = compiler_environment(remaining, environment)
        except ValueError as error:
            raise SystemExit('dev_build.py: ' + str(error))
        for step in commands:
            print('+ ' + shlex.join(step), file=sys.stderr)
            status = subprocess.call(step, cwd=ROOT, env=environment)
            if status:
                raise SystemExit(status)
        raise SystemExit(0)
    raise SystemExit(subprocess.call(command(remaining), cwd=ROOT, env=environment))


if __name__ == '__main__':
    main()
