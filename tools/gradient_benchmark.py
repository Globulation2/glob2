#!/usr/bin/env python3
"""Build/run opt-in paired terrain gradient benchmarks; no timing gates in CI.

The explicit baseline source tree must contain the historical kernels from
3266c8e51. Its general bucket function is adapted into the same executable with
only its name, registry extent and counter hooks changed. Both solvers share the
compiler, flags, queue implementation and timer code. Independent heap oracles
check results. Shared queues measure warm reuse; repetition -1 is cold.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = ('classic', 'road', 'ice', 'sparse', 'network', 'dense', 'isolated')
CASE_INTEGER_KEYS = {'size', 'width', 'height', 'swim', 'registry', 'travel', 'cap'}
CASE_STRING_KEYS = {'pattern', 'costs', 'seeds', 'mode'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def replace_exact(text, old, new, *, count=1):
    """Fail closed when a frozen baseline no longer matches the known adapter.

    In particular, a missing counter hook must not silently produce plausible
    timings with incomplete work counts. These are textual adapters for one
    historical implementation, not general C++ source transformations.
    """
    actual = text.count(old)
    if actual != count:
        raise ValueError(
            f'Unsupported baseline: expected {count} occurrences of {old!r}, '
            f'found {actual}; use the source captured from 3266c8e51.'
        )
    return text.replace(old, new)


def baseline_header(source):
    text = source.read_text()
    marker = 'template<class TerrainAt>'
    try:
        start = text.index(marker)
        end = text.index('\n' + marker, start + len(marker))
    except ValueError as error:
        raise ValueError(
            f'Unsupported baseline kernel in {source}; use 3266c8e51.'
        ) from error
    text = text[start:end]
    replacements = (
        (marker, 'template<class TerrainAt, std::size_t N>', 1),
        ('expandTerrainBucket', 'expandBaselineTerrainBucket', 1),
        ('TERRAIN_COUNT', 'N', 7),
        ('const TerrainEntryCosts &costs', 'const std::array<EntrySteps,N> &costs', 1),
        ('if (!count) return;',
         'if (!count) return; GLOB2_GRADIENT_BENCH_EVENT(occupied,1); '
         'GLOB2_GRADIENT_BENCH_EVENT(popped,count);', 1),
        ('target.reserveExtra(',
         'GLOB2_GRADIENT_BENCH_EVENT(chunkReserves,1); target.reserveExtra(', 1),
        ('if (gradient[i] != GRADIENT_AT_GOAL - cur) continue;',
         'if (gradient[i] != GRADIENT_AT_GOAL - cur) '
         '{ GLOB2_GRADIENT_BENCH_EVENT(stale,1); continue; }', 1),
        ('pending += newSize-target.size;',
         'GLOB2_GRADIENT_BENCH_EVENT(relaxations,newSize-target.size); '
         'pending += newSize-target.size;', 1),
    )
    for old, new, count in replacements:
        text = replace_exact(text, old, new, count=count)
    return (
        '// Generated frozen comparison adapter; do not edit.\n'
        'namespace gradient_kernel {\n' + text + '\n}\n'
    )


def baseline_travel_header(source):
    text = source.read_text()
    if text.count('namespace field') != 1:
        raise ValueError(f'Unsupported baseline travel namespace in {source}.')
    text = text[text.index('namespace field'):]
    replacements = (
        ('namespace field', 'namespace baseline_field'),
        ('const auto [cost,index]=queue.top();queue.pop();',
         'const auto [cost,index]=queue.top();queue.pop(); '
         'GLOB2_GRADIENT_BENCH_EVENT(popped,1);'),
        ('if(cost!=costs[index]) continue;',
         'if(cost!=costs[index]) '
         '{ GLOB2_GRADIENT_BENCH_EVENT(stale,1); continue; }'),
        ('costs[next]=candidate;queue.emplace(candidate,next);',
         'costs[next]=candidate;queue.emplace(candidate,next); '
         'GLOB2_GRADIENT_BENCH_EVENT(relaxations,1);'),
    )
    for old, new in replacements:
        text = replace_exact(text, old, new)
    return text


def cases(suite):
    # Exhaustive combinations stay opt-in. Balance each requested axis without
    # creating the full Cartesian product of every independent dimension.
    sizes = {
        'full': (32, 64, 128, 256, 512),
        'representative': (32, 128, 512),
        'smoke': (32,),
    }[suite]
    for size, pattern, swim in itertools.product(sizes, PATTERNS, range(7)):
        yield dict(size=size, pattern=pattern, swim=swim, registry=7,
                   costs='equivalent', seeds='single', mode='terrain')
    for size, swim in itertools.product(sizes, range(7)):
        yield dict(size=size, pattern='classic', swim=swim, registry=7,
                   costs='equivalent', seeds='single', mode='dispatch')
    for size, pattern, travel in itertools.product(sizes, PATTERNS, (1, 2, 3)):
        yield dict(size=size, pattern=pattern, swim=3, registry=7,
                   costs='equivalent', seeds='dense', mode='strategic', travel=travel)
    for registry, costs, seeds, mode in itertools.product(
        (8, 32, 64), ('equivalent', 'distinct'),
        ('single', 'dense', 'deferred'), ('terrain', 'plane'),
    ):
        yield dict(size=128 if suite != 'smoke' else 32, pattern='dense', swim=3,
                   registry=registry, costs=costs, seeds=seeds, mode=mode)
    for width, height, swim in itertools.product((1, 2, 3, 17), (1, 3, 31), range(7)):
        yield dict(width=width, height=height, pattern='dense', swim=swim,
                   registry=8, costs='equivalent', seeds='deferred', cap=127,
                   mode='terrain')
    for cap, seeds in itertools.product(
        (0, 1, 42, 63, 64, 65, 250, 701), ('single', 'dense', 'deferred'),
    ):
        yield dict(size=32, pattern='dense', swim=6, registry=64,
                   costs='distinct', seeds=seeds, cap=cap, mode='terrain')


def load_cases(custom_cases, suite):
    if not custom_cases:
        return list(cases(suite))
    matrix = []
    for encoded in custom_cases:
        case = json.loads(encoded)
        if not isinstance(case, dict):
            raise ValueError('--case must be a JSON object.')
        unknown = case.keys() - CASE_INTEGER_KEYS - CASE_STRING_KEYS
        if unknown:
            raise ValueError(f'Unsupported --case keys: {", ".join(sorted(unknown))}.')
        for key, value in case.items():
            expected = int if key in CASE_INTEGER_KEYS else str
            if type(value) is not expected:
                raise ValueError(f'--case {key} must be a {expected.__name__}.')
        matrix.append(case)
    return matrix


def prepare_output(directory):
    evidence = ('manifest.json', 'samples.jsonl', 'summary.json')
    existing = [name for name in evidence if (directory / name).exists()]
    if existing:
        raise FileExistsError(
            f'Refusing to overwrite benchmark evidence in {directory}: '
            f'{", ".join(existing)}. Choose a fresh output directory.'
        )
    directory.mkdir(parents=True, exist_ok=True)


def copy_sources(args):
    """Freeze all compilation inputs before either implementation is measured."""
    generated = args.output / 'baseline_gradient.h'
    generated.write_text(baseline_header(args.baseline_dir / 'field/TerrainGradient.h'))
    (args.output / 'baseline_travel.h').write_text(
        baseline_travel_header(args.baseline_dir / 'field/TerrainTravel.h')
    )
    snapshot = args.output / 'source'
    (snapshot / 'field').mkdir(parents=True, exist_ok=True)
    (snapshot / 'map').mkdir(exist_ok=True)
    for header in (args.candidate_dir / 'field').glob('*.h'):
        shutil.copyfile(header, snapshot / 'field' / header.name)
    for name in ('TerrainProperties.h', 'TerrainType.h'):
        shutil.copyfile(args.candidate_dir / 'map' / name, snapshot / 'map' / name)
    if (args.candidate_dir / 'map/TerrainRegistry.h').exists():
        for name in ('TerrainRegistry.h', 'TerrainRegistry.cpp', 'TerrainPresentation.h'):
            shutil.copyfile(args.candidate_dir / 'map' / name, snapshot / 'map' / name)
        (snapshot / 'online').mkdir(exist_ok=True)
        for name in ('Sha256.h', 'Sha256.cpp'):
            shutil.copyfile(args.candidate_dir / 'online' / name, snapshot / 'online' / name)
    shutil.copyfile(ROOT / 'tools/gradient_benchmark.cpp', snapshot / 'gradient_benchmark.cpp')
    shutil.copyfile(__file__, snapshot / 'gradient_benchmark.py')
    if args.bucket_count:
        bucket = snapshot / 'field/GradientBucket.h'
        text = bucket.read_text()
        start = text.index('static constexpr unsigned COUNT =')
        end = text.index('();', start) + 3
        text = text[:start] + f'static constexpr unsigned COUNT = {args.bucket_count};' + text[end:]
        bucket.write_text(text)
    return snapshot


def build_benchmark(args, snapshot):
    binary = args.output / 'gradient-benchmark'
    command = [
        args.compiler, *shlex.split(args.flags),
        '-I' + str(snapshot), '-I' + str(args.output),
        str(snapshot / 'gradient_benchmark.cpp'), '-o', str(binary),
    ]
    if (snapshot / 'map/TerrainRegistry.cpp').exists():
        command.extend([str(snapshot / 'map/TerrainRegistry.cpp'), str(snapshot / 'online/Sha256.cpp'),
                        '-I' + str(ROOT / 'third_party/nlohmann-json/include')])
    if args.scalar:
        command.insert(1, '-DGLOB2_GRADIENT_SCALAR')
    if args.instrumented:
        command.insert(1, '-DGLOB2_GRADIENT_BENCH_COUNTERS')
    subprocess.run(command, check=True)
    return binary, command


def write_manifest(args, snapshot, binary, command):
    manifest = dict(
        command=command,
        compiler=subprocess.check_output([args.compiler, '--version'], text=True),
        platform=platform.platform(),
        cpu=args.cpu,
        instrumented=args.instrumented,
        baseline_source_sha256=sha(args.baseline_dir / 'field/TerrainGradient.h'),
        adapter_sha256=sha(args.output / 'baseline_gradient.h'),
        baseline_travel_source_sha256=sha(args.baseline_dir / 'field/TerrainTravel.h'),
        baseline_travel_adapter_sha256=sha(args.output / 'baseline_travel.h'),
        benchmark_sha256=sha(snapshot / 'gradient_benchmark.cpp'),
        binary_sha256=sha(binary),
        candidate_headers={
            str(path.relative_to(snapshot)): sha(path)
            for path in sorted(snapshot.rglob('*.h'))
        },
        args=vars(args),
    )
    (args.output / 'manifest.json').write_text(
        json.dumps(manifest, indent=2, default=str) + '\n'
    )


def run_cases(args, binary, matrix):
    summary = []
    with (args.output / 'samples.jsonl').open('w') as raw:
        for index, case in enumerate(matrix):
            for layout in ('shared', 'separate'):
                command = [str(binary), '--repeats', str(args.repeats), '--layout', layout]
                for key, value in case.items():
                    command.extend(['--' + key, str(value)])
                if args.cpu is not None:
                    command = ['taskset', '-c', str(args.cpu), *command]
                try:
                    result = subprocess.run(command, check=True, text=True, capture_output=True)
                except subprocess.CalledProcessError as error:
                    # Preserve rows already produced by a failing case, while
                    # showing the actual oracle/validation diagnostic to users.
                    raw.write(error.stdout or '')
                    raw.flush()
                    raise RuntimeError(
                        f'Benchmark failed (exit {error.returncode}): {shlex.join(command)}\n'
                        f'{(error.stderr or "No diagnostic output.").strip()}'
                    ) from error
                raw.write(result.stdout)
                raw.flush()
                rows = [json.loads(line) for line in result.stdout.splitlines()]
                warm = [row for row in rows if row['repetition'] >= 0]
                before = statistics.median(row['cpu_ms'] for row in warm if not row['candidate'])
                after = statistics.median(row['cpu_ms'] for row in warm if row['candidate'])
                summary.append(dict(
                    case=case, layout=layout, baseline_cpu_ms=before,
                    candidate_cpu_ms=after, ratio=after / before if before else None,
                    command=command,
                ))
            if index % 20 == 0:
                print(f'{index + 1}/{len(matrix)} cases', file=sys.stderr, flush=True)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-dir', required=True, type=Path,
                        help='frozen baseline source root containing field/TerrainGradient.h')
    parser.add_argument('--candidate-dir', type=Path, default=ROOT / 'src')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--suite', choices=('smoke', 'representative', 'full'), default='representative')
    parser.add_argument('--repeats', type=int, default=11)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--flags', default='-O3 -DNDEBUG -std=c++20')
    parser.add_argument('--scalar', action='store_true')
    parser.add_argument('--instrumented', action='store_true',
                        help='collect counters/allocations; never use these timings for performance claims')
    parser.add_argument('--cpu', type=int, help='pin benchmark subprocess to a CPU')
    parser.add_argument('--bucket-count', type=int, choices=(64, 256),
                        help='standalone future-cost experiment: override queue ring in copied headers only')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--case', action='append', help='JSON case object; repeat for a custom matrix')
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive.')
    if args.cpu is not None and args.cpu < 0:
        parser.error('--cpu must be non-negative.')
    return args


def main():
    args = parse_args()
    matrix = load_cases(args.case, args.suite)
    try:
        prepare_output(args.output)
        snapshot = copy_sources(args)
        binary, command = build_benchmark(args, snapshot)
        write_manifest(args, snapshot, binary, command)
        if not args.build_only:
            run_cases(args, binary, matrix)
    except (FileExistsError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
