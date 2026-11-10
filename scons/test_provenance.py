"""Record which source tree and configuration produced each test executable.

Native test programs get a `<binary>.provenance.json` sidecar, written after the
link step whenever the program's alias is built, so the identity is computed
inside a build action rather than at every SConscript read and a commit that
changes no compiled input relinks nothing. Browser and mobile test executables
run away from their build directory, so they still embed the identity in
glob2/TestBuildProvenance.h (GLOB2_TEST_PROVENANCE_EMBEDDED).

The CMake build writes the same sidecar through write_sidecar
(scons/cmake_generate.py test-provenance), so this module imports SCons lazily.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
from build_layout import write_if_changed


def _source_identity(root):
    spec = importlib.util.spec_from_file_location('glob2_build_provenance', root / 'test/build_provenance.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.source_identity(root)


def _configuration(env):
    return dict(compiler=env.subst('$CXX'),
                flags={key: env.subst('$' + key) for key in ('CCFLAGS', 'CXXFLAGS', 'CPPDEFINES', 'LINKFLAGS')})


def _binary_sha256(binary, previous):
    stat = binary.stat()
    stamp = [stat.st_size, stat.st_mtime_ns]
    if previous.get('binaryStamp') == stamp and previous.get('binarySha256'):
        return previous['binarySha256'], stamp
    digest = hashlib.sha256()
    with binary.open('rb') as data:
        for block in iter(lambda: data.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest(), stamp


def write_sidecar(root, binary, sidecar, configuration):
    """Record the source identity, `configuration` and the binary's hash in `sidecar`."""
    path = Path(sidecar)
    try:
        previous = json.loads(path.read_text())
    except (OSError, ValueError):
        previous = {}
    metadata = _source_identity(Path(root))
    metadata.update(configuration)
    metadata['binarySha256'], metadata['binaryStamp'] = _binary_sha256(Path(binary), previous)
    write_if_changed(str(path), json.dumps(metadata, sort_keys=True) + '\n')


def attach_sidecar(env, program):
    """Write <program>.provenance.json after every build of `program`; returns the node."""
    from SCons.Script import Action, AlwaysBuild
    root = Path(env.Dir('#').srcnode().abspath)
    binary = Path(program[0].abspath)
    sidecar = str(binary) + '.provenance.json'
    configuration = _configuration(env)

    def generate(target, source, env):
        write_sidecar(root, binary, str(target[0]), configuration)
        return 0

    node = env.Command(sidecar, program, Action(generate, 'Recording $TARGET'))
    AlwaysBuild(node)
    return node


def register_test_provenance(env, output):
    """Embedded identity for browser and mobile test executables."""
    from SCons.Script import Action, Value
    root = Path(env.Dir('#').srcnode().abspath)
    metadata = _source_identity(root)
    metadata.update(_configuration(env))
    contents = '#pragma once\n// Generated test evidence identity; compiled into the producer.\n'
    contents += '#define GLOB2_TEST_PROVENANCE_EMBEDDED 1\n'
    contents += '#define GLOB2_TEST_PROVENANCE_JSON ' + json.dumps(json.dumps(metadata, sort_keys=True)) + '\n'
    target = (root / output).resolve() / 'include/glob2/TestBuildProvenance.h'
    def generate(target, source, env):
        write_if_changed(str(target[0]), contents)
        return 0
    return env.Command(str(target), Value(contents), Action(generate, 'Generating $TARGET'))
