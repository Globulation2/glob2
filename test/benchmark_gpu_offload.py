"""Serialized paired GPU experiments from frozen retained-game manifests.

Config keys: control, variants [{id,binary,sha256,source_revision,build_flags,env}],
scenarios [{id,args,fixture_sha256,phase,group}], warmup_ticks. Source/build claims
must have separately retained build receipts; a binary hash alone cannot prove them.
No sealed qualification inputs are generated or consumed by this development tool.
"""
import argparse
import csv
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time
from gpu_offload_analysis import summarize, cpu_ceiling, aggregate_cpu

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def round_order(variants, round_index):
    """Balance positions in cyclic blocks and reverse each second block."""
    if round_index < 0:
        return list(variants)
    count = len(variants)
    shift = round_index % count
    order = list(variants[shift:]) + list(variants[:shift])
    if (round_index // count) % 2:
        order.reverse()
    return order


def validate(config):
    ids = [v['id'] for v in config['variants']]
    if len(ids) != len(set(ids)) or config['control'] not in ids:
        raise ValueError('distinct variants and an existing control required')
    if len({s['id'] for s in config['scenarios']}) != len(config['scenarios']):
        raise ValueError('distinct scenario ids required')
    for v in config['variants']:
        if sha(v['binary']) != v['sha256'] or not v['source_revision'] or not v['build_flags']:
            raise ValueError('unfrozen binary or missing build provenance: ' + v['id'])
    counts = {str(v.get('compute_threads', '8')) for v in config['variants']}
    if len(counts) != 1 or any(v != 'auto' and (not v.isdecimal() or int(v) <= 0) for v in counts):
        raise ValueError('paired variants require the same positive executor budget or auto')
    if any('cpu_affinity' in v and not v['cpu_affinity'] for v in config['variants']):
        raise ValueError('explicit CPU affinity must be nonempty')
    affinity_sets = {tuple(v.get('cpu_affinity', ())) for v in config['variants']}
    if len(affinity_sets) != 1:
        raise ValueError('paired variants require identical CPU affinity')
    for affinity in affinity_sets:
        if len(affinity) != len(set(affinity)) or any(type(c) is not int or c < 0 for c in affinity):
            raise ValueError('CPU affinity must contain distinct nonnegative logical CPU IDs')
    for s in config['scenarios']:
        if not s.get('fixture_sha256') or '--load-game' not in s['args'] or '--ticks' not in s['args']:
            raise ValueError('retained fixed-tick loaded scenario required')
        if any(k in s['args'] for k in ('--save', '--telemetry', '--write-replay', '--benchmark-warmup', '--output-dir')):
            raise ValueError('scenario args must exclude optional exports and runner options')
        for path, expected in s['fixture_sha256'].items():
            if sha(path) != expected: raise ValueError('fixture changed: ' + path)
    if config['warmup_ticks'] < 0: raise ValueError('nonnegative warmup required')
    if 'gpu_uuid' in config and (not isinstance(config['gpu_uuid'], str) or not config['gpu_uuid'].startswith('GPU-')):
        raise ValueError('GPU monitor identity must be an explicit NVIDIA UUID')
    if config.get('reservation') and not partition_snapshot(config['reservation'])['valid']:
        raise ValueError('exclusive benchmark partition validation failed')
    for path, digest in config.get('reservation_receipts', {}).items():
        if sha(path) != digest: raise ValueError('reservation receipt changed: ' + path)


def expand_cpus(text):
    cpus = set()
    for part in text.strip().split(','):
        if not part: continue
        bounds = list(map(int, part.split('-')))
        cpus.update(range(bounds[0], bounds[-1]+1))
    return cpus


def partition_snapshot(expected):
    result = {'valid': False, 'group': expected['group']}
    try:
        group = Path(expected['group'])
        result['files'] = {name: (group / name).read_text().strip() for name in
            ('cpuset.cpus.effective', 'cpuset.cpus.exclusive.effective', 'cpuset.cpus.partition', 'cpuset.mems.effective')}
        result['process_cgroup'] = Path('/proc/self/cgroup').read_text().strip()
        result['process_affinity'] = sorted(os.sched_getaffinity(0))
        desired = set(expected['cpus']); files = result['files']
        process_group = '0::/' + str(group.relative_to('/sys/fs/cgroup'))
        result['valid'] = (files['cpuset.cpus.partition'] == 'root' and
            expand_cpus(files['cpuset.cpus.effective']) == desired and
            expand_cpus(files['cpuset.cpus.exclusive.effective']) == desired and
            expand_cpus(files['cpuset.mems.effective']) == set(expected.get('memory_nodes', [0])) and
            set(result['process_affinity']) == desired and process_group in result['process_cgroup'].splitlines())
    except (OSError, ValueError, AttributeError) as error: result['error'] = str(error)
    return result


def gpu_snapshot(selected_uuid=None):
    """NVIDIA boundary observations only; never evidence of interval-wide isolation."""
    result = {'available': False, 'selected_uuid': selected_uuid,
              'note': 'Before/after observations exclude graphics contexts and cannot prove isolation throughout the interval.'}
    executable = shutil.which('nvidia-smi')
    if not executable:
        return result
    requests = {
        'devices': ('--query-gpu', ('index', 'uuid', 'name', 'driver_version', 'utilization.gpu',
                     'utilization.memory', 'memory.used', 'memory.total', 'temperature.gpu',
                     'power.draw', 'clocks.sm', 'clocks.mem')),
        'compute_processes': ('--query-compute-apps', ('gpu_uuid', 'pid', 'used_gpu_memory'))}
    try:
        for name, (option, fields) in requests.items():
            command = [executable, option+'='+','.join(fields), '--format=csv,noheader,nounits']
            completed = subprocess.run(command, capture_output=True, text=True, timeout=10, check=False)
            if completed.returncode:
                raise ValueError(f'{name} query failed: exit {completed.returncode}')
            rows = list(csv.reader(completed.stdout.splitlines(), skipinitialspace=True))
            if any(len(row) != len(fields) for row in rows):
                raise ValueError('malformed GPU observation')
            result[name] = [dict(zip(fields, (value.strip() for value in row))) for row in rows]
        result['available'] = True
        result['selected_device_found'] = selected_uuid is None or any(d['uuid'] == selected_uuid for d in result['devices'])
        result['selected_compute_processes'] = [p for p in result['compute_processes'] if p['gpu_uuid'] == selected_uuid]
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        result['error'] = str(error)
    return result


def resource_snapshot(benchmark_cpus=None, reservation=None, gpu_uuid=None):
    """Read-only inventory outside the timed window; command names exclude args."""
    if benchmark_cpus is None and hasattr(os, 'sched_getaffinity'):
        benchmark_cpus = sorted(os.sched_getaffinity(0))
    snapshot = {'monotonic_ns': time.monotonic_ns(), 'benchmark_cpus': benchmark_cpus,
                'gpu': gpu_snapshot(gpu_uuid)}
    if reservation: snapshot['reservation'] = partition_snapshot(reservation)
    load = Path('/proc/loadavg')
    if load.exists(): snapshot['loadavg'] = load.read_text().strip()
    listing = subprocess.run(['ps', '-eo', 'pid,comm,stat,pcpu', '--sort=-pcpu'],
                             capture_output=True, text=True, check=False)
    if listing.returncode:
        snapshot['inventory_available'] = False; return snapshot
    processes = []
    for line in listing.stdout.splitlines()[1:]:
        parts = line.split()
        if len(parts) != 4: continue
        pid, name, state, cpu = parts
        try: processes.append(dict(pid=int(pid), command=name, state=state, lifetime_cpu_percent=float(cpu)))
        except ValueError: continue
    compilers = [p for p in processes if p['command'] in ('cc1', 'cc1plus', 'gcc', 'g++', 'nvcc', 'ptxas') or
                 (p['command'].startswith('clang') and not p['command'].startswith('clangd'))]
    for compiler in compilers:
        try:
            compiler['allowed_cpus'] = sorted(os.sched_getaffinity(compiler['pid']))
            compiler['overlaps_benchmark_cpus'] = benchmark_cpus is None or bool(set(compiler['allowed_cpus']) & set(benchmark_cpus))
        except (AttributeError, OSError): compiler['overlaps_benchmark_cpus'] = True
    snapshot.update(inventory_available=True, compilers=compilers,
                    active_compiler_detected=any(p['state'].startswith(('R', 'D')) and p['overlaps_benchmark_cpus'] for p in compilers),
                    top_cpu_commands=processes[:10])
    return snapshot


def execute(variant, scenario, output, warmup, *, extra_args=(), reservation=None, gpu_uuid=None):
    output.mkdir(parents=True, exist_ok=False)
    threads = str(variant.get('compute_threads', '8'))
    command = [variant['binary'], 'game', 'run', *scenario['args'], *extra_args, '--compute-threads', threads,
               '--benchmark-warmup', str(warmup), '--output-dir', str(output)]
    affinity = variant.get('cpu_affinity')
    if affinity: command = ['taskset', '--cpu-list', ','.join(map(str, affinity)), *command]
    env = dict(os.environ, **variant.get('env', {}))
    resources_before = resource_snapshot(affinity, reservation, gpu_uuid)
    started = time.monotonic_ns()
    with (output / 'engine.log').open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            _, status, usage = os.wait4(process.pid, 0)
            process.returncode = os.waitstatus_to_exitcode(status)
        except BaseException:
            process.kill(); process.wait()
            raise
    row = dict(command=command, environment=variant.get('env', {}), exit_code=process.returncode,
               process_wall_ns=time.monotonic_ns() - started,
               process_cpu_ns=round((usage.ru_utime + usage.ru_stime) * 1e9),
               peak_rss_bytes=usage.ru_maxrss * (1 if platform.system() == 'Darwin' else 1024),
               valid=process.returncode == 0, errors=[],
               resources_before=resources_before, resources_after=resource_snapshot(affinity, reservation, gpu_uuid))
    row['resource_contaminated'] = any(not s.get('inventory_available', False) or s.get('active_compiler_detected', False) or
                                        (reservation is not None and not s.get('reservation', {}).get('valid', False)) for s in
                                        (row['resources_before'], row['resources_after']))
    if gpu_uuid:
        row['resource_contaminated'] |= any(not s['gpu'].get('available') or not s['gpu'].get('selected_device_found') or
            any(p['pid'] != str(process.pid) for p in s['gpu'].get('selected_compute_processes', ())) for s in
            (row['resources_before'], row['resources_after']))
    if process.returncode:
        row['errors'].append('process failure'); return row
    try:
        result = json.loads((output / 'result.json').read_text())
    except (OSError, ValueError) as error:
        row['valid'] = False; row['errors'].append('missing or malformed result: ' + str(error)); return row
    required = ('benchmark_measured_ticks', 'compute_threads', 'ticks', 'initialChecksum', 'finalChecksum',
                'benchmark_run_cpu_ns', 'benchmark_run_wall_ns', 'benchmark_publication_wait_ns', 'tick_p99_ns')
    if not isinstance(result, dict) or any(k not in result for k in required):
        row['valid'] = False; row['errors'].append('incomplete result metrics'); return row
    row['result'] = result
    row['cpu_ceiling'] = cpu_ceiling(result)
    if result['benchmark_measured_ticks'] <= 0 or (threads != 'auto' and result['compute_threads'] != int(threads)):
        row['errors'].append('empty window or executor fallback')
    if result['ticks'] != int(scenario['args'][scenario['args'].index('--ticks') + 1]):
        row['errors'].append('game ended before fixed tick endpoint')
    if variant.get('require_gpu'):
        start, end = result.get('benchmark_opencl_at_start'), result.get('benchmark_opencl_at_end')
        if not start or not end or any(k not in snap for snap in (start, end) for k in ('available', 'fields', 'dispatches')):
            row['errors'].append('GPU execution counters unavailable')
        elif (warmup and (not start['available'] or not result.get('benchmark_gradient_at_start', {}).get('ready_plan_mask'))) or end['fields'] <= start['fields'] or end['dispatches'] <= start['dispatches']:
            row['errors'].append('backend not warm or no actual measured GPU execution')
    row['valid'] = not row['errors']
    return row


def campaign_summary(rows, control, candidates, scenarios, *, rounds, stage):
    summary = dict(stage=stage, diagnostic_only=stage == 'diagnose', qualifying_evidence=False,
                   guard_completeness='scenario metrics and CPU aggregate only; final protocol and external gates required',
                   scenarios=summarize(rows, control, candidates, minimum_pairs=rounds,
                                       confirmation=stage == 'confirm'),
                   aggregate_cpu=aggregate_cpu(rows, control, candidates,
                         expected_scenarios=scenarios, expected_rounds=range(rounds), minimum_pairs=rounds))
    if stage == 'diagnose':
        for scenario in summary['scenarios'].values():
            for result in scenario.values():
                result['qualified'] = False
                result['scenario_gates_pass'] = False
                result['cpu_target_pass'] = False
                result['diagnostic_only'] = True
                result['admission_reason'] = 'short diagnostic stage cannot qualify a candidate'
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--lock', required=True, type=Path, help='one shared path for all builds and campaigns')
    parser.add_argument('--stage', choices=['diagnose', 'screen', 'confirm'], default='screen',
                        help='diagnose: two paired repeats, no admission; screen: five; confirm: ten')
    parser.add_argument('--cold', action='store_true', help='fresh process and no simulation warmup; retain separately')
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    validate(config)
    rounds = {'diagnose': 2, 'screen': 5, 'confirm': 10}[args.stage]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    with args.lock.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        metadata = dict(schema=1, configuration=config, config_sha256=sha(args.config),
                        platform=platform.platform(), rounds=rounds, cold=args.cold, stage=args.stage,
                        diagnostic_only=args.stage == 'diagnose',
                        runner_sha256=sha(__file__), analysis_sha256=sha(Path(__file__).with_name('gpu_offload_analysis.py')),
                        lock=str(args.lock.resolve()),
                        note='CPU is all-thread process work. p99 uses benchmark loop. Rendered guard and independent holdouts required separately.')
        (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        rows = []
        with (output / 'measurements.jsonl').open('w') as stream:
            for scenario in config['scenarios']:
                for n in range(-1, rounds):
                    order = round_order(config['variants'], n)
                    reference = None
                    for variant in order:
                        dest = output / scenario['id'] / str(n) / variant['id']
                        row = dict(campaign_stage=args.stage, diagnostic_stage=args.stage == 'diagnose',
                                   scenario=scenario['id'], phase=scenario.get('phase'), group=scenario.get('group'),
                                   map_id=scenario.get('map_id'), control=scenario.get('control', False),
                                   round=n, variant=variant['id'], **execute(variant, scenario, dest, 0 if args.cold else config['warmup_ticks'], reservation=config.get('reservation'), gpu_uuid=config.get('gpu_uuid')))
                        if 'result' in row:
                            signature = tuple(row['result'][k] for k in ('initialChecksum', 'finalChecksum', 'ticks', 'benchmark_measured_ticks'))
                            if reference is None: reference = signature
                            elif signature != reference:
                                row['valid'] = False; row['errors'].append('paired simulation mismatch')
                        rows.append(row)
                        stream.write(json.dumps(row) + '\n'); stream.flush()
                        print(f"{scenario['id']} round={n} {variant['id']} valid={row['valid']}", flush=True)
                        if 'paired simulation mismatch' in row['errors']:
                            raise RuntimeError('simulation diverged; retained evidence, campaign stopped')
        candidates = [v['id'] for v in config['variants'] if v['id'] != config['control']]
        summary = campaign_summary(rows, config['control'], candidates, config['scenarios'],
                                   rounds=rounds, stage=args.stage)
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__': main()
