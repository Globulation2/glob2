#!/usr/bin/env python3
"""Paired A/B performance measurement of two builds, as a PR-ready table.

    python3 tools/perf_ab.py --baseline master --candidate build/linux/client/perf \\
        --pairs 7 --name gradient/terrain-hazard \\
        -- {build}/test/glob2-engine-tests -ts=TerrainHazardBenchmark '-tc=*idle decisions*'

The command after `--` runs once per build and pair; `{build}` becomes the build
directory. Runs alternate baseline/candidate (the first of each pair alternates
too) so load drift affects both equally. Each run's metric is its wall time by
default, or the first number matched by `--metric REGEX` in its output (group 1),
for benchmarks that print their own timing. The report gives medians, the change
and the tier, ready to paste into a pull request:

    | bench | metric | base (sha) | cand (sha) | Δ% | pairs | tier | host |

`--baseline master` uses the prebuilt baseline of the current branch's merge base
(tools/dev_baseline.py), built for the same tier as the candidate; a directory
uses that build. Tiers come from each build's identity.json: perf-screen (-O2,
three pairs is enough for direction) and perf-confirm (opt=3, seven pairs, the
number to quote). On Linux, `--pin` runs each process under
test/run_with_benchmark_cpuset.py.
"""
from __future__ import annotations

import argparse
import json
import platform
import re
import shlex
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def identity(build: Path) -> dict:
    try:
        return json.loads((build / 'identity.json').read_text())
    except (OSError, ValueError):
        return {}


def tier(build: Path) -> str:
    found = identity(build)
    if found.get('mode') == 'perf':
        return 'perf-confirm O3' if found.get('opt') == 3 else 'perf-screen O2'
    return found.get('mode', 'unknown')


def revision(build: Path) -> str:
    """The commit a build directory belongs to (its checkout's HEAD)."""
    checkout = build
    while checkout != checkout.parent and not (checkout / '.git').exists():
        checkout = checkout.parent
    result = subprocess.run(['git', '-C', str(checkout), 'rev-parse', '--short=10', 'HEAD'], capture_output=True, text=True)
    return result.stdout.strip() or '?'


def resolve_baseline(spec: str, candidate: Path) -> Path:
    if spec != 'master':
        return Path(spec).resolve()
    variant = 'perf' if identity(candidate).get('mode') == 'perf' else 'dev'
    if variant == 'perf' and identity(candidate).get('opt') == 3:
        variant = 'perf-o3'  # python3 tools/dev_baseline.py build --variants perf-o3
    result = subprocess.run([sys.executable, str(ROOT / 'tools/dev_baseline.py'), 'path', '--merge-base', 'HEAD',
                             '--variant', variant], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(result.stderr.strip() or result.stdout.strip())
    return Path(result.stdout.strip())


def measure(command: list[str], build: Path, metric: re.Pattern | None, pin: bool) -> float:
    argv = [part.replace('{build}', str(build)) for part in command]
    if pin and platform.system() == 'Linux':
        argv = [sys.executable, str(ROOT / 'test/run_with_benchmark_cpuset.py'), '--', *argv]
    started = time.perf_counter()
    result = subprocess.run(argv, capture_output=True, text=True)
    elapsed = time.perf_counter() - started
    if result.returncode != 0:
        raise SystemExit(f'benchmark failed ({result.returncode}) for {build}:\n{result.stdout[-2000:]}{result.stderr[-2000:]}')
    if metric is None:
        return elapsed
    found = metric.search(result.stdout + result.stderr)
    if not found:
        raise SystemExit(f'--metric {metric.pattern!r} matched nothing in the output for {build}')
    return float(found.group(1))


def table(name: str, metric_name: str, base: Path, cand: Path, base_values: list[float], cand_values: list[float]) -> str:
    base_median, cand_median = statistics.median(base_values), statistics.median(cand_values)
    delta = 100 * (cand_median - base_median) / base_median if base_median else float('nan')
    tiers = {tier(base), tier(cand)}
    host = f'{platform.system().lower()} {platform.machine()}'
    return ('| bench | metric | base (sha) | cand (sha) | Δ% | pairs | tier | host |\n'
            '| --- | --- | ---: | ---: | ---: | ---: | --- | --- |\n'
            f'| {name} | {metric_name} | {base_median:.4g} ({revision(base)}) | {cand_median:.4g} ({revision(cand)}) '
            f'| {delta:+.1f} | {len(base_values)} | {" / ".join(sorted(tiers))} | {host} |\n')


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if '--' not in argv:
        raise SystemExit('give the benchmark command after --; see --help')
    split = argv.index('--')
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--baseline', required=True, help='a build directory, or master for the prebuilt baseline')
    parser.add_argument('--candidate', required=True, type=Path, help='the candidate build directory')
    parser.add_argument('--pairs', type=int, default=None, help='default 3 for perf-screen, 7 otherwise')
    parser.add_argument('--warmup', type=int, default=1, help='unmeasured runs of each build first')
    parser.add_argument('--metric', help='regex whose group 1 is the metric; default wall seconds')
    parser.add_argument('--metric-name', default=None)
    parser.add_argument('--name', default='benchmark')
    parser.add_argument('--pin', action='store_true', help='Linux: run under the benchmark cpuset')
    parser.add_argument('--json', type=Path, help='also write raw samples here')
    args = parser.parse_args(argv[:split])
    command = argv[split + 1:]
    candidate = args.candidate.resolve()
    baseline = resolve_baseline(args.baseline, candidate)
    pairs = args.pairs or (3 if tier(candidate) == 'perf-screen O2' else 7)
    metric = re.compile(args.metric) if args.metric else None
    lease = None
    if args.baseline == 'master':
        # A shared lease on the baseline worktree keeps dev_baseline.py gc away from it.
        sys.path.insert(0, str(ROOT / 'scons'))
        import dev_store
        worktree = next(parent for parent in baseline.parents if (parent / '.baseline-complete').exists())
        lease = dev_store.Lease(worktree)
    for _ in range(args.warmup):
        measure(command, baseline, metric, args.pin)
        measure(command, candidate, metric, args.pin)
    samples = {'baseline': [], 'candidate': []}
    for pair in range(pairs):
        order = [('baseline', baseline), ('candidate', candidate)]
        for label, build in (order if pair % 2 == 0 else order[::-1]):
            samples[label].append(measure(command, build, metric, args.pin))
        print(f'pair {pair + 1}/{pairs}: base {samples["baseline"][-1]:.4g} cand {samples["candidate"][-1]:.4g}',
              file=sys.stderr)
    if lease is not None:
        lease.close()
    if args.json:
        args.json.write_text(json.dumps({'command': shlex.join(command), 'baseline': str(baseline),
                                         'candidate': str(candidate), 'samples': samples}, indent=2) + '\n')
    print(table(args.name, args.metric_name or ('wall s' if metric is None else 'metric'),
                baseline, candidate, samples['baseline'], samples['candidate']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
