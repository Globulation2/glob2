"""Freeze development games, including large maps and small-map controls.

This deliberately excludes sealed qualification seeds. 1024+ real-game fixtures
require the native fixture harness: ordinary generator/import controls stop at512.
"""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FAMILIES = {'open': ('symmetric-arena', ['moat=0', 'lakes=0']),
            'water': ('fingerprint', ['pattern=1']),
            'corridors': ('maze', [])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lock', type=Path, required=True)
    parser.add_argument('--sizes', type=int, nargs='+', choices=[64, 128, 256, 512], default=[256, 512])
    parser.add_argument('--seeds', type=int, nargs='+', default=[91001, 91002])
    parser.add_argument('--families', nargs='+', choices=list(FAMILIES), default=list(FAMILIES))
    parser.add_argument('--controls', action='store_true', help='also freeze64 and128 open-map controls')
    args = parser.parse_args()
    output, binary = args.output.resolve(), args.binary.resolve()
    output.mkdir(parents=True, exist_ok=False)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    scenarios, preparation = [], []
    cases = [(f, size, seed) for f in args.families for size in args.sizes for seed in args.seeds]
    if args.controls:
        cases += [('open', size, seed) for size in (64, 128) for seed in args.seeds if size not in args.sizes]
    with args.lock.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for family, size, seed in cases:
            generator, options = FAMILIES[family]
            identity = f'{family}-{size}-{seed}'
            dest = output / identity
            teams = 2 if size <= 128 else 4
            command = [str(binary), 'game', 'run', '--generator', generator, '--map-seed', str(seed),
                       '--game-seed', '19', '--ticks', '32768', '--compute-threads', '8',
                       '--save', 'initial', '--save', 'every:8192', '--save', 'final', '--output-dir', str(dest)]
            for option in [f'width={size.bit_length()-1}', f'height={size.bit_length()-1}', f'teams={teams}', *options]:
                command += ['--set', option]
            for n in range(teams): command += ['--player', ('maxima', 'cortex', 'nicowar')[n % 3]]
            with (output / f'{identity}.log').open('w') as log:
                proc = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            receipt = dict(id=identity, command=command, exit_code=proc.returncode, available_phases=[])
            if not proc.returncode:
                receipt['result'] = json.loads((dest / 'result.json').read_text())
                for phase, tick in [('early', 0), ('middle', 8192), ('late', 24576)]:
                    fixture = dest / ('initial.game.gz' if tick == 0 else f'checkpoint-{tick}.game.gz')
                    if not fixture.exists(): continue
                    receipt['available_phases'].append(phase)
                    scenarios.append(dict(id=f'{identity}-{phase}', group=family, map_id=identity,
                        phase=phase, size=size, control=size <= 128, start_tick=tick,
                        args=['--load-game', str(fixture), '--ticks', str(tick + 8192)],
                        fixture_sha256={str(fixture): hashlib.sha256(fixture.read_bytes()).hexdigest()}))
            preparation.append(receipt)
            (output / 'preparation.json').write_text(json.dumps(preparation, indent=2) + '\n')
            print(f'{identity}: status={proc.returncode} phases={receipt["available_phases"]}', flush=True)
    manifest = dict(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    scenarios=scenarios, missing_phases=[r['id'] for r in preparation if len(r['available_phases']) != 3],
                    independent_unit='map_id; early/middle/late from one map are clustered',
                    note='development inputs, no final holdout qualification; incomplete progression is recorded')
    (output / 'windows.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if not scenarios: raise RuntimeError('no retained game windows')


if __name__ == '__main__': main()
