#!/usr/bin/env python3
"""Paired CPU/wall/RSS comparison of a resource refactor using frozen game windows.

Uses the same execution and fixture hashing as benchmark_parallel_compute.py.
Run with idle compilers and identical affinity for both binaries. Confidence
intervals describe repeat noise, not coverage of unmeasured maps or platforms.
"""
import argparse
from contextlib import contextmanager
import json
import hashlib
import math
import os
import platform
import random
import shutil
import statistics
import subprocess
import sys
import time
import benchmark_parallel_compute
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


# These labels retain the historical JSON keys while making their boundaries explicit.
METRIC_DESCRIPTIONS = {
    'simulation_cpu_s': 'All-thread process CPU after prepareRun/beginSession through finishSession (summary/teardown) and final gradient-pipeline drain; excludes requested final save.',
    'simulation_wall_s': 'Engine run_ns wall interval: also includes prepareRun/beginSession and diagnostic finish; not the exact measured CPU interval.',
    'setup_cpu_s': 'Headless game setup/loading CPU before prepareRun/beginSession; excludes those session-start calls and earlier process startup.',
    'save_cpu_s': 'CPU around the requested final save; near zero when no final save is requested.',
    'cpu_s': 'Whole child-process user plus system CPU from wait4, including all threads, startup and teardown.',
    'wall_s': 'Whole child-process wall time from launch through wait4 completion.',
    'peak_rss_bytes': 'Whole child-process peak resident memory; includes setup and does not isolate resource/cache allocations.',
}
RUNNER_INPUTS = (Path(__file__).resolve(), Path(benchmark_parallel_compute.__file__).resolve())
CATALOG_SUFFIXES = {'.json', '.txt', '.js', '.sgsl'}
HOST_ACTIVITY_NOTE = ('Approximate host activity across all CPUs, not attribution to a process or proof of interference. '
    'Busy excludes idle/iowait and includes steal; guest time is already included in user/nice. '
    'Other busy subtracts whole-child wait4 CPU from host busy, includes runner/kernel activity, and can be negative '
    'because /proc/stat uses coarse jiffies and its snapshot boundaries differ from wait4. '
    'Average cores use the monotonic interval between snapshots, not engine wall time. No automatic performance gate applies.')


def parse_cpu_busy_jiffies(contents):
    """Read Linux CPU accounting without double-counting guest/guest_nice."""
    counters = {}
    for line in contents.splitlines():
        fields = line.split()
        if not fields or not (fields[0] == 'cpu' or
                              fields[0].startswith('cpu') and fields[0][3:].isdigit()):
            continue
        name = fields[0]
        values = [int(value) for value in fields[1:]]
        if name in counters or len(values) < 4 or any(value < 0 for value in values):
            raise ValueError(f'Invalid CPU accounting row: {name}')
        # user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice.
        counters[name] = sum(value for index, value in enumerate(values[:8]) if index not in (3, 4))
    if 'cpu' not in counters or len(counters) < 2:
        raise ValueError('Missing aggregate or per-CPU accounting rows')
    return counters


def host_cpu_snapshot():
    if platform.system() != 'Linux':
        return {'status': 'unavailable', 'reason': 'Linux /proc/stat unavailable on this platform'}
    try:
        contents = Path('/proc/stat').read_text()
        captured_ns = time.monotonic_ns()
        return {'status': 'available', 'monotonic_ns': captured_ns,
                'clock_ticks_per_second': os.sysconf('SC_CLK_TCK'),
                'busy_jiffies': parse_cpu_busy_jiffies(contents)}
    except (OSError, ValueError) as error:
        return {'status': 'unavailable', 'reason': str(error)}


def host_cpu_activity(before, after, child_cpu_s):
    """Estimate other host activity; retain negative estimates rather than hide skew."""
    result = {'note': HOST_ACTIVITY_NOTE}
    if before.get('status') != 'available' or after.get('status') != 'available':
        return dict(result, status='unavailable', reason='One or both CPU snapshots unavailable')
    ticks = before['clock_ticks_per_second']
    elapsed_s = (after['monotonic_ns'] - before['monotonic_ns']) / 1e9
    first, last = before['busy_jiffies'], after['busy_jiffies']
    if (ticks <= 0 or ticks != after['clock_ticks_per_second'] or elapsed_s <= 0 or
            first.keys() != last.keys() or not math.isfinite(child_cpu_s) or child_cpu_s < 0):
        return dict(result, status='unavailable', reason='CPU topology, clock or interval changed/invalid')
    deltas = {name: (last[name] - value) / ticks for name, value in first.items()}
    if any(value < 0 for value in deltas.values()):
        return dict(result, status='unavailable', reason='CPU busy counter decreased')
    other = deltas['cpu'] - child_cpu_s
    return dict(result, status='available', snapshot_elapsed_s=elapsed_s,
                host_busy_cpu_s=deltas['cpu'],
                per_cpu_busy_s={name: value for name, value in deltas.items() if name != 'cpu'},
                child_cpu_s=child_cpu_s, approximate_other_busy_cpu_s=other,
                approximate_other_busy_average_cores=other / elapsed_s)


def frequency_snapshot():
    """Untimed boundary samples; neither field integrates the measured window."""
    if platform.system() != 'Linux':
        return {'status': 'unavailable on this platform'}
    cpus = {}
    for cpu in sorted(os.sched_getaffinity(0)):
        values = {}
        for name in ('scaling_cur_freq', 'cpuinfo_avg_freq', 'scaling_governor'):
            path = Path(f'/sys/devices/system/cpu/cpu{cpu}/cpufreq') / name
            try:
                values[name] = path.read_text().strip()
            except OSError as error:
                values[name] = {'unavailable': str(error)}
        cpus[str(cpu)] = values
    return {'cpus': cpus, 'frequency_unit': 'kHz',
            'scope': 'boundary snapshot only; scaling frequency is a requested/driver value; cpuinfo average is hardware/driver-defined, not the measured engine interval'}


def frequency_capability():
    """Probe permissions outside timing; never label a probe as engine evidence."""
    result = {'effective_frequency': 'unavailable',
              'reason': 'This runner does not collect interval cycles/task-clock; boundary snapshots are not effective frequency.'}
    if platform.system() != 'Linux' or not shutil.which('perf'):
        result['counter_probe'] = {'status': 'unavailable', 'reason': 'Linux perf unavailable'}
        return result
    command = ['perf', 'stat', '-x', ';', '-e', 'cycles,task-clock', '--', 'true']
    try:
        probe = subprocess.run(command, text=True, capture_output=True, timeout=10)
        result['counter_probe'] = {'command': command, 'exit_code': probe.returncode,
                                   'stdout': probe.stdout, 'stderr': probe.stderr,
                                   'status': 'available' if probe.returncode == 0 else 'unavailable'}
    except (OSError, subprocess.TimeoutExpired) as error:
        result['counter_probe'] = {'command': command, 'status': 'unavailable', 'reason': str(error)}
    return result


def capture_inputs(binaries, roots, manifest_path, fixture_paths):
    """Hash inputs outside timed runs; additions/removals of catalog files count too."""
    return dict(
        binaries={name: {'path': str(path), 'sha256': digest(path)} for name, path in binaries.items()},
        manifest={'path': str(manifest_path), 'sha256': digest(manifest_path)},
        fixtures={str(path): digest(path) for path in sorted(fixture_paths)},
        # ldd's printed addresses vary with ASLR; identity is resolved path + bytes.
        runtime_libraries={name: {'sha256': runtime_libraries(path).get('sha256', {}),
                                 'inspection': 'ldd' if platform.system() == 'Linux' else 'unavailable'}
                           for name, path in binaries.items()},
        data_roots={name: {'path': str(root), 'files': {
            str(path.relative_to(root)): digest(path)
            for path in sorted((root / 'data').rglob('*'))
            if path.is_file() and path.suffix in CATALOG_SUFFIXES}}
            for name, root in roots.items()},
        runner_inputs={str(path): digest(path) for path in RUNNER_INPUTS},
        python={'executable': str(Path(sys.executable).resolve()),
                'sha256': digest(Path(sys.executable).resolve()), 'version': sys.version},
        affinity=sorted(os.sched_getaffinity(0)),
        environment_sha256=hashlib.sha256(json.dumps(dict(os.environ), sort_keys=True).encode()).hexdigest(),
    )


def changed_inputs(before, after, prefix=''):
    """Return precise changed/missing paths without duplicating all hashes in errors."""
    if isinstance(before, dict) and isinstance(after, dict):
        result = []
        for key in sorted(before.keys() | after.keys()):
            path = f'{prefix}/{key}'
            if key not in before or key not in after:
                result.append(path)
            else:
                result.extend(changed_inputs(before[key], after[key], path))
        return result
    return [] if before == after else [prefix]


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


@contextmanager
def stable_campaign(output, capture):
    """Never publish acceptance after changed inputs, including failed campaigns.

    Python exceptions and KeyboardInterrupt execute the final audit. Uncatchable
    termination can leave state=running, which is explicitly incomplete evidence.
    """
    report = {'state': 'starting', 'inputs_unchanged': False}
    report_path = output / 'input-verification.json'
    write_json(report_path, report)
    try:
        before = capture()
    except Exception as error:
        report.update(state='invalid', verification_error={'type': type(error).__name__, 'message': str(error)})
        write_json(report_path, report)
        raise
    report.update(state='running', before=before)
    write_json(report_path, report)
    failure = None
    try:
        yield before
    except BaseException as error:
        failure = {'type': type(error).__name__, 'message': str(error)}
        raise
    finally:
        report['campaign_error'] = failure
        try:
            after = capture()
            report['after'] = after
            report['changed_inputs'] = changed_inputs(before, after)
            report['inputs_unchanged'] = not report['changed_inputs']
        except Exception as error:
            report['verification_error'] = {'type': type(error).__name__, 'message': str(error)}
            report['inputs_unchanged'] = False
        report['state'] = ('failed' if failure else 'verified') if report['inputs_unchanged'] else 'invalid'
        write_json(report_path, report)
        if not report['inputs_unchanged'] and failure is None:
            raise RuntimeError(f'Benchmark inputs changed or could not be verified; see {report_path}')


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
    p.add_argument('--repeats', type=int, default=8)
    p.add_argument('--report-only', action='store_true',
                   help='Report historical threshold diagnostics without failing on regression or inconclusive intervals; execution and integrity failures still fail')
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
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    if not manifest.get('scenarios'):
        p.error('manifest has no scenarios')
    scenario_ids = set()
    for scenario in manifest['scenarios']:
        name = scenario.get('id')
        if not isinstance(name, str) or not name or name in ('.', '..') or '/' in name or '\\' in name or name in scenario_ids:
            p.error('each scenario requires a unique single-directory id')
        scenario_ids.add(name)
        run_args = scenario.get('args')
        if not isinstance(run_args, list) or not all(isinstance(value, str) for value in run_args):
            p.error('each scenario requires a list of string arguments')
        if run_args.count('--load-game') != 1 or run_args.count('--ticks') != 1 or 'start_tick' not in scenario:
            p.error('each scenario requires an absolute frozen save, start_tick and tick limit')
        try:
            ticks = int(run_args[run_args.index('--ticks') + 1])
            save = run_args[run_args.index('--load-game') + 1]
            start_tick = scenario['start_tick']
            if type(start_tick) is not int or start_tick < 0 or ticks <= start_tick:
                raise ValueError('nonpositive window')
        except (IndexError, ValueError):
            p.error('each scenario requires nonnegative integer start_tick and a larger integer tick limit')
        if not Path(save).is_absolute() or save not in scenario.get('fixture_sha256', {}):
            p.error('each load-game path must be absolute and hash-verified')
        for path, expected in scenario.get('fixture_sha256', {}).items():
            if digest(path) != expected:
                raise ValueError(f'fixture changed: {path}')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    fixture_paths = {Path(path) for scenario in manifest['scenarios']
                     for path in scenario.get('fixture_sha256', {})}
    capture = lambda: capture_inputs(binaries, roots, args.manifest.resolve(), fixture_paths)
    with stable_campaign(out, capture) as inputs:
        if inputs['manifest']['sha256'] != hashlib.sha256(manifest_bytes).hexdigest():
            raise RuntimeError('Manifest changed between parsing and campaign capture')
        for scenario in manifest['scenarios']:
            for path, expected in scenario.get('fixture_sha256', {}).items():
                if inputs['fixtures'][str(Path(path))] != expected:
                    raise RuntimeError(f'Fixture changed before campaign capture: {path}')
        write_json(out/'metadata.json', dict(
            platform=platform.platform(), **{key: value for key, value in inputs.items() if key != 'manifest'},
            runtime_environment={key: os.environ.get(key) for key in
                ('LD_LIBRARY_PATH', 'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER', 'GLOB2_PERF_DISABLE')},
            instrumentation={'scope_collection': 'enabled by structured headless startup',
                'parent_disable_flag': 'Headless isolateEnvironment clears GLOB2_PERF_DISABLE; the recorded parent value is not effective.',
                'scope_output': 'enabled only when --telemetry team-timeline is requested'},
            measurement_descriptions=METRIC_DESCRIPTIONS,
            frequency_evidence=frequency_capability(),
            host_cpu_activity_description=HOST_ACTIVITY_NOTE,
            warmup={'discarded_pairs_per_scenario': 1, 'simulation_warmup_ticks': 0,
                'meaning': 'Fresh-process warmup for OS caches; every measured run starts from its save, not a warmed in-process simulation.'},
            ordering={'kind': 'alternating paired order', 'balanced': args.repeats % 2 == 0},
            catalog_hash_scope=sorted(CATALOG_SUFFIXES),
            arguments={key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
            manifest_input=inputs['manifest'], manifest=manifest, repeats=args.repeats))
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
                        frequency_before = frequency_snapshot()
                        host_before = host_cpu_snapshot()
                        row = dict(scenario=scenario['id'], repeat=repeat, variant=variant,
                            **execute(binaries[variant], run_args, out/scenario['id']/str(repeat)/variant, cwd=roots[variant]))
                        host_after = host_cpu_snapshot()
                        row['host_cpu_before'] = host_before
                        row['host_cpu_after'] = host_after
                        row['host_cpu_activity'] = host_cpu_activity(host_before, host_after, row['cpu_s'])
                        row['system_load_before'] = load_before
                        row['system_load_after'] = os.getloadavg()
                        row['frequency_before'] = frequency_before
                        row['frequency_after'] = frequency_snapshot()
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
                  report_only=args.report_only,
                  performance_gate='regression' if credible_regression else 'within_limits' if within_limits else 'inconclusive')
    (out/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['aggregate_cpu']), result['performance_gate'])
    # Report-only changes only threshold enforcement, after the complete input audit.
    return 0 if args.report_only else 1 if credible_regression else 0 if within_limits else 2

if __name__ == '__main__':
    raise SystemExit(main())
