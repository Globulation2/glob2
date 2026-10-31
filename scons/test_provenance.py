"""Embed source/config identity in test executables, rather than relabeling at run time."""
import importlib.util
import json
from pathlib import Path
from SCons.Script import Action, Value
from build_layout import write_if_changed


def register_test_provenance(env, output):
    root = Path(env.Dir('#').srcnode().abspath)
    target = (root / output).resolve() / 'include/glob2/TestBuildProvenance.h'
    if not (root / '.git').exists():
        # Release archives have no Git checkout. Merely registering unused test
        # targets must not prevent building/installing the game from an archive.
        # Do not accidentally borrow an enclosing repository's provenance.
        def unavailable(target, source, env):
            raise ValueError('Test build provenance requires a Git checkout; '
                             'build the selected release tag in Git to run evidence-producing harnesses')
        return env.Command(str(target), Value('source archive: no Git provenance'),
                           Action(unavailable, 'Checking test build provenance'))
    spec = importlib.util.spec_from_file_location('glob2_build_provenance', root / 'test/build_provenance.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    metadata = module.source_identity(root)
    if env.get('installed_paths'):
        metadata['installedPaths'] = True
    metadata.update(compiler=env.subst('$CXX'),
                    flags={key: env.subst('$' + key) for key in
                           ('CFLAGS', 'CCFLAGS', 'CPPFLAGS', 'CXXFLAGS', 'CPPDEFINES', 'LINKFLAGS')})
    contents = '#pragma once\n// Generated test evidence identity; compiled into the producer.\n'
    contents += '#define GLOB2_TEST_PROVENANCE_JSON ' + json.dumps(json.dumps(metadata, sort_keys=True)) + '\n'
    def generate(target, source, env):
        write_if_changed(str(target[0]), contents)
        return 0
    return env.Command(str(target), Value(contents), Action(generate, 'Generating $TARGET'))
