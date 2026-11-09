"""Locate native build outputs; GLOB2_BUILD_DIR selects a custom build identity.

Without GLOB2_BUILD_DIR the newest native client build wins: the fast development
build that tools/dev_build.py produces (build/<toolchain>/client/debug/dev-*) or
the release build (build/<toolchain>/client/release), judged by its binaries.
study_binary() also considers the optimized perf builds (dev_build.py perf=1),
for map studies and sweeps that run thousands of generations.
"""
import os
from pathlib import Path
import platform

ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = '.exe' if os.name == 'nt' else ''


def _newest_output(directory):
    times = []
    for name in ('src/glob2', 'test/glob2-engine-tests', 'test/glob2-unit-tests'):
        try:
            times.append((directory / (name + EXECUTABLE)).stat().st_mtime)
        except OSError:
            pass
    return max(times, default=None)


def native_build_directory():
    if os.environ.get('GLOB2_BUILD_DIR'):
        return ROOT / os.environ['GLOB2_BUILD_DIR']
    toolchain = 'mingw' if os.name == 'nt' else platform.system().lower()
    release = ROOT / 'build' / toolchain / 'client/release'
    candidates = [release, *sorted((ROOT / 'build' / toolchain / 'client/debug').glob('dev-*'))]
    built = [(time, path) for path in candidates if (time := _newest_output(path)) is not None]
    return max(built)[1] if built else release


def native_binary(name='glob2'):
    return native_build_directory() / 'src' / (name + EXECUTABLE)


def _client_builds():
    toolchain = 'mingw' if os.name == 'nt' else platform.system().lower()
    client = ROOT / 'build' / toolchain / 'client'
    return [client / 'release', client / 'perf', client / 'perf' / 'o3',
            *sorted((client / 'debug').glob('dev-*'))]


def study_binary():
    """The newest native client, perf builds included: (path, optimized).

    Newest, not fastest, so a study never runs code older than the last build.
    A perf build is several times faster than a dev build for generation.
    """
    if os.environ.get('GLOB2_BUILD_DIR'):
        return native_binary(), 'debug' not in native_binary().parts
    built = []
    for path in _client_builds():
        binary = path / 'src' / ('glob2' + EXECUTABLE)
        try:
            built.append((binary.stat().st_mtime, binary, 'debug' not in path.parts))
        except OSError:
            pass
    if not built:
        return native_binary(), False
    _, binary, optimized = max(built)
    return binary, optimized
