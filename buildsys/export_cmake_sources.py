#!/usr/bin/env python3
"""Write cmake/sources.cmake and cmake/tests.cmake from the shared manifests.

The CMake build (CMakeLists.txt) reads its source lists, include
directories, the QuickJS/openlibm strict-numeric contract, the test registry
(test/tests.py) and the harness programs (buildsys/harness_programs.py) from the
generated files, so the two build systems cannot drift:
test/build_system/test_cmake_sources.py fails while a committed file differs
from what this script writes.

    python3 buildsys/export_cmake_sources.py          # rewrite cmake/*.cmake
    python3 buildsys/export_cmake_sources.py --check  # exit 1 when one is stale
"""
import argparse
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DIRECTORY = ROOT / 'cmake'
sys.path.insert(0, str(HERE))

import harness_programs  # noqa: E402
import javascript  # noqa: E402
import sources  # noqa: E402

# test/tests.py, loaded by path so test/'s other modules cannot shadow ours.
_spec = importlib.util.spec_from_file_location('glob2_test_registry', ROOT / 'test/tests.py')
registry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(registry)

# Native builds add these sources (the MCP server) to CLIENT_SOURCES.
NATIVE_CLIENT_SOURCES = ('mcp/Tools.cpp', 'mcp/Protocol.cpp', 'mcp/HttpServer.cpp')
ENTRY_POINT = 'app/Glob2.cpp'


def _list(name, values, comment):
    lines = ['# ' + comment, 'set(' + name]
    lines += ['    ' + value for value in values]
    return '\n'.join(lines) + '\n)\n'


def content():
    client = ['src/' + name for name in (*sources.CLIENT_SOURCES, *NATIVE_CLIENT_SOURCES)]
    strict = [path for path in client if javascript.strict_numeric_source(path)]
    guarded = [path for path in client if javascript.guarded_numeric_source(path)]
    openlibm = [path.relative_to(ROOT).as_posix() for path in javascript.openlibm_sources(ROOT)]
    web = sources.web_sources()
    mobile = list(dict.fromkeys(sources.mobile_sources('android') + sources.mobile_sources('ios')))
    mobile_strict = [path for path in mobile if javascript.strict_numeric_source(path)]
    mobile_guarded = [path for path in mobile if javascript.guarded_numeric_source(path)]
    blocks = [
        _list('GLOB2_CLIENT_SOURCES', client,
              'buildsys/sources.py CLIENT_SOURCES plus the native-only sources.'),
        'set(GLOB2_CLIENT_ENTRY_POINT src/' + ENTRY_POINT + ')\n',
        _list('GLOB2_STRICT_NUMERIC_SOURCES', strict,
              'Client sources compiled with GLOB2_STRICT_CXXFLAGS (javascript.strict_numeric_source).'),
        _list('GLOB2_GUARDED_NUMERIC_SOURCES', guarded,
              'Client sources whose objects the numeric symbol guard checks (javascript.guarded_numeric_source).'),
        _list('GLOB2_STRICT_CXXFLAGS', javascript.STRICT_CXXFLAGS, 'javascript.STRICT_CXXFLAGS.'),
        _list('GLOB2_GAG_SOURCES', ['libs/gag/src/' + name for name in sources.GAG_SOURCES],
              'buildsys/sources.py GAG_SOURCES.'),
        _list('GLOB2_ANDROID_SOURCES', sources.mobile_sources('android'),
              'buildsys/sources.py mobile_sources("android"): libmain.so (cmake/mobile.cmake).'),
        _list('GLOB2_IOS_SOURCES', sources.mobile_sources('ios'),
              'buildsys/sources.py mobile_sources("ios"): libglob2.a (cmake/mobile.cmake).'),
        _list('GLOB2_IOS_ARC_SOURCES', list(sources.IOS_ARC_SOURCES), 'buildsys/sources.py IOS_ARC_SOURCES.'),
        _list('GLOB2_MOBILE_STRICT_NUMERIC_SOURCES', mobile_strict,
              'Mobile sources compiled with GLOB2_STRICT_CXXFLAGS (javascript.strict_numeric_source).'),
        _list('GLOB2_MOBILE_GUARDED_NUMERIC_SOURCES', mobile_guarded,
              'Mobile sources whose objects the numeric symbol guard checks.'),
        _list('GLOB2_WEB_SOURCES', web, 'buildsys/sources.py web_sources(): the browser runtime (cmake/web.cmake).'),
        _list('GLOB2_WEB_STRICT_NUMERIC_SOURCES', [path for path in web if javascript.strict_numeric_source(path)],
              'Browser sources compiled with GLOB2_STRICT_CXXFLAGS.'),
        _list('GLOB2_WEB_GUARDED_NUMERIC_SOURCES', [path for path in web if javascript.guarded_numeric_source(path)],
              'Browser sources whose objects the numeric symbol guard checks.'),
        *[_list('GLOB2_WEB_WORKER_' + name + '_SOURCES', list(paths), 'buildsys/sources.py WEB_WORKER_SOURCES.')
          for name, paths in sources.WEB_WORKER_SOURCES.items()],
        _list('GLOB2_GAG_DARWIN_SOURCES', ['libs/gag/src/' + name for name in sources.GAG_DARWIN_SOURCES],
              'buildsys/sources.py GAG_DARWIN_SOURCES.'),
        'set(GLOB2_USL_RUNNER_SOURCE ' + sources.USL_RUNNER_SOURCE + ')\n',
        _list('GLOB2_USL_SOURCES', ['libs/usl/src/' + name for name in sources.USL_SOURCES],
              'buildsys/sources.py USL_SOURCES.'),
        _list('GLOB2_RELAY_SOURCES', ['src/' + name for name in sources.RELAY_SOURCES],
              'buildsys/sources.py RELAY_SOURCES.'),
        _list('GLOB2_RELAY_GAG_SOURCES', ['libs/gag/src/' + name for name in sources.RELAY_GAG_SOURCES],
              'buildsys/sources.py RELAY_GAG_SOURCES.'),
        'set(GLOB2_RELAY_MAIN_SOURCE src/' + sources.RELAY_MAIN_SOURCE + ')\n',
        *[_list('GLOB2_' + platform.upper() + '_TRUST_SOURCES', list(paths),
                'sources.PLATFORM_TRUST_SOURCES: the ' + platform + ' certificate-trust sources.')
          for platform, paths in sources.PLATFORM_TRUST_SOURCES.items()],
        _list('GLOB2_RELAY_TEST_SOURCES', ['src/' + name for name in sources.RELAY_TEST_SOURCES],
              'buildsys/sources.py RELAY_TEST_SOURCES: glob2-relay-tests beside the relay objects.'),
        _list('GLOB2_MAP_SOURCES', ['src/' + name for name in sources.MAP_SOURCES],
              'buildsys/sources.py MAP_SOURCES: the glob2_map module library.'),
        _list('GLOB2_SIM_SOURCES', ['src/' + name for name in sources.SIM_SOURCES],
              'buildsys/sources.py SIM_SOURCES: the glob2_sim module library.'),
        _list('GLOB2_OPTIONAL_GROUPS', list(sources.OPTIONAL_SOURCES),
              'buildsys/sources.py OPTIONAL_SOURCES: the groups compile_profile=core leaves out.'),
        _list('GLOB2_CORE_EXCLUDED_SOURCES',
              ['src/' + name for name in sources.CLIENT_SOURCES
               if name not in sources.profile_sources('core')],
              'Client sources in an optional group, which compile_profile=core does not build.'),
        _list('GLOB2_INCLUDE_DIRECTORIES', sources.INCLUDE_DIRECTORIES,
              'buildsys/sources.py INCLUDE_DIRECTORIES, in search order.'),
        _list('GLOB2_JAVASCRIPT_SOURCES',
              ['third_party/quickjs-ng/' + name for name in javascript.QUICKJS_SOURCES] + openlibm,
              'buildsys/javascript.py QUICKJS_SOURCES and openlibm_sources().'),
        _list('GLOB2_JAVASCRIPT_INCLUDE_DIRECTORIES', javascript.INCLUDE_DIRECTORIES,
              'javascript.INCLUDE_DIRECTORIES.'),
        _list('GLOB2_JAVASCRIPT_CFLAGS', javascript.STRICT_CFLAGS,
              'javascript.STRICT_CFLAGS (after the -O0/-O2 level).'),
        _list('GLOB2_JAVASCRIPT_DEFINES', javascript.DEFINES, 'javascript.DEFINES.'),
    ]
    return ('# Generated by buildsys/export_cmake_sources.py from buildsys/sources.py and\n'
            '# buildsys/javascript.py; do not edit. Regenerate after changing either.\n\n'
            + '\n'.join(blocks))


def _entries(entries):
    """(repository path, options) of test/tests.py registry entries."""
    for entry in entries:
        source, options = (entry, {}) if isinstance(entry, str) else entry
        yield registry.source_path(source), options


def _groups(name, entries, comment):
    """Entries compiled alike (same cxxflags, defines and requirements) share a group."""
    groups = {}
    for path, options in entries:
        key = (tuple(options.get('cxxflags', ())), tuple(options.get('defines', ())),
               tuple(sorted(options.get('require', ()))))
        groups.setdefault(key, []).append(path)
    blocks = ['# ' + comment + '\nset(' + name + '_GROUPS ' + ' '.join(str(i) for i in range(len(groups))) + ')\n']
    for index, ((cxxflags, defines, require), paths) in enumerate(groups.items()):
        prefix = f'{name}_{index}'
        blocks.append(_list(prefix + '_SOURCES', paths, f'Group {index}.'))
        blocks.append(_list(prefix + '_CXXFLAGS', cxxflags, 'Extra compile flags of the group.'))
        blocks.append(_list(prefix + '_DEFINES', defines, 'Extra definitions of the group.'))
        blocks.append(_list(prefix + '_REQUIRE', require,
                            "Build features the group needs ('mac', 'wss', 'opengl', 'not-mingw')."))
    return blocks


def test_binaries():
    """The native test binaries: (key, target, compiled entries, production entries,
    module libraries, every client object?, aliases that build it)."""
    return (
        ('ENGINE_TESTS', 'glob2-engine-tests', registry.SUPPORT + registry.MAC_SUPPORT + registry.ENGINE_SUPPORT + registry.ENGINE_TESTS,
         [], (), True, ('engine-tests', 'tests')),
        ('UNIT_TESTS', 'glob2-unit-tests', registry.SUPPORT + registry.MAC_SUPPORT + registry.UNIT_STUBS + registry.UNIT_TESTS,
         registry.UNIT_PRODUCTION_SOURCES, (), False, ('unit-tests', 'tests')),
        # The module binaries hold cases moved out of the two above, so engine-tests and
        # unit-tests build them too.
        ('MAP_TESTS', 'glob2-map-tests', registry.SUPPORT + registry.MAC_SUPPORT + registry.MAP_STUBS + registry.MAP_TESTS,
         registry.MAP_PRODUCTION_SOURCES, ('map',), False, ('map-tests', 'unit-tests', 'tests')),
        ('SIM_TESTS', 'glob2-sim-tests',
         registry.SUPPORT + registry.MAC_SUPPORT + registry.SIM_SUPPORT + registry.SIM_STUBS + registry.SIM_TESTS,
         registry.SIM_PRODUCTION_SOURCES, ('sim', 'map'), False, ('sim-tests', 'engine-tests', 'tests')),
    )


def mobile_tests_content():
    """buildsys/mobile_build.py's device tests: the Android engine and unit
    executables (android-tests) and the iOS script-test archive (ios-tests)."""
    mobile = set(sources.mobile_sources('android'))
    blocks = _groups('GLOB2_ANDROID_ENGINE_TESTS',
                     list(_entries(registry.SUPPORT + registry.ENGINE_SUPPORT + registry.portable_engine_tests())),
                     'android-tests glob2-engine-tests: its registry entries.')
    blocks += _groups('GLOB2_ANDROID_UNIT_TESTS',
                      list(_entries(registry.SUPPORT + registry.UNIT_STUBS + registry.portable_unit_tests())),
                      'android-tests glob2-unit-tests: its registry entries.')
    reused, again = [], []
    for path, options in _entries(registry.UNIT_PRODUCTION_SOURCES):
        if path in mobile and not options.get('defines') and not options.get('cxxflags'):
            reused.append(path)
        else:
            again.append((path, options))
    blocks.append(_list('GLOB2_ANDROID_UNIT_REUSED_SOURCES', reused,
                        'Production sources whose mobile objects the Android unit tests link.'))
    blocks += _groups('GLOB2_ANDROID_UNIT_PRODUCTION', again,
                      'Production sources the Android unit tests compile again with the test flags.')
    blocks += _groups('GLOB2_WEB_TESTS', list(_entries(registry.web_test_entries())),
                      'web-tests script-tests.js: its registry entries.')
    blocks += _groups('GLOB2_IOS_SCRIPT_TESTS',
                      list(_entries(registry.SUPPORT + registry.ENGINE_SUPPORT + registry.scripting_entries()
                                    + ['#src/online/ApiErrorTextTest.cpp'])),
                      'ios-tests libglob2-script-tests.a: its registry entries.')
    return blocks


def tests_content():
    client = {name for name in (*sources.CLIENT_SOURCES, *NATIVE_CLIENT_SOURCES)
              if name != ENTRY_POINT and name.endswith('.cpp')}
    binaries = test_binaries()
    blocks = ['set(GLOB2_TEST_BINARIES ' + ' '.join(key for key, *_ in binaries) + ')\n']
    # Production sources: plain client sources reuse the client
    # object; the rest compile again with the test flags, once for every binary
    # (their 'unit-' objects are shared).
    compiled = {}
    for key, target, entries, production, modules, everything, aliases in binaries:
        reused, again = [], []
        for path, options in _entries(production):
            name = path[len('src/'):] if path.startswith('src/') else None
            if name in client and not options.get('defines') and not options.get('cxxflags'):
                if options.get('require'):
                    raise SystemExit(f'{path}: a reused client object cannot carry requirements')
                reused.append(path)
            else:
                compiled.setdefault(path, options)
                again.append(path)
        blocks.append(f'set(GLOB2_{key}_TARGET {target})\n'
                      f'set(GLOB2_{key}_ALL_CLIENT_OBJECTS {"ON" if everything else "OFF"})\n'
                      f'set(GLOB2_{key}_MODULES {" ".join(modules)})\n'
                      f'set(GLOB2_{key}_ALIASES {" ".join(aliases)})\n')
        blocks += _groups('GLOB2_' + key, list(_entries(entries)), f'{target}: the registry entries it compiles.')
        blocks.append(_list(f'GLOB2_{key}_REUSED_CLIENT_SOURCES', reused,
                            f'Production sources whose client objects {target} links.'))
        blocks.append(_list(f'GLOB2_{key}_PRODUCTION_SOURCES', again,
                            f'Production sources {target} links compiled again (GLOB2_TEST_PRODUCTION).'))
    blocks += _groups('GLOB2_TEST_PRODUCTION', list(compiled.items()),
                      'Production sources compiled again with the test flags, shared by the binaries.')
    blocks += mobile_tests_content()
    blocks.append(_list('GLOB2_TEST_PROGRAMS', [target for target, *_ in registry.PROGRAMS],
                        'test/tests.py PROGRAMS: compiled with the test flags, linked with every client object.'))
    for target, source, alias, group in registry.PROGRAMS:
        blocks.append(f'set(GLOB2_TEST_PROGRAM_{target}_SOURCE {registry.source_path(source)})\n'
                      f'set(GLOB2_TEST_PROGRAM_{target}_ALIAS {alias})\n'
                      f'set(GLOB2_TEST_PROGRAM_{target}_GROUP {group})\n'
                      f'set(GLOB2_TEST_PROGRAM_{target}_REQUIRE '
                      f'{" ".join(sorted(registry.PROGRAM_REQUIRES.get(target, ())))})\n')
    blocks.append(_list('GLOB2_TEST_PROGRAM_SIDECARS',
                        [target for target, source, *_ in registry.PROGRAMS
                         if source.endswith('TranslationPreviewHarness.cpp')],
                        'Programs that get a <binary>.provenance.json sidecar.'))
    blocks.append(_list('GLOB2_TEST_LEGACY_ALIASES',
                        [f'{old}={new}' for old, new in registry.LEGACY_ALIASES.items()],
                        'test/tests.py LEGACY_ALIASES as alias=binary.'))
    blocks.append(_list('GLOB2_HARNESS_PROGRAMS', [program.target for program in harness_programs.HARNESS_PROGRAMS],
                        'buildsys/harness_programs.py HARNESS_PROGRAMS: compiled with the client flags.'))
    for program in harness_programs.HARNESS_PROGRAMS:
        prefix = 'GLOB2_HARNESS_' + program.target
        kind = program.link if isinstance(program.link, str) else 'sources'
        blocks.append(f'set({prefix}_SOURCE {program.source.lstrip("#")})\n'
                      f'set({prefix}_ALIAS {program.alias})\n'
                      f'set({prefix}_LINK {kind})\n'
                      f'set({prefix}_CXXFLAGS {" ".join(program.cxxflags)})\n'
                      f'set({prefix}_SHARED {" ".join(program.shared)})\n'
                      f'set({prefix}_REQUIRE {program.require or ""})\n'
                      f'set({prefix}_PROFILE {program.profile or ""})\n'
                      f'set({prefix}_TRUST {"ON" if program.trust else "OFF"})\n')
        if kind == 'sources':
            blocks.append(_list(prefix + '_LINK_SOURCES', ['src/' + name for name in program.link],
                                'Client sources whose objects the program links.'))
    blocks.append(_list('GLOB2_HARNESS_SHARED_OBJECTS', list(harness_programs.SHARED_OBJECTS),
                        'buildsys/harness_programs.py SHARED_OBJECTS.'))
    for name, (_, source) in harness_programs.SHARED_OBJECTS.items():
        blocks.append(f'set(GLOB2_HARNESS_SHARED_{name}_SOURCE {source.lstrip("#")})\n')
    return ('# Generated by buildsys/export_cmake_sources.py from test/tests.py and\n'
            '# buildsys/harness_programs.py; do not edit. Regenerate after changing either.\n\n'
            + '\n'.join(blocks))


def outputs(directory=DIRECTORY):
    directory = Path(directory)
    return {directory / 'sources.cmake': content(), directory / 'tests.cmake': tests_content()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='exit 1 when a committed file is stale')
    parser.add_argument('--directory', type=Path, default=DIRECTORY,
                        help='where to write or check the files (default: cmake/)')
    args = parser.parse_args(argv)
    stale = []
    for path, text in outputs(args.directory).items():
        if (path.read_text() if path.exists() else None) == text:
            continue
        if args.check:
            stale.append(path)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    for path in stale:
        print(f'{path} is stale; run python3 buildsys/export_cmake_sources.py', file=sys.stderr)
    return 1 if stale else 0


if __name__ == '__main__':
    raise SystemExit(main())
