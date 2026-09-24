"""Locate native release outputs; GLOB2_BUILD_DIR selects a custom build identity."""
import os
from pathlib import Path
import platform

ROOT = Path(__file__).resolve().parents[1]


def native_build_directory():
    toolchain = 'mingw' if os.name == 'nt' else platform.system().lower()
    return ROOT / os.environ.get('GLOB2_BUILD_DIR', f'build/{toolchain}/client/release')


def native_binary(name='glob2'):
    return native_build_directory() / 'src' / (name + ('.exe' if os.name == 'nt' else ''))
