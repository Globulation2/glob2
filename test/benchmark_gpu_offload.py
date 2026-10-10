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
from gpu_offload_analysis import summarize, cpu_ceiling

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
    for s in config['scenarios']:
        if not s.get('fixture_sha256') or '--load-game' not in s['args'] or '--ticks' not in s['args']:
            raise ValueError('retained fixed-tick loaded scenario required')
        if any(k in s['args'] for k in ('--save', '--telemetry', '--write-replay', '--benchmark-warmup', '--output-dir')):
            raise ValueError('scenario args must exclude optional exports and runner options')
        for path, expected in s['fixture_sha256'].items():
            if sha(path) != expected: raise ValueError('fixture changed: ' + path)
    if config['warmup_ticks'] < 0: raise ValueError('nonnegative warmup required')


def execute(variant, scenario, output, warmup):
    output.mkdir(parents=True, exist_ok=False)
    command = [variant['binary'], 'game', 'run', *scenario['args'], '--compute-threads', '8',
               '--benchmark-warmup', str(warmup), '--output-dir', str(output)]
    env = dict(os.environ, **variant.get('env', {}))
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
               valid=process.returncode == 0, errors=[])
    if process.returncode:
        row['errors'].append('process failure'); return row
    result = json.loads((output / 'result.json').read_text())
    row['result'] = result
    row['cpu_ceiling'] = cpu_ceiling(result)
    if result['benchmark_measured_ticks'] <= 0 or result['compute_threads'] != 8:
        row['errors'].append('empty window or executor fallback')
    if result['ticks'] != int(scenario['args'][scenario['args'].index('--ticks') + 1]):
        row['errors'].append('game ended before fixed tick endpoint')
    if variant.get('require_gpu'):
        start, end = result.get('benchmark_opencl_at_start'), result.get('benchmark_opencl_at_end')
        if not start or not end:
            row['errors'].append('GPU execution counters unavailable')
        elif (warmup and not start['available']) or end['fields'] <= start['fields']:
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
        summary = summarize(rows, config['control'], [v['id'] for v in config['variants'] if v['id'] != config['control']], minimum_pairs=rounds)
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__': main()
