#!/usr/bin/env python3
"""Compare the SCons and CMake native Linux builds of one tier.

Builds glob2, the four test binaries, the test/tests.py programs and the
buildsys/harness_programs.py harnesses with both build systems into separate
parity directories (build/parity/<tier>/{scons,cmake}; SCons through
tools/dev_build.py, CMake through the tier's preset), each with the same extra
-ffile-prefix-map so objects do not embed the checkout path, then reports:

1. per compilation, the compile flags of both compile_commands.json files: the
   -I search order (first occurrence of each directory) and the multiset of
   every other flag, after normalising the checkout, build directories,
   compiler path, output and dependency-file arguments. A source compiled more
   than once (test support in both test binaries, a production source compiled
   again with test defines) must have the same multiset of compilations in both;
2. per pair of matching compilations, whether the object files are
   byte-identical, and whether they are identical once debug sections are
   stripped.

    python3 tools/compare_build_systems.py               # build both, then compare
    python3 tools/compare_build_systems.py --tier perf   # the perf=1 tier (dev, core, perf, perf-o3, release)
    python3 tools/compare_build_systems.py --no-build    # compare existing parity builds
    python3 tools/compare_build_systems.py --glob2-only  # only glob2 (the stage 1-2 scope)
    python3 tools/compare_build_systems.py --json report.json --summary summary.md

The script never hides differences; it exits 1 when any flag set or the set of
compiled sources differs. Object hash differences are reported, not failed on.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
# Each tier's SCons arguments (with the tools/dev_build.py defaults it should not
# add spelled out) and the CMake preset that builds the same identity.
TIERS = {
    'dev': ([], 'dev'),
    'core': (['compile_profile=core'], 'core'),
    'perf': (['perf=1'], 'perf'),
    'perf-o3': (['perf=1', 'opt=3'], 'perf-o3'),
    'release': (['release=1', 'dev_fast=0', 'linker=default'], 'release'),
    'relay': (['role=relay', 'release=1', 'dev_fast=0', 'linker=default'], 'relay'),
}
SCONS_DIRECTORY = ROOT / 'build/parity/dev/scons'
CMAKE_DIRECTORY = ROOT / 'build/parity/dev/cmake'
TIER = 'dev'


def select_tier(tier):
    global SCONS_DIRECTORY, CMAKE_DIRECTORY, TIER
    TIER = tier
    SCONS_DIRECTORY = ROOT / 'build/parity' / tier / 'scons'
    CMAKE_DIRECTORY = ROOT / 'build/parity' / tier / 'cmake'
# Maps the checkout away in both builds. With "<root>/=" rather than "<root>=.",
# the absolute source paths CMake passes and the relative ones SCons passes give
# the same __FILE__ text.
PREFIX_MAP = '-ffile-prefix-map=' + str(ROOT) + '/='
# Arguments that name this compilation's outputs rather than how it compiles.
OUTPUT_OPTIONS = {'-o', '-MF', '-MT', '-MQ'}
DEPENDENCY_FLAGS = {'-MD', '-MMD', '-c'}


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def programs():
    """(SCons aliases, CMake targets) of every test binary, test program and harness."""
    sys.path.insert(0, str(ROOT / 'buildsys'))
    registry = _load('glob2_test_registry', ROOT / 'test/tests.py')
    harnesses = _load('glob2_harness_programs', ROOT / 'buildsys/harness_programs.py').HARNESS_PROGRAMS
    aliases = ['tests'] + [alias for _, _, alias, _ in registry.PROGRAMS] + [program.alias for program in harnesses]
    targets = (['glob2-engine-tests', 'glob2-unit-tests', 'glob2-map-tests', 'glob2-sim-tests']
               + [target for target, *_ in registry.PROGRAMS]
               + [program.target for program in harnesses])
    return list(dict.fromkeys(aliases)), targets


def run(command, **kwargs):
    print('+ ' + shlex.join(str(word) for word in command), flush=True)
    subprocess.run(command, check=True, cwd=ROOT, **kwargs)


def build_scons(jobs, everything):
    # CC/CXX carry the extra flag so it reaches C and C++ objects alike; SCons'
    # ccache wrapping and every other flag stay as tools/dev_build.py sets them.
    command = [sys.executable, 'tools/dev_build.py', '--generator=scons', '--build=' + str(SCONS_DIRECTORY.relative_to(ROOT)),
               *TIERS[TIER][0],
               'CC=' + os.environ.get('CC', 'gcc') + ' ' + PREFIX_MAP,
               'CXX=' + os.environ.get('CXX', 'g++') + ' ' + PREFIX_MAP]
    if jobs:
        command.append('-j' + str(jobs))
    targets = [str(SCONS_DIRECTORY.relative_to(ROOT) / 'src/glob2')]
    if TIER == 'relay':
        targets = ['relay']
    elif TIER == 'core' and everything:
        # compile_profile=core leaves out the harnesses of the optional groups.
        targets += ['tests']
    elif everything:
        targets += programs()[0]
    run(command + targets + [str(SCONS_DIRECTORY.relative_to(ROOT) / 'compile_commands.json')])


def build_cmake(jobs, everything):
    # ccache without CCACHE_BASEDIR: the base directory rewrites CMake's absolute
    # source paths relative to the build directory (__FILE__ becomes
    # "../../../src/..."), which no prefix map can match to SCons' root-relative
    # paths. Without it, hits need the same checkout path, as on CI runners.
    run(['cmake', '--preset', TIERS[TIER][1], '-B', str(CMAKE_DIRECTORY), '-DGLOB2_CCACHE_BASEDIR=',
         '-DGLOB2_EXTRA_COMPILE_FLAGS=' + PREFIX_MAP])
    if TIER == 'relay':
        targets = ['relay']
    elif TIER == 'core':
        targets = ['glob2'] + (['tests'] if everything else [])
    else:
        targets = ['glob2'] + (programs()[1] if everything else [])
    run(['cmake', '--build', str(CMAKE_DIRECTORY), '--target', *targets, '--', '-j' + str(jobs or 8)])


class Normaliser:
    def __init__(self, build_directory):
        self.build = Path(build_directory).resolve()

    def path(self, value, directory):
        path = Path(value)
        if not path.is_absolute():
            path = Path(directory) / path
        text = os.path.normpath(str(path))
        for prefix, name in ((str(self.build), '<build>'), (str(ROOT), '<root>')):
            if text == prefix or text.startswith(prefix + os.sep):
                return name + text[len(prefix):]
        return text

    def text(self, value):
        return value.replace(str(self.build), '<build>').replace(str(ROOT), '<root>')

    def entry(self, entry):
        """(compiler, include order, other flags) of one compile_commands entry."""
        words = entry['arguments'] if 'arguments' in entry else shlex.split(entry['command'])
        directory = entry['directory']
        source = os.path.normpath(os.path.join(directory, entry['file']))
        while words and (Path(words[0]).name in ('ccache', 'env', 'cmake') or words[0] in ('-E', 'env')
                         or words[0].startswith(('--unset=', 'CCACHE_'))):
            words = words[1:]
        compiler = os.path.realpath(shutil.which(words[0]) or words[0])
        includes, flags = [], []
        index = 1
        while index < len(words):
            word = words[index]
            index += 1
            if word in OUTPUT_OPTIONS:
                index += 1
                continue
            if word in DEPENDENCY_FLAGS or os.path.normpath(os.path.join(directory, word)) == source:
                continue
            if word == '-I' and index < len(words):
                word = '-I' + words[index]
                index += 1
            if word.startswith('-I'):
                include = self.path(word[2:], directory)
                if include not in includes:
                    includes.append(include)
                continue
            flags.append(self.text(word))
        return compiler, includes, collections.Counter(flags)

    def variant(self, entry):
        """A hashable key: two compilations with the same key compile alike."""
        compiler, includes, flags = self.entry(entry)
        return compiler, tuple(includes), tuple(sorted(flags.elements()))


def database(directory):
    """source -> [(entry, object path)] of the compilations whose object exists."""
    path = Path(directory) / 'compile_commands.json'
    if not path.exists():
        raise SystemExit(f'{path} is missing; build first or drop --no-build')
    compilations = collections.defaultdict(list)
    for entry in json.loads(path.read_text()):
        source = Path(os.path.normpath(os.path.join(entry['directory'], entry['file'])))
        if Path(directory).resolve() in source.parents:
            continue  # configure probes and generated sources
        output = entry.get('output')
        if output:
            output = Path(os.path.normpath(os.path.join(entry['directory'], output)))
        # The SCons database registers every program's objects; CMake's every
        # target's. Compare what each parity build actually compiled.
        if output and output.exists():
            compilations[source.relative_to(ROOT).as_posix()].append((entry, output))
    return compilations


def digest(path, strip=False):
    if not strip:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory() as directory:
        stripped = Path(directory) / 'object.o'
        subprocess.run(['objcopy', '--strip-debug', str(path), str(stripped)], check=True)
        return hashlib.sha256(stripped.read_bytes()).hexdigest()


def _flag_difference(source, a, b, scons_normal, cmake_normal):
    a_compiler, a_includes, a_flags = scons_normal.entry(a)
    b_compiler, b_includes, b_flags = cmake_normal.entry(b)
    difference = {'source': source}
    if a_compiler != b_compiler:
        difference['compiler'] = [a_compiler, b_compiler]
    if a_includes != b_includes:
        difference['includes'] = {'scons': a_includes, 'cmake': b_includes}
    if a_flags != b_flags:
        difference['flags_only_scons'] = sorted((a_flags - b_flags).elements())
        difference['flags_only_cmake'] = sorted((b_flags - a_flags).elements())
    return difference


def compare():
    scons, cmake = database(SCONS_DIRECTORY), database(CMAKE_DIRECTORY)
    scons_normal, cmake_normal = Normaliser(SCONS_DIRECTORY), Normaliser(CMAKE_DIRECTORY)
    report = {'only_scons': sorted(set(scons) - set(cmake)), 'only_cmake': sorted(set(cmake) - set(scons)),
              'flag_differences': [], 'compilers': set(), 'objects': collections.Counter(),
              'object_differences': [], 'compilations': 0}
    for source in sorted(set(scons) & set(cmake)):
        groups = collections.defaultdict(lambda: ([], []))
        for side, (entries, normal) in enumerate(((scons[source], scons_normal), (cmake[source], cmake_normal))):
            for entry, output in entries:
                groups[normal.variant(entry)][side].append((entry, output))
        for variant, (a, b) in groups.items():
            report['compilers'].add(variant[0])
            report['compilations'] += max(len(a), len(b))
        unmatched = {variant: sides for variant, sides in groups.items() if len(sides[0]) != len(sides[1])}
        if unmatched:
            a = [entry for sides in unmatched.values() for entry, _ in sides[0]]
            b = [entry for sides in unmatched.values() for entry, _ in sides[1]]
            if len(a) == 1 and len(b) == 1:
                report['flag_differences'].append(_flag_difference(source, a[0], b[0], scons_normal, cmake_normal))
            else:
                report['flag_differences'].append({'source': source, 'compilations': [len(a), len(b)],
                                                   'note': 'differing compilations of a source built more than once'})
        for variant, (a, b) in groups.items():
            for (_, a_object), (_, b_object) in zip(a, b):
                if digest(a_object) == digest(b_object):
                    report['objects']['identical'] += 1
                elif digest(a_object, True) == digest(b_object, True):
                    report['objects']['identical_without_debug'] += 1
                    report['object_differences'].append({'source': source, 'kind': 'debug sections only'})
                else:
                    report['objects']['different'] += 1
                    report['object_differences'].append({'source': source, 'kind': 'code or data'})
    report['compilers'] = sorted(report['compilers'])
    report['objects'] = dict(report['objects'])
    return report


def summary(report):
    lines = [f"Compilations compared: {report['compilations']}",
             f"Sources only SCons compiled: {len(report['only_scons'])}",
             f"Sources only CMake compiled: {len(report['only_cmake'])}",
             f"Sources with differing compile flags: {len(report['flag_differences'])}",
             'Objects: ' + ', '.join(f'{key} {value}' for key, value in sorted(report['objects'].items()))]
    for name in ('only_scons', 'only_cmake'):
        for source in report[name]:
            lines.append(f'  {name}: {source}')
    for difference in report['flag_differences'][:20]:
        lines.append('  flags: ' + json.dumps(difference))
    if len(report['flag_differences']) > 20:
        lines.append(f"  ... {len(report['flag_differences']) - 20} more flag differences")
    kinds = collections.Counter(item['kind'] for item in report['object_differences'])
    for kind, count in sorted(kinds.items()):
        examples = sorted({item['source'] for item in report['object_differences'] if item['kind'] == kind})
        lines.append(f'  objects differing in {kind}: {count} (e.g. {", ".join(examples[:5])})')
    return '\n'.join(lines)


def markdown(report):
    """A GitHub step summary: failures first, object parity as information."""
    failed = report['flag_differences'] or report['only_scons'] or report['only_cmake']
    objects = report['objects']
    lines = [f'## Build-system equivalence (SCons vs CMake, {TIER} tier)', '',
             ('**Failed:** compile commands differ.' if failed else 'Compile commands match.'), '',
             '| Check | Result |', '| --- | --- |',
             f"| Compilations compared | {report['compilations']} |",
             f"| Sources only SCons compiled | {len(report['only_scons'])} |",
             f"| Sources only CMake compiled | {len(report['only_cmake'])} |",
             f"| Sources with differing flags | {len(report['flag_differences'])} |",
             f"| Identical objects | {objects.get('identical', 0)} |",
             f"| Objects identical without debug sections | {objects.get('identical_without_debug', 0)} |",
             f"| Objects differing in code or data | {objects.get('different', 0)} |", '']
    if report['object_differences']:
        lines += ['Object differences are reported, not failed on, until parity is exact:', '']
        for item in report['object_differences'][:50]:
            lines.append(f"- `{item['source']}`: {item['kind']}")
        if len(report['object_differences']) > 50:
            lines.append(f"- ... {len(report['object_differences']) - 50} more")
        lines.append('')
    if failed:
        lines += ['```', summary(report), '```', '']
    return '\n'.join(lines)


def time_noop(jobs):
    """Seconds for a no-change build of each ordinary development build."""
    environment = dict(os.environ)
    results = {}
    for name, command in (('scons', [sys.executable, 'tools/dev_build.py', '--generator=scons']),
                          ('cmake', [sys.executable, 'tools/dev_build.py', '--generator=cmake'])):
        if jobs:
            command.append('-j' + str(jobs))
        subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
        start = time.monotonic()
        subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
        results[name] = round(time.monotonic() - start, 2)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true', help='compare the existing parity builds')
    parser.add_argument('--tier', choices=sorted(TIERS), default='dev', help='the build tier to compare (default: dev)')
    parser.add_argument('--glob2-only', action='store_true',
                        help='build only glob2, not the test binaries, test programs and harnesses')
    parser.add_argument('--jobs', type=int, default=8, help='parallel compile jobs for each build')
    parser.add_argument('--json', type=Path, help='also write the full report here')
    parser.add_argument('--summary', type=Path, help='also append a Markdown summary here (e.g. $GITHUB_STEP_SUMMARY)')
    parser.add_argument('--time-noop', action='store_true',
                        help='also time a no-change rebuild of both ordinary development builds')
    args = parser.parse_args()
    select_tier(args.tier)
    if not args.no_build:
        build_scons(args.jobs, not args.glob2_only)
        build_cmake(args.jobs, not args.glob2_only)
    report = compare()
    if args.time_noop:
        report['noop_seconds'] = time_noop(args.jobs)
    print(summary(report))
    if 'noop_seconds' in report:
        print('No-op build seconds: ' + ', '.join(f'{k} {v}' for k, v in report['noop_seconds'].items()))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, default=list) + '\n')
    if args.summary:
        with args.summary.open('a') as output:
            output.write(markdown(report) + '\n')
    return 1 if report['flag_differences'] or report['only_scons'] or report['only_cmake'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
