"""Serialized paired GPU experiments from frozen retained-game manifests.

Config keys: control, variants [{id,binary,sha256,source_revision,build_flags,env}],
scenarios [{id,args,fixture_sha256,phase,group}], warmup_ticks. Source/build claims
must have separately retained build receipts; a binary hash alone cannot prove them.
No sealed qualification inputs are generated or consumed by this development tool.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time
from gpu_offload_analysis import summarize, cpu_ceiling, aggregate_cpu

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


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
    for s in config['scenarios']:
        if not s.get('fixture_sha256') or '--load-game' not in s['args'] or '--ticks' not in s['args']:
            raise ValueError('retained fixed-tick loaded scenario required')
        if any(k in s['args'] for k in ('--save', '--telemetry', '--write-replay', '--benchmark-warmup', '--output-dir')):
            raise ValueError('scenario args must exclude optional exports and runner options')
        for path, expected in s['fixture_sha256'].items():
            if sha(path) != expected: raise ValueError('fixture changed: ' + path)
    if config['warmup_ticks'] < 0: raise ValueError('nonnegative warmup required')


def resource_snapshot():
    """Read-only inventory outside the timed window; command names exclude args."""
    snapshot = {'monotonic_ns': time.monotonic_ns()}
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
    snapshot.update(inventory_available=True, compilers=compilers,
                    active_compiler_detected=any(p['state'].startswith(('R', 'D')) for p in compilers),
                    top_cpu_commands=processes[:10])
    return snapshot


def execute(variant, scenario, output, warmup, *, extra_args=()):
    output.mkdir(parents=True, exist_ok=False)
    threads = str(variant.get('compute_threads', '8'))
    command = [variant['binary'], 'game', 'run', *scenario['args'], *extra_args, '--compute-threads', threads,
               '--benchmark-warmup', str(warmup), '--output-dir', str(output)]
    env = dict(os.environ, **variant.get('env', {}))
    resources_before = resource_snapshot()
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
               resources_before=resources_before, resources_after=resource_snapshot())
    row['resource_contaminated'] = any(not s.get('inventory_available', False) or s.get('active_compiler_detected', False) for s in
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--lock', required=True, type=Path, help='one shared path for all builds and campaigns')
    parser.add_argument('--stage', choices=['screen', 'confirm'], default='screen')
    parser.add_argument('--cold', action='store_true', help='fresh process and no simulation warmup; retain separately')
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    validate(config)
    rounds = 5 if args.stage == 'screen' else 10
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    with args.lock.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        metadata = dict(schema=1, configuration=config, config_sha256=sha(args.config),
                        platform=platform.platform(), rounds=rounds, cold=args.cold,
                        runner_sha256=sha(__file__), analysis_sha256=sha(Path(__file__).with_name('gpu_offload_analysis.py')),
                        lock=str(args.lock.resolve()),
                        note='CPU is all-thread process work. p99 uses benchmark loop. Rendered guard and independent holdouts required separately.')
        (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        rows = []
        with (output / 'measurements.jsonl').open('w') as stream:
            for scenario in config['scenarios']:
                for n in range(-1, rounds):
                    shift = (n + 1) % len(config['variants'])
                    order = config['variants'][shift:] + config['variants'][:shift]
                    if n % 2: order = list(reversed(order))
                    reference = None
                    for variant in order:
                        dest = output / scenario['id'] / str(n) / variant['id']
                        row = dict(scenario=scenario['id'], phase=scenario.get('phase'), group=scenario.get('group'),
                                   map_id=scenario.get('map_id'), control=scenario.get('control', False),
                                   round=n, variant=variant['id'], **execute(variant, scenario, dest, 0 if args.cold else config['warmup_ticks']))
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
        summary = dict(scenarios=summarize(rows, config['control'], candidates, minimum_pairs=rounds, confirmation=args.stage == 'confirm'),
                       aggregate_cpu=aggregate_cpu(rows, config['control'], candidates))
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__': main()
