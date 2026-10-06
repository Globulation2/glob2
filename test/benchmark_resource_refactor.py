#!/usr/bin/env python3
"""Paired CPU/wall/RSS comparison of a resource refactor using frozen game windows.

Uses the same execution and fixture hashing as benchmark_parallel_compute.py.
Run with idle compilers and identical affinity for both binaries. Confidence
intervals describe repeat noise, not coverage of unmeasured maps or platforms.
"""
import argparse
import json
import math
import platform
import random
import statistics
import subprocess
from pathlib import Path
from benchmark_parallel_compute import ROOT, digest, execute


def runtime_libraries(binary):
    """Record the actual loader resolution, including explicit local overrides."""
    if platform.system() != 'Linux':
        return {'status': 'not inspected on this platform'}
    output = subprocess.check_output(['ldd', str(binary)], text=True)
    libraries = {}
    for line in output.splitlines():
        fields = line.split()
        path = fields[2] if len(fields) > 2 and fields[1] == '=>' else fields[0] if fields else ''
        if path.startswith('/') and Path(path).is_file():
            libraries[path] = digest(path)
    return {'resolution': output, 'sha256': libraries}


def interval(values):
    rng = random.Random(714031)
    means = sorted(statistics.mean(rng.choices(values, k=len(values))) for _ in range(4000))
    return [math.exp(means[100]), math.exp(means[3899])]


def aggregate_interval(scenario_logs):
    """Equal-weight fixed scenarios, independently resampling paired repeats.

    Scenario-major execution gives repeat indices no shared timing-block meaning.
    Sort each stratum so harmless input permutations also preserve the seeded
    Monte Carlo result exactly.
    """
    strata = sorted(sorted(values) for values in scenario_logs)
    if not strata or any(not values for values in strata):
        raise ValueError('aggregate requires nonempty scenario samples')
    rng = random.Random(714031)
    means = sorted(statistics.mean(statistics.mean(rng.choices(values, k=len(values)))
                                   for values in strata) for _ in range(4000))
    return dict(ratio=math.exp(statistics.mean(statistics.mean(values) for values in strata)),
                ci95=[math.exp(means[100]), math.exp(means[3899])])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('before', type=Path)
    p.add_argument('after', type=Path)
    p.add_argument('manifest', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--before-root', type=Path, required=True, help='Archived baseline source/data root; baseline must not read candidate catalogs')
    p.add_argument('--after-root', type=Path, default=ROOT, help='Candidate source/data root')
    p.add_argument('--repeats', type=int, default=7)
    args = p.parse_args()
    if args.repeats < 3:
        p.error('at least three paired repeats required')
    binaries = {'baseline': args.before.resolve(), 'candidate': args.after.resolve()}
    roots = {'baseline': args.before_root.resolve(), 'candidate': args.after_root.resolve()}
    for root in roots.values():
        if not (root/'data').is_dir():
            p.error(f'missing data directory: {root}')
    if roots['baseline'] == roots['candidate']:
        p.error('baseline and candidate require distinct frozen data roots')
    manifest = json.loads(args.manifest.read_text())
    if not manifest.get('scenarios'):
        p.error('manifest has no scenarios')
    for scenario in manifest['scenarios']:
        if '--load-game' not in scenario['args'] or '--ticks' not in scenario['args'] or 'start_tick' not in scenario:
            p.error('each scenario requires an absolute frozen save, start_tick and tick limit')
        save = scenario['args'][scenario['args'].index('--load-game') + 1]
        if not Path(save).is_absolute() or save not in scenario.get('fixture_sha256', {}):
            p.error('each load-game path must be absolute and hash-verified')
        for path, expected in scenario.get('fixture_sha256', {}).items():
            if digest(path) != expected:
                raise ValueError(f'fixture changed: {path}')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    import os
    (out/'metadata.json').write_text(json.dumps(dict(
        platform=platform.platform(), affinity=sorted(os.sched_getaffinity(0)),
        binaries={k: {'path':str(v),'sha256':digest(v)} for k,v in binaries.items()},
        runtime_libraries={k: runtime_libraries(v) for k,v in binaries.items()},
        runtime_environment={key: os.environ.get(key) for key in
            ('LD_LIBRARY_PATH', 'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER', 'GLOB2_PERF_DISABLE')},
        data_roots={k: {'path': str(root), 'files': {str(path.relative_to(root)): digest(path)
            for path in sorted((root/'data').rglob('*')) if path.is_file() and path.suffix in ('.json', '.txt', '.js')}}
            for k, root in roots.items()},
        manifest=manifest, repeats=args.repeats), indent=2)+'\n')
    pairs = {}
    with (out/'measurements.jsonl').open('w') as stream:
        for scenario in manifest['scenarios']:
            samples = []
            for repeat in range(-1, args.repeats):
                rows = {}
                order = ['baseline', 'candidate'] if repeat % 2 else ['candidate', 'baseline']
                for variant in order:
                    run_args = list(scenario['args']) + ['--benchmark-warmup', '0']
                    load_before = os.getloadavg()
                    row = dict(scenario=scenario['id'], repeat=repeat, variant=variant,
                        **execute(binaries[variant], run_args, out/scenario['id']/str(repeat)/variant, cwd=roots[variant]))
                    row['system_load_before'] = load_before
                    row['system_load_after'] = os.getloadavg()
                    result = row['result']
                    expected_ticks = int(run_args[run_args.index('--ticks') + 1]) - scenario['start_tick']
                    if result.get('benchmark_measured_ticks') != expected_ticks:
                        raise RuntimeError(f'{scenario["id"]} ended before its full measurement window; classify and shorten both windows')
                    row['simulation_cpu_s'] = result['benchmark_run_cpu_ns'] / 1e9
                    row['setup_cpu_s'] = result['benchmark_setup_cpu_ns'] / 1e9
                    row['save_cpu_s'] = result['benchmark_save_cpu_ns'] / 1e9
                    row['simulation_wall_s'] = result['run_ns'] / 1e9
                    rows[variant] = row
                    stream.write(json.dumps(row)+'\n'); stream.flush()
                    print(f"{scenario['id']} {repeat} {variant}: cpu={row['cpu_s']:.4f}s wall={row['wall_s']:.4f}s", flush=True)
                if rows['baseline']['result']['ticks'] != rows['candidate']['result']['ticks']:
                    raise RuntimeError('unequal simulation windows: classify early termination before timing comparison')
                if repeat >= 0: samples.append(rows)
            pairs[scenario['id']] = samples
    summary = {}
    for name, samples in pairs.items():
        summary[name] = {}
        for metric in ('simulation_cpu_s', 'simulation_wall_s', 'cpu_s', 'wall_s', 'peak_rss_bytes'):
            logs = [math.log(row['candidate'][metric]/row['baseline'][metric]) for row in samples]
            summary[name][metric] = dict(ratio=math.exp(statistics.mean(logs)), ci95=interval(logs),
                baseline_median=statistics.median(row['baseline'][metric] for row in samples),
                candidate_median=statistics.median(row['candidate'][metric] for row in samples))
    # Each scenario receives equal weight; all early/middle/late controls are included.
    aggregate_logs = [[math.log(row['candidate']['simulation_cpu_s']/row['baseline']['simulation_cpu_s'])
                       for row in samples] for samples in pairs.values()]
    aggregate = aggregate_interval(aggregate_logs)
    credible_regression = aggregate['ci95'][0] > 1.02 or any(s['simulation_cpu_s']['ci95'][0] > 1.05 for s in summary.values())
    within_limits = aggregate['ci95'][1] <= 1.02 and all(s['simulation_cpu_s']['ci95'][1] <= 1.05 for s in summary.values())
    result = dict(scenarios=summary, aggregate_cpu=aggregate,
                  performance_gate='regression' if credible_regression else 'within_limits' if within_limits else 'inconclusive')
    (out/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['aggregate_cpu']), result['performance_gate'])
    # Inconclusive evidence is not acceptance: repeat with more samples.
    return 1 if credible_regression else 0 if within_limits else 2

if __name__ == '__main__':
    raise SystemExit(main())
