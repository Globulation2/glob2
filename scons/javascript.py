"""Vendored, synchronous scripting runtime for every client toolchain."""
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from SCons.Script import Action, Flatten


def javascript_objects(env, directory, release, shared=False):
    local = env.Clone()
    local.Append(CPPPATH=['#third_party/quickjs-ng', '#third_party/openlibm/include',
                         '#third_party/openlibm/src'])
    local.Append(CFLAGS=['-O2' if release else '-O0', '-std=c11', '-fno-fast-math',
                        '-ffp-contract=off', '-fno-strict-aliasing', '-fno-builtin'])
    if env.get('size_optimization') == 'size':
        local.Append(CFLAGS=['-Os'])
    local.Append(CPPDEFINES=['_GNU_SOURCE', 'QUICKJS_NG_BUILD', '__BSD_VISIBLE'])
    local['CCFLAGS'] = [flag for flag in local.Split(local.get('CCFLAGS', []))
                        if flag != '-Werror']
    object_builder = local.SharedObject if shared else local.Object
    output = Path(directory)
    quick = ['quickjs.c', 'dtoa.c', 'libregexp.c', 'libunicode.c']
    objects = [object_builder(str(output / 'quickjs-ng' / name.replace('.c', '.o')),
                            '#third_party/quickjs-ng/' + name) for name in quick]
    math = sorted(Path(env.Dir('#third_party/openlibm/src').abspath).glob('*.c'))
    objects += [object_builder(str(output / 'openlibm' / (p.stem + '.o')),
                             '#third_party/openlibm/src/' + p.name) for p in math]
    from build_timing import record
    record(local, objects, "compile")
    numeric_guard(local, objects)
    return objects


def verify_symbols(target, source, env):
    # Keep the action free of dynamically imported module closures: those
    # otherwise change SCons signatures and rebuild every numeric object.
    subprocess.run([env['GLOB2_NUMERIC_PYTHON'], env['GLOB2_NUMERIC_GUARD'],
                    '--nm', env['GLOB2_NUMERIC_NM'],
                    *[node.abspath for node in target]], check=True)
    return 0


def numeric_guard(local, objects):
    """Check all script-reachable native numeric code, including host conversion."""
    guard = Path(local.Dir('#tools/javascript').abspath) / 'check-math-symbols.py'
    compiler = Path(shutil.which(shlex.split(str(local['CC']))[-1]) or str(local['CC']))
    # emsdk's emnm shell wrapper does not quote dirname($0), so a managed SDK
    # under macOS "Application Support" breaks it. Prefer LLVM's binary directly.
    candidates = [compiler.parent / 'llvm-nm', compiler.parent.parent / 'bin/llvm-nm',
                  compiler.parent / 'emnm',
                  compiler.parent / (compiler.name.replace('gcc', 'nm'))]
    nm = next((str(path) for path in candidates if path.is_file() and path != compiler),
              str(local.get('NM', 'nm')))
    local['GLOB2_NUMERIC_GUARD'] = str(guard)
    local['GLOB2_NUMERIC_NM'] = nm
    local['GLOB2_NUMERIC_PYTHON'] = sys.executable
    action = Action(verify_symbols, varlist=['GLOB2_NUMERIC_GUARD', 'GLOB2_NUMERIC_NM',
                                            'GLOB2_NUMERIC_PYTHON'])
    for group in objects:
        # PCH objects use a cloned compilation environment. Post-actions execute
        # with that object's environment, not necessarily the caller's local.
        for node in Flatten([group]):
            build_env = node.get_executor().get_build_env()
            for name in ('GLOB2_NUMERIC_GUARD', 'GLOB2_NUMERIC_NM', 'GLOB2_NUMERIC_PYTHON'):
                build_env[name] = local[name]
        local.AddPostAction(group, action)
        local.Depends(group, str(guard))


def strict_numeric_source(source):
    name = str(source).removeprefix('#').removeprefix('src/')
    return (name.startswith(('scripting/javascript/', 'map/generator/'))
            or name == 'ai/javascript/AIJavaScript.cpp') and name.endswith('.cpp')


def guarded_numeric_source(source):
    name = str(source).removeprefix('#').removeprefix('src/')
    return (name.startswith('scripting/javascript/') or name == 'ai/javascript/AIJavaScript.cpp'
            or name == 'map/generator/javascript/ToolkitBinding.cpp')
