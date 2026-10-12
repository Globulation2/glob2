#!/usr/bin/env python3
"""Compare development configurations in disposable source snapshots.

Example: python3 tools/benchmark_build.py --configuration normal:dev_fast=0 \
  --configuration 'fast:dev_fast=1 linker=auto' --scons-arg target=web
Reports are local review evidence, never repository documentation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scons'))
from build_layout import build_identity, default_directory


def interval_total(intervals):
    total = 0.0
    stop = None
    for start, end in sorted(intervals):
        total += max(0, end - max(start, stop if stop is not None else start))
        stop = max(end, stop if stop is not None else end)
    return total


def timings(text, events=()):
    phases = {}
    for phase in ('compile', 'link'):
        rows = [event for event in events if event.get('phase') == phase and 'start' in event]
        phases[phase + '_work_seconds'] = sum(row['end'] - row['start'] for row in rows)
        phases[phase + '_wall_seconds'] = interval_total([(row['start'], row['end']) for row in rows])
        phases[phase + '_actions'] = len(rows)
    for title, name in [('SConscript file execution', 'setup_wall_seconds'), ('command execution', 'commands_wall_seconds')]:
        match = re.search('Total ' + title + r' time: ([0-9.]+) seconds', text)
        phases[name] = float(match[1]) if match else None
    return phases


def cache_stats(environment):
    result = subprocess.run(['ccache', '--print-stats'], env=environment, capture_output=True, text=True, check=True)
    return {name: int(value) for name, value in (line.split() for line in result.stdout.splitlines())}


def compiler_metadata(database, environment):
    compilers = {}
    for entry in database:
        words = shlex.split(entry['command'])
        if words and Path(words[0]).name in ('ccache', 'ccache.exe'):
            words = words[1:]
        if not words or words[0] in compilers:
            continue
        binary = shutil.which(words[0], path=environment.get('PATH')) or words[0]
        path = Path(binary)
        version = subprocess.run([binary, '--version'], env=environment, capture_output=True, text=True, check=True).stdout
        compilers[words[0]] = {'binary': str(path), 'version': version,
                               'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    return compilers


def snapshot(destination):
    # Native harness registration reads Git provenance even when the requested
    # target only provisions dependencies. Retain a lightweight repository,
    # then overlay the candidate's working files without committing them.
    subprocess.run(['git', 'clone', '--shared', '--quiet', '--no-checkout',
                    str(ROOT), str(destination)], check=True)
    subprocess.run(['git', 'reset', '--mixed', 'HEAD'], cwd=destination,
                   stdout=subprocess.DEVNULL, check=True)
    files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    for name in filter(None, files):
        source, target = ROOT / name, destination / name
        if not source.exists() and not source.is_symlink():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_symlink():
            target.symlink_to(os.readlink(source))
        else:
            shutil.copy2(source, target)
    # Include new implementation files when profiling an uncommitted candidate.
    for name in subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT).decode().split('\0'):
        if name and name.startswith(('scons/', 'tools/', 'test/')):
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)


def run(work, arguments, environment, log):
    command = ['scons', '--debug=time', *arguments]
    memory = log.with_suffix('.memory')
    if sys.platform.startswith('linux') and Path('/usr/bin/time').exists():
        command = ['/usr/bin/time', '-v', '-o', str(memory), *command]
    environment = dict(environment, GLOB2_BUILD_TIMING_LOG=str(log.with_suffix('.actions.jsonl')))
    before = cache_stats(environment)
    started = time.monotonic()
    with log.open('w') as output:
        result = subprocess.run(command, cwd=work, env=environment, stdout=output, stderr=subprocess.STDOUT)
    wall = time.monotonic() - started
    text = log.read_text(errors='replace')
    peak = None
    if memory.exists():
        match = re.search(r'Maximum resident set size \(kbytes\): (\d+)', memory.read_text())
        peak = int(match[1]) * 1024 if match else None
    after = cache_stats(environment)
    record = {'command': command, 'exit_code': result.returncode, 'wall_seconds': wall,
              'peak_process_rss_bytes': peak, 'memory_coverage': 'GNU time process maximum; not aggregate concurrent RSS' if memory.exists() else 'unavailable on this host',
              'cache_delta': {key: value - before.get(key, 0) for key, value in after.items()}, **timings(text, [json.loads(line) for line in log.with_suffix('.actions.jsonl').read_text().splitlines()] if log.with_suffix('.actions.jsonl').exists() else [])}
    if result.returncode:
        raise RuntimeError('Build failed; see ' + str(log))
    record['dependency_events'] = [json.loads(line) for line in log.with_suffix('.actions.jsonl').read_text().splitlines() if json.loads(line).get('phase') == 'dependency'] if log.with_suffix('.actions.jsonl').exists() else []
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', action='append', help='LABEL:SCons options', default=[])
    parser.add_argument('--scons-arg', action='append', default=[])
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--source', default='src/ui/Glob2Style.cpp')
    parser.add_argument('--header', default='src/game/Game.h')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/dev-build/benchmark')
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error('repetitions must be positive')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name in (args.source, args.header):
        if Path(name).is_absolute() or '..' in Path(name).parts:
            parser.error('benchmark edit paths must stay within the source snapshot')
    if any(word.startswith('--build') for word in args.scons_arg):
        parser.error('benchmark manages its own isolated build directories')
    configs = args.configuration or ['normal:dev_fast=0', 'fast:dev_fast=1 linker=auto']
    report = {'dependency_overrides': {key: os.environ.get(key) for key in ('GLOB2_SDL3_PREFIX', 'GLOB2_RECORDING_PREFIX')}, 'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
              'platform': sys.platform, 'samples': [], 'edits': [args.source, args.header],
              'note': 'Compile/link work totals sum command durations and are not parallel wall times.'}
    report['dependency_input_hashes'] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in [*sorted((ROOT / 'scons').glob('*versions.json')), *sorted((ROOT / 'scons').glob('*vendored.json')),
                     *sorted((ROOT / 'scons/vcpkg-ports').glob('**/*.patch')), ROOT / 'browser/toolchain.json', ROOT / 'vcpkg.json']}
    report['external_manifests'] = {str(path): json.loads(path.read_text())
        for prefix in filter(None, report['dependency_overrides'].values())
        for path in Path(prefix).glob('*manifest.json')}
    frozen = tempfile.TemporaryDirectory(prefix='input-', dir=output)
    snapshot(Path(frozen.name))
    for configuration in configs:
        label, separator, flags = configuration.partition(':')
        if not separator or not re.fullmatch('[A-Za-z0-9_-]+', label):
            parser.error('configuration must be LABEL:options with a simple label')
        options = [*shlex.split(flags), *args.scons_arg]
        if not any(word.startswith('-j') for word in options):
            options.append('-j' + str(min(os.cpu_count() or 1, 8)))
        arguments = {word.split('=', 1)[0]: word.split('=', 1)[1] for word in options if '=' in word}
        identity = build_identity(arguments)
        for repetition in range(args.repetitions):
            logs = output / label / str(repetition)
            logs.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='source-', dir=logs) as temporary:
                work = Path(temporary)
                shutil.copytree(frozen.name, work, dirs_exist_ok=True, symlinks=True)
                environment = dict(os.environ, CCACHE='1', CCACHE_DIR=str(logs / 'ccache'), CCACHE_MAXSIZE='12G', GLOB2_DEV_HOME=str(logs / 'store'), GLOB2_DEV_MODE='shared')
                build = work / default_directory(identity)
                scenarios = [('dependencies', ['dev-dependencies']), ('uncached', []), ('warm-cache', []), ('no-change', []), ('source-edit', []), ('header-edit', [])]
                for scenario, targets in scenarios:
                    if scenario == 'warm-cache':
                        shutil.rmtree(build)
                    if scenario in ('source-edit', 'header-edit'):
                        path = work / (args.source if scenario == 'source-edit' else args.header)
                        # A content edit triggers SCons, unlike a timestamp touch.
                        with path.open('a') as edited:
                            edited.write('\nint glob2_build_benchmark_probe = 1;\n' if scenario == 'source-edit' else '\n[[maybe_unused]] static volatile int glob2_build_benchmark_header_probe = 1;\n')
                    sample = run(work, [*options, *targets], environment, logs / (scenario + '.log'))
                    sample.update(configuration=label, repetition=repetition, scenario=scenario,
                        object_bytes=sum(path.stat().st_size for path in build.rglob('*.o')) if build.exists() else 0)
                    report['samples'].append(sample)
                    (output / 'results.json').write_text(json.dumps(report, indent=2))
                database = json.loads((build / 'compile_commands.json').read_text())
                report.setdefault('build_inputs', []).append({'configuration': label, 'identity': identity,
                    'compile_database': database, 'compilers': compiler_metadata(database, environment),
                    'dependencies': [json.loads(path.read_text()) for path in (logs / 'store/dependencies').glob('*/prefix/.shared-install.json')]})
    frozen.cleanup()
    report['medians'] = {label: {scenario: statistics.median(sample['wall_seconds'] for sample in report['samples'] if sample['configuration'] == label and sample['scenario'] == scenario)
        for scenario in ('dependencies', 'uncached', 'warm-cache', 'no-change', 'source-edit', 'header-edit')} for label in {sample['configuration'] for sample in report['samples']}}
    (output / 'results.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report['medians'], indent=2))


if __name__ == '__main__':
    main()
