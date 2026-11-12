#!/usr/bin/env python3
"""Build glob2 for development, testing and measurement.

Arguments are key=value build options, targets and a few flags:

    python3 tools/dev_build.py                       # glob2, the dev tier
    python3 tools/dev_build.py . -j12                # glob2 and the four test binaries
    python3 tools/dev_build.py perf=1 engine-tests   # the perf tier
    python3 tools/dev_build.py perf=1 opt=3          # the perf-confirm tier
    python3 tools/dev_build.py release=1 glob2       # the release tier
    python3 tools/dev_build.py role=relay release=1 relay
    python3 tools/dev_build.py compile_profile=core engine-tests

Every build uses CMake + Ninja: native Linux, macOS and Windows (in MSYS2's
MINGW64 shell, where mingw=1 is added), target=android, target=ios (on macOS)
and target=web. The options resolve, through buildsys/build_layout.py, to a
build identity and its directory (build/linux/client/debug/dev-...,
build/linux/client/perf[/o3], build/emscripten/client/release, ...), which
CMake configures with GLOB2_BUILD_ARGS; dev_build.py reconfigures a directory
whose options changed. Native builds without perf=1, release=1 or profile=1
add the dev tier's dev_fast=1 linker=auto; browser development builds add
web_variant=threaded too.

CC=, CXX=, CXXFLAGS= and LINKFLAGS= set the compilers and base flags (CXXFLAGS
and LINKFLAGS replace the default -g), portaudio=1 enables the PortAudio voice
backend, --build=DIR picks another directory, -jN the jobs (default 8), -k
keeps going and -n only prints the commands. A target may be a CMake target or
a file under the build directory (glob2.exe names glob2; a browser file names
web-package). The install target builds a shipping binary (no build-prefix
rpath) and runs `cmake --install` into BINDIR=, INSTALLDIR= and DATADIR=. On
macOS the bundle and package targets build Glob2.app and Glob2-<version>.dmg,
linked with the pinned image libraries. Mobile builds take android_sdk=,
mobile_deps= and developer_dir=, and build lib/libmain.so or lib/libglob2.a
and the android-tests and ios-tests targets.
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
# the build identity.
TOOL_OPTIONS = ('CC', 'CXX', 'CXXFLAGS', 'LINKFLAGS', 'BINDIR', 'INSTALLDIR', 'DATADIR', 'portaudio')
INSTALL_DEFAULTS = {'BINDIR': '/usr/local/bin', 'INSTALLDIR': '/usr/local/share', 'DATADIR': '/usr/local/share'}


def enabled(value):
    return str(value).lower() in ('1', 'true', 'yes', 'on')


def windows_host():
    return platform.system() == 'Windows' or sys.platform == 'win32'


def development_defaults(options):
    """The dev tier's fast-development flags unless an optimized tier or the relay is
    asked for, and mingw=1 on Windows, whose only native toolchain is MSYS2's MinGW."""
    target = options.get('target', 'native')
    if target in ('android', 'ios'):
        return []  # the mobile drivers' builds keep their own optimization levels
    optimized = (enabled(options.get('perf', 0)) or enabled(options.get('release', 0))
                 or enabled(options.get('profile', 0)) or options.get('role') == 'relay')
    defaults = ['mingw=1'] if windows_host() and target == 'native' and 'mingw' not in options else []
    if target == 'web' and not optimized and 'web_variant' not in options:
        defaults.append('web_variant=threaded')  # one runtime for development
    if 'dev_fast' not in options and not optimized:
        defaults.append('dev_fast=1')
    if 'linker' not in options and not optimized:
        defaults.append('linker=auto')
    return defaults


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
                # The browser's page and runtimes come from its web-package target.
                web = self.options.get('target') == 'web'
                names.append('web-package' if web else path.name.removesuffix('.exe'))
            else:
                names.append(target)
        return list(dict.fromkeys(names))


def check_tools(arguments):
    """Stop with a reason when this host cannot build these arguments."""
    for tool in ('cmake', 'ninja'):
        if not shutil.which(tool):
            raise ValueError(tool + ' is not installed (pip install cmake ninja provides both)')
    version = subprocess.run(['cmake', '--version'], capture_output=True, text=True).stdout
    found = re.search(r'(\d+)\.(\d+)', version)
    if not found or (int(found.group(1)), int(found.group(2))) < (3, 25):
        raise ValueError('the build needs CMake 3.25 or later')
    identity = Request(arguments).identity()
    host = 'mingw' if windows_host() else platform.system().lower()
    if identity['target'] == 'native' and identity['toolchain'] != host:
        raise ValueError('native ' + identity['toolchain'] + ' builds run on that host; this is ' + host)


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
    # The install target: a shipping build (no build-prefix rpath or debug-path
    # mapping) installed into BINDIR, INSTALLDIR and DATADIR.
    targets = request.target_names()
    install = 'install' in targets
    if install:
        targets = [target for target in targets if target != 'install'] or ['glob2']
    # The defaults; restating them resets an earlier override.
    # Quoted for CMake's separate_arguments(UNIX_COMMAND): mobile_deps= and android_sdk=
    # paths live under ~/Library/Application Support on macOS.
    settings = {'GLOB2_BUILD_ARGS': shlex.join(request.build_args),
                'GLOB2_CXXFLAGS': request.tools.get('CXXFLAGS', '-g'),
                'GLOB2_LINKFLAGS': request.tools.get('LINKFLAGS', '-g'),
                'GLOB2_SHIPPING': 'ON' if install else 'OFF',
                # The interpreter running this script runs the build's generators; on
                # Windows FindPython would otherwise prefer a registered CPython to MSYS2's.
                'Python3_EXECUTABLE': Path(sys.executable).as_posix()}
    if request.options.get('target', 'native') != 'native':
        # Mobile builds take their compilers and flags from the pinned toolchain.
        settings = {name: settings[name] for name in ('GLOB2_BUILD_ARGS', 'Python3_EXECUTABLE')}
    elif platform.system() == 'Darwin':
        # Only app bundles link the pinned image libraries.
        settings['GLOB2_MAC_BUNDLE'] = 'ON' if {'bundle', 'package'} & set(targets) else 'OFF'
    for key, default in INSTALL_DEFAULTS.items():
        if request.options.get('target', 'native') == 'native':
            settings['GLOB2_' + key] = request.tools.get(key, default)
    if request.options.get('target', 'native') == 'native':
        # portaudio=1: the PortAudio voice backend.
        settings['GLOB2_PORTAUDIO'] = 'ON' if enabled(request.tools.get('portaudio', 0)) else 'OFF'
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
    # Default to a share of the machine rather than all of it.
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
    args, remaining = parser.parse_known_args()
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
    try:
        check_tools(remaining)
        commands = cmake_commands(remaining)
        environment = compiler_environment(remaining, environment)
    except ValueError as error:
        raise SystemExit('dev_build.py: ' + str(error))
    for step in commands:
        print('+ ' + shlex.join(step), file=sys.stderr)
        status = subprocess.call(step, cwd=ROOT, env=environment)
        if status:
            raise SystemExit(status)


if __name__ == '__main__':
    main()
