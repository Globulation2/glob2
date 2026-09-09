#!/usr/bin/env python3
"""Compare Numbi tournament orders and state across a saved-game continuation.

Requires the experiment branch's audit/checkpoint-enabled game binary.
The default fixture exercises the inn decision at tick 3491.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(binary, output, checkpoint_tick, terminal_tick):
    output.mkdir(parents=True, exist_ok=True)
    checkpoint = output / 'checkpoint.game'
    prefix = [str(binary), '-nicowar-scenario-match-nox',
              str(ROOT / 'maps/A_big_pond.map'), '1342179281',
              '2', '6', '1', '0', '1', str(terminal_tick)]
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('GLOB2_MAXIMA_', 'GLOB2_CORTEX_', 'GLOB2_NICOWAR_'))}
    records = {}
    for mode in ('full', 'loaded'):
        audit = output / (mode + '.jsonl')
        args = (prefix + ['--maxima-checkpoint-save', str(checkpoint), str(checkpoint_tick)]
                if mode == 'full' else
                [str(binary), '--maxima-checkpoint-run', str(checkpoint), str(terminal_tick - checkpoint_tick)])
        args += ['--maxima-audit', str(audit), '{}']
        env['GLOB2_REPLAY_PATH'] = str(output / (mode + '.replay'))
        (output / (mode + '-command.json')).write_text(json.dumps(args, indent=2) + '\n')
        with (output / (mode + '.log')).open('w') as log:
            result = subprocess.run(args, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        if result.returncode:
            raise AssertionError(f'{mode} engine exited {result.returncode}; see {output}')
        records[mode] = [json.loads(line) for line in audit.read_text().splitlines()]
    full, loaded = records['full'], records['loaded']

    def state(rows, kind):
        row = next(row for row in rows if row['type'] == kind)
        return {key: row[key] for key in ('tick', 'world_checksum', 'rng', 'players')}

    def orders(rows):
        return [{k: v for k, v in row.items() if k != 'sequence'} for row in rows
                if row['type'] in ('order_issued', 'order_dispatched') and row['tick'] >= checkpoint_tick]

    result = {
        'checkpoint_tick': checkpoint_tick,
        'terminal_tick': terminal_tick,
        'boundary_equal': state(full, 'checkpoint') == state(loaded, 'start'),
        'terminal_equal': state(full, 'terminal') == state(loaded, 'terminal'),
        'orders_equal': orders(full) == orders(loaded),
    }
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
    if not all(result[key] for key in ('boundary_equal', 'terminal_equal', 'orders_equal')):
        raise AssertionError(f'Numbi save/load continuation diverged; see {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--checkpoint', type=int, default=2137)
    parser.add_argument('--ticks', type=int, default=5000)
    args = parser.parse_args()
    if not 0 < args.checkpoint < args.ticks:
        parser.error('require 0 < checkpoint < ticks')
    if args.output:
        run(args.binary.resolve(), args.output.resolve(), args.checkpoint, args.ticks)
    else:
        with tempfile.TemporaryDirectory(prefix='numbi-continuation-') as directory:
            run(args.binary.resolve(), Path(directory), args.checkpoint, args.ticks)
