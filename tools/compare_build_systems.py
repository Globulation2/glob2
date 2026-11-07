#!/usr/bin/env python3
"""Compare the SCons and experimental CMake native Linux development builds.

Builds glob2 with both build systems into separate parity directories (SCons
through tools/dev_build.py, CMake through the dev preset), each with the same
extra -ffile-prefix-map so objects do not embed the checkout path, then reports:

1. per source, the compile flags of both compile_commands.json files: the -I
   search order (first occurrence of each directory) and the multiset of every
   other flag, after normalising the checkout, build directories, compiler path,
   output and dependency-file arguments;
2. per source compiled by both, whether the object files are byte-identical,
   and whether they are identical once debug sections are stripped.

    python3 tools/compare_build_systems.py               # build both, then compare
    python3 tools/compare_build_systems.py --no-build    # compare existing parity builds
    python3 tools/compare_build_systems.py --json report.json

The script never hides differences; it exits 1 when any flag set differs.
"""
import argparse
import collections
import hashlib
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
SCONS_DIRECTORY = ROOT / 'build/parity/scons'
CMAKE_DIRECTORY = ROOT / 'build/parity/cmake'
# Maps the checkout away in both builds. With "<root>/=" rather than "<root>=.",
# the absolute source paths CMake passes and the relative ones SCons passes give
# the same __FILE__ text.
PREFIX_MAP = '-ffile-prefix-map=' + str(ROOT) + '/='
# Arguments that name this compilation's outputs rather than how it compiles.
OUTPUT_OPTIONS = {'-o', '-MF', '-MT', '-MQ'}
DEPENDENCY_FLAGS = {'-MD', '-MMD', '-c'}


def run(command, **kwargs):
    print('+ ' + shlex.join(str(word) for word in command), flush=True)
    subprocess.run(command, check=True, cwd=ROOT, **kwargs)


def build_scons(jobs):
    # CC/CXX carry the extra flag so it reaches C and C++ objects alike; SCons'
    # ccache wrapping and every other flag stay as tools/dev_build.py sets them.
    command = [sys.executable, 'tools/dev_build.py', '--build=' + str(SCONS_DIRECTORY.relative_to(ROOT)),
               'CC=gcc ' + PREFIX_MAP, 'CXX=g++ ' + PREFIX_MAP]
    if jobs:
        command.append('-j' + str(jobs))
    run(command + [str(SCONS_DIRECTORY.relative_to(ROOT) / 'src/glob2'),
                   str(SCONS_DIRECTORY.relative_to(ROOT) / 'compile_commands.json')])


def build_cmake(jobs):
    run(['cmake', '--preset', 'dev', '-B', str(CMAKE_DIRECTORY),
         '-DGLOB2_EXTRA_COMPILE_FLAGS=' + PREFIX_MAP])
    run(['cmake', '--build', str(CMAKE_DIRECTORY), '--target', 'glob2', '--', '-j' + str(jobs or 8)])


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
        while words and Path(words[0]).name in ('ccache', 'env'):
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


def database(directory):
    path = Path(directory) / 'compile_commands.json'
    if not path.exists():
        raise SystemExit(f'{path} is missing; build first or drop --no-build')
    result = {}
    for entry in json.loads(path.read_text()):
        source = Path(os.path.normpath(os.path.join(entry['directory'], entry['file'])))
        output = entry.get('output')
        if output:
            output = Path(os.path.normpath(os.path.join(entry['directory'], output)))
        result[source.relative_to(ROOT).as_posix()] = (entry, output)
    return result


def digest(path, strip=False):
    if not strip:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory() as directory:
        stripped = Path(directory) / 'object.o'
        subprocess.run(['objcopy', '--strip-debug', str(path), str(stripped)], check=True)
        return hashlib.sha256(stripped.read_bytes()).hexdigest()


def compare():
    scons, cmake = database(SCONS_DIRECTORY), database(CMAKE_DIRECTORY)
    # The SCons database also registers tests and tools that the parity build
    # does not compile; compare the sources whose objects it actually built.
    scons = {source: value for source, value in scons.items() if value[1] and value[1].exists()}
    scons_normal, cmake_normal = Normaliser(SCONS_DIRECTORY), Normaliser(CMAKE_DIRECTORY)
    report = {'only_scons': sorted(set(scons) - set(cmake)), 'only_cmake': sorted(set(cmake) - set(scons)),
              'flag_differences': [], 'compilers': set(), 'objects': collections.Counter(),
              'object_differences': []}
    for source in sorted(set(scons) & set(cmake)):
        a_compiler, a_includes, a_flags = scons_normal.entry(scons[source][0])
        b_compiler, b_includes, b_flags = cmake_normal.entry(cmake[source][0])
        report['compilers'].add((a_compiler, b_compiler))
        difference = {}
        if a_compiler != b_compiler:
            difference['compiler'] = [a_compiler, b_compiler]
        if a_includes != b_includes:
            difference['includes'] = {'scons': a_includes, 'cmake': b_includes}
        if a_flags != b_flags:
            difference['flags_only_scons'] = sorted((a_flags - b_flags).elements())
            difference['flags_only_cmake'] = sorted((b_flags - a_flags).elements())
        if difference:
            report['flag_differences'].append({'source': source, **difference})
        a_object, b_object = scons[source][1], cmake[source][1]
        if not (a_object and b_object and a_object.exists() and b_object.exists()):
            report['objects']['missing'] += 1
            continue
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
    compared = sum(report['objects'].values())
    lines = [f"Sources compiled by both builds: {compared}",
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
        examples = [item['source'] for item in report['object_differences'] if item['kind'] == kind][:5]
        lines.append(f'  objects differing in {kind}: {count} (e.g. {", ".join(examples)})')
    return '\n'.join(lines)


def time_noop(jobs):
    """Seconds for a no-change build of each ordinary development build."""
    environment = dict(os.environ)
    results = {}
    for name, command in (('scons', [sys.executable, 'tools/dev_build.py']),
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
    parser.add_argument('--jobs', type=int, default=8, help='parallel compile jobs for each build')
    parser.add_argument('--json', type=Path, help='also write the full report here')
    parser.add_argument('--time-noop', action='store_true',
                        help='also time a no-change rebuild of both ordinary development builds')
    args = parser.parse_args()
    if not args.no_build:
        build_scons(args.jobs)
        build_cmake(args.jobs)
    report = compare()
    if args.time_noop:
        report['noop_seconds'] = time_noop(args.jobs)
    print(summary(report))
    if 'noop_seconds' in report:
        print('No-op build seconds: ' + ', '.join(f'{k} {v}' for k, v in report['noop_seconds'].items()))
    if args.json:
        args.json.write_text(json.dumps(report, indent=2, default=list) + '\n')
    return 1 if report['flag_differences'] or report['only_scons'] or report['only_cmake'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
