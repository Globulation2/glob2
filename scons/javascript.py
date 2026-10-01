"""Vendored, synchronous scripting runtime for every client toolchain."""
from pathlib import Path
import importlib.util
import shlex
import shutil


def javascript_objects(env, directory, release, shared=False):
    local = env.Clone()
    local.Append(CPPPATH=['#third_party/quickjs-ng', '#third_party/openlibm/include',
                         '#third_party/openlibm/src'])
    local.Append(CFLAGS=['-O2' if release else '-O0', '-std=c11', '-fno-fast-math',
                        '-ffp-contract=off', '-fno-strict-aliasing', '-fno-builtin'])
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
    numeric_guard(local, objects)
    return objects


def numeric_guard(local, objects):
    """Check all script-reachable native numeric code, including host conversion."""
    guard = Path(local.Dir('#tools/javascript').abspath) / 'check-math-symbols.py'
    specification = importlib.util.spec_from_file_location('glob2_math_guard', guard)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    compiler = Path(shutil.which(shlex.split(str(local['CC']))[-1]) or str(local['CC']))
    candidates = [compiler.parent / 'llvm-nm', compiler.parent / 'emnm',
                  compiler.parent / (compiler.name.replace('gcc', 'nm'))]
    nm = next((str(path) for path in candidates if path.is_file() and path != compiler),
              str(local.get('NM', 'nm')))

    def verify_symbols(target, source, env):
        module.check([node.abspath for node in target], nm)
        return 0

    for group in objects:
        local.AddPostAction(group, verify_symbols)
        local.Depends(group, str(guard))
