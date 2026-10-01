"""Vendored, synchronous scripting runtime for every client toolchain."""
from pathlib import Path


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
    return objects
