"""Opt-in PCH and curated unity compilation, preserving production object reuse."""
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
from build_timing import record

_PCH_BUILDS = {}

HEADERS = ('algorithm', 'array', 'cstddef', 'cstdint', 'map', 'memory', 'optional', 'span', 'string', 'vector')


def unique(nodes):
    from SCons.Script import Flatten
    return list(dict.fromkeys(Flatten(list(nodes))))


def pch_environment(env):
    if not env.get('DEV_IDENTITY', {}).get('pch'):
        return env, []
    from SCons.Script import Action, Value
    from build_layout import write_if_changed
    local = env.Clone()
    compiler_words = shlex.split(str(env['CXX']))
    if compiler_words and Path(compiler_words[0]).name in ('ccache', 'ccache.exe'):
        compiler_words = compiler_words[1:]
        local['CXX'] = shlex.join(compiler_words)
    version = subprocess.check_output(compiler_words + ['--version'], env=env['ENV'], text=True)
    from mobile_toolchain import compiler_digest
    binary = shutil.which(compiler_words[0], path=env.get('ENV', {}).get('PATH')) or compiler_words[0]
    identity = [version, compiler_digest(binary), env.subst('$CXX $CCFLAGS $CXXFLAGS $CPPFLAGS $_CPPDEFFLAGS $_CPPINCFLAGS'), env.get('DEV_IDENTITY')]
    signature = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:20]
    directory = Path(env.Dir('#').abspath) / env['BUILDDIR'] / 'dev-pch' / signature
    header = directory / 'standard.hpp'
    content = '#pragma once\n' + ''.join('#include <' + name + '>\n' for name in HEADERS)
    def write_header(target, source, env):
        write_if_changed(str(target[0]), source[0].get_text_contents())
        return 0
    clang = 'clang' in version.lower() or 'emscripten' in version.lower()
    if str(header) not in _PCH_BUILDS:
        compiler = local.Clone()
        generated = compiler.Command(str(header), Value(content), Action(write_header, 'Generating $TARGET'))
        compiled = compiler.Command(str(header) + ('.pch' if clang else '.gch'), generated,
            '$CXX $CCFLAGS $CXXFLAGS $CPPFLAGS $_CPPDEFFLAGS $_CPPINCFLAGS -x c++-header $SOURCE -o $TARGET')
        # Discover standard-header dependencies on the first graph evaluation,
        # avoiding a needless second-build rebuild from newly loaded .d files.
        flags = compiler.Split(compiler.get('CCFLAGS', [])) + compiler.Split(compiler.get('CXXFLAGS', [])) + compiler.Split(compiler.get('CPPFLAGS', []))
        flags = [str(flag) for flag in flags if not str(flag).startswith('--use-port=')]
        defines = shlex.split(compiler.subst('$_CPPDEFFLAGS'))
        includes = ['-I' + compiler.Dir(path).abspath for path in compiler.get('CPPPATH', [])]
        dependencies = directory / 'standard.d'
        scan = subprocess.check_output([*compiler_words, *flags, *defines, *includes,
            '-M', '-MQ', str(compiled[0]), '-x', 'c++', '-'], input=content,
            env=compiler['ENV'], text=True)
        write_if_changed(dependencies, scan)
        compiler.ParseDepends(str(dependencies), must_exist=True)
        compiler.Clean(compiled, str(dependencies))
        record(compiler, compiled, 'compile')
        _PCH_BUILDS[str(header)] = compiled
    compiled = _PCH_BUILDS[str(header)]
    if clang:
        local.Append(CXXFLAGS=['-include-pch', str(compiled[0])])
    else:
        local.Append(CXXFLAGS=['-include', str(header), '-Winvalid-pch'])
    # ccache's PCH support requires sloppiness settings that this repository
    # deliberately forbids. PCH objects use the compiler directly; unrelated C
    # and Objective-C++ objects still use the enabled compiler cache.
    for name in ('CXXCOM', 'SHCXXCOM'):
        value = str(local.get(name, ''))
        if 'ccache' in value.split(' ', 1)[0]:
            local[name] = value.split(' ', 1)[1]
    return local, compiled


def unity_groups(env, sources):
    from javascript import strict_numeric_source
    if not env.get('DEV_IDENTITY', {}).get('unity'):
        return []
    root = Path(env.Dir('#').abspath)
    allowlist = set(json.loads((root / 'scons/unity-sources.json').read_text()))
    by_directory = {}
    for source in sources:
        path = Path(env.File(source).srcnode().abspath).relative_to(root).as_posix()
        if path in allowlist and not strict_numeric_source(path):
            by_directory.setdefault(str(Path(path).parent), []).append(source)
    return [group for directory in sorted(by_directory)
            for offset in range(0, len(by_directory[directory]), 4)
            if len(group := sorted(by_directory[directory])[offset:offset + 4]) > 1]


def objects(env, sources, target, shared=False):
    """Return source -> node mapping; several unity sources can share one node.

    target(source) preserves callers' existing ordinary object paths. C/ObjC++
    bypass the PCH and only checked-in production C++ files can enter unity.
    """
    from SCons.Script import Action, Value
    from build_layout import write_if_changed
    ordinary, pch = pch_environment(env)
    groups = unity_groups(env, sources)
    mapping = {}
    def write_unity(target, source, env):
        write_if_changed(str(target[0]), source[0].get_text_contents())
        return 0
    builder = ordinary.SharedObject if shared else ordinary.Object
    root = Path(env.Dir('#').abspath)
    for group in groups:
        content = ''.join('#include "' + Path(env.File(source).srcnode().abspath).as_posix() + '"\n' for source in group)
        signature = hashlib.sha256(content.encode()).hexdigest()[:20]
        path = root / env['BUILDDIR'] / 'dev-unity' / (signature + '.cpp')
        generated = ordinary.Command(str(path), Value(content), Action(write_unity, 'Generating $TARGET'))
        node = builder(str(path.with_suffix('.o')), generated)[0]
        ordinary.Depends(node, pch)
        record(ordinary, [node], "compile")
        for source in group:
            mapping[source] = node
            # Register ordinary source commands for compilation_db/clangd,
            # without making these extra objects dependencies of any program.
            ordinary.Object(target(source), source)
    for source in sources:
        if source in mapping:
            continue
        cpp = str(source).endswith('.cpp')
        local = ordinary if cpp else env
        builder = local.SharedObject if shared else local.Object
        node = builder(target(source), source)[0]
        if cpp:
            local.Depends(node, pch)
        record(local, [node], "compile")
        mapping[source] = node
    return mapping
