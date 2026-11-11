"""Vendored, synchronous scripting runtime for every client toolchain."""
from pathlib import Path


# The interpreter's exact compilation contract, also exported to CMake by
# buildsys/export_cmake_sources.py. Paths are relative to the checkout.
INCLUDE_DIRECTORIES = ('third_party/quickjs-ng', 'third_party/openlibm/include',
                       'third_party/openlibm/src')
STRICT_CFLAGS = ('-std=c11', '-fno-fast-math', '-ffp-contract=off', '-fno-strict-aliasing', '-fno-builtin')
DEFINES = ('_GNU_SOURCE', 'QUICKJS_NG_BUILD', '__BSD_VISIBLE')
QUICKJS_SOURCES = ('quickjs.c', 'dtoa.c', 'libregexp.c', 'libunicode.c')
# Host C++ that converts script numbers uses the same strict boundary.
STRICT_CXXFLAGS = ('-fno-fast-math', '-ffp-contract=off')


def openlibm_sources(root):
    return sorted(Path(root, 'third_party/openlibm/src').glob('*.c'))


def strict_numeric_source(source):
    name = str(source).removeprefix('#').removeprefix('src/')
    return (name.startswith(('scripting/javascript/', 'map/generator/'))
            or name == 'ai/javascript/AIJavaScript.cpp') and name.endswith('.cpp')


def guarded_numeric_source(source):
    name = str(source).removeprefix('#').removeprefix('src/')
    return (name.startswith('scripting/javascript/') or name == 'ai/javascript/AIJavaScript.cpp'
            or name == 'map/generator/javascript/ToolkitBinding.cpp')
