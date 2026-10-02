#!/usr/bin/env python3
"""Paired native CPU benchmarks using --benchmark-warmup (no malloc logging)."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import signal
import time
import subprocess

ROOT = Path(__file__).resolve().parents[1]
METRICS = ('benchmark_setup_cpu_ns', 'benchmark_run_cpu_ns', 'benchmark_save_cpu_ns')


def confidence(ratios):
    logs = [math.log(x) for x in ratios]
    rng = random.Random(19)
    samples = sorted(statistics.mean(rng.choices(logs, k=len(logs))) for _ in range(10000))
    return {'paired_change_percent': 100 * math.expm1(statistics.mean(logs)),
            'upper_95_percent': 100 * math.expm1(samples[int(.95 * len(samples))]),
            'pairs': len(ratios)}


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def stop_on_signal(signum, _frame):
    raise SystemExit(128 + signum)


def run_pair(commands, directories, order, env, slice_seconds):
    if not slice_seconds:
        for label in order:
            with (directories[label] / 'run.log').open('w') as log:
                subprocess.run(commands[label], cwd=ROOT, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True)
        return
    processes, logs = {}, {}
    try:
        for label in order:
            logs[label] = (directories[label] / 'run.log').open('w')
            processes[label] = subprocess.Popen(commands[label], cwd=ROOT, env=env,
                                                stdout=logs[label], stderr=subprocess.STDOUT)
            os.kill(processes[label].pid, signal.SIGSTOP)
        while any(p.poll() is None for p in processes.values()):
            for label in order:
                p = processes[label]
                if p.poll() is not None:
                    continue
                os.kill(p.pid, signal.SIGCONT)
                time.sleep(slice_seconds)
                if p.poll() is None:
                    try:
                        os.kill(p.pid, signal.SIGSTOP)
                    except ProcessLookupError:
                        p.wait()
        for label, p in processes.items():
            if p.returncode:
                raise subprocess.CalledProcessError(p.returncode, commands[label])
    finally:
        for p in processes.values():
            if p.poll() is None:
                try:
                    os.kill(p.pid, signal.SIGCONT)
                except ProcessLookupError:
                    p.wait()
                    continue
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
        for log in logs.values():
            log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--fixture', action='append', required=True, help='START_TICK=SAVE_PATH')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--resume', action='store_true', help='resume completed pairs after validating inputs')
    parser.add_argument('--warmup', type=int, default=1024)
    parser.add_argument('--ticks', type=int, default=4096, help='measured ticks after warmup')
    parser.add_argument('--repeat', type=int, default=7)
    parser.add_argument('--max-repeat', type=int, default=21)
    parser.add_argument('--limit', type=float, default=2)
    parser.add_argument('--interleave-seconds', type=float, default=0,
                        help='POSIX: alternate stopped processes in short slices to reduce load drift')
    args = parser.parse_args()
    if args.repeat < 2 or args.max_repeat < args.repeat or args.warmup < 0 or args.ticks < 1:
        parser.error('invalid repetition or tick counts')
    if not math.isfinite(args.interleave_seconds) or args.interleave_seconds < 0 or (args.interleave_seconds and os.name != 'posix'):
        parser.error('interleaved measurements require POSIX and a positive slice duration')
    signal.signal(signal.SIGTERM, stop_on_signal)
    fixtures = []
    for spec in args.fixture:
        tick, path = spec.split('=', 1)
        fixtures.append((int(tick), Path(path).resolve()))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    env = os.environ.copy()
    for name in ('MallocStackLogging', 'MallocStackLoggingNoCompact', 'DYLD_INSERT_LIBRARIES'):
        env.pop(name, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    summary = {'binaries': {k: {'path': str(v), 'sha256': digest(v)} for k, v in binaries.items()},
               'interleave_seconds': args.interleave_seconds, 'warmup_ticks': args.warmup, 'measured_ticks': args.ticks, 'limit_percent': args.limit,
               'fixtures': []}
    previous = None
    if args.resume:
        previous = json.loads((output / 'comparison.json').read_text())
        for key in ('binaries', 'warmup_ticks', 'measured_ticks', 'limit_percent', 'interleave_seconds'):
            if previous.get(key) != summary[key]:
                parser.error(f'resume input differs: {key}')
        if len(previous['fixtures']) > len(fixtures):
            parser.error('resume fixture list differs')
    for index, (start, save) in enumerate(fixtures):
        result = {'save': str(save), 'sha256': digest(save), 'initial_tick': start, 'pairs': []}
        if previous and index < len(previous['fixtures']):
            prior = previous['fixtures'][index]
            if any(prior[k] != result[k] for k in ('save', 'sha256', 'initial_tick')):
                parser.error(f'resume fixture {index} differs')
            result = prior
        summary['fixtures'].append(result)
        if len(result['pairs']) >= args.repeat and result.get('passed', False):
            continue
        for pair in range(len(result['pairs']), args.max_repeat):
            values, commands, directories = {}, {}, {}
            order = ('baseline', 'candidate') if pair % 2 == 0 else ('candidate', 'baseline')
            for label in order:
                directory = output / f'fixture-{index}-pair-{pair}-{label}'
                command = [str(binaries[label]), '--run-game', '--load-game', str(save),
                           '--ticks', str(start + args.warmup + args.ticks),
                           '--benchmark-warmup', str(args.warmup), '--save', 'final',
                           '--output-dir', str(directory)]
                directory.mkdir(exist_ok=True)
                commands[label], directories[label] = command, directory
            run_pair(commands, directories, order, env, args.interleave_seconds)
            for label in order:
                directory, command = directories[label], commands[label]
                data = json.loads((directory / 'result.json').read_text())
                if data['benchmark_measured_ticks'] != args.ticks:
                    raise RuntimeError('game ended before the measured window completed')
                text = (directory / 'run.log').read_text()
                checksum = re.findall(r'nox::gui.game.checkSum\(\) = ([0-9a-f]+)', text)[-1]
                saved_hash = digest(directory / 'final.game.gz')
                values[label] = {k: data[k] for k in METRICS}
                values[label].update(checksum=checksum, saved_sha256=saved_hash, command=command)
            if values['baseline']['checksum'] != values['candidate']['checksum']:
                raise RuntimeError('simulation checksums differ')
            if values['baseline']['saved_sha256'] != values['candidate']['saved_sha256']:
                raise RuntimeError('saved bytes differ')
            result['pairs'].append(values)
            result['cpu'] = {key: confidence([p['candidate'][key] / p['baseline'][key]
                                             for p in result['pairs']]) for key in METRICS}
            result['passed'] = all(v['upper_95_percent'] <= args.limit for v in result['cpu'].values())
            (output / 'comparison.json').write_text(json.dumps(summary, indent=2))
            print(f'fixture {index} pair {pair + 1}: {result["cpu"]}', flush=True)
            # Keep one full save per variant/fixture; hashes and raw logs retain each comparison.
            for label in ('baseline', 'candidate'):
                if pair:
                    (output / f'fixture-{index}-pair-{pair}-{label}' / 'final.game.gz').unlink()
            if pair + 1 >= args.repeat and result['passed']:
                break
        if not result['passed']:
            print(f'fixture {index} failed the CPU gate', flush=True)
    summary['passed'] = all(f['passed'] for f in summary['fixtures'])
    (output / 'comparison.json').write_text(json.dumps(summary, indent=2))
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
