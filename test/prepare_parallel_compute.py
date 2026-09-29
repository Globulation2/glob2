"""Freeze a seeded benchmark corpus; generated evidence stays outside Git.

The full corpus has land/water, 2/4 teams, two seeds, three homogeneous AI mixes
and a mixed match. --quick selects three calibration games and a small control.
Each retained checkpoint is a separate fixed-tick scenario; completion.json
runs each initial state to the normal end condition or a 90000-tick cap.
"""
import argparse
import hashlib
import itertools
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    mixes = ['maxima', 'cortex', 'nicowar', 'mixed']
    configurations = list(itertools.product(['land', 'water'], [2, 4], [1001, 1002], mixes))
    if args.quick:
        configurations = [('land', 4, 1001, 'maxima'), ('water', 4, 1001, 'cortex'), ('water', 4, 1002, 'nicowar'), ('land', 2, 1002, 'mixed')]
    windows, completed = [], []
    for terrain, teams, seed, mix in configurations:
        name = f'{terrain}-{teams}-{seed}-{mix}'
        directory = output / name
        size = 6 if args.quick and teams == 2 else 8
        params = ['width='+str(size), 'height='+str(size), 'teams='+str(teams)]
        generator = 15 if terrain == 'land' else 26
        params += ['moat=0', 'lakes=0'] if terrain == 'land' else ['pattern=1']
        command = [str(binary), '--run-game', '--generator', str(generator), '--map-seed', str(seed), '--game-seed', '19', '--ticks', '32768', '--save', 'initial', '--save', 'every:8192', '--save', 'final', '--output-dir', str(directory)]
        for param in params: command += ['--param', param]
        for player in range(teams): command += ['--player', mixes[player % 3] if mix == 'mixed' else mix]
        with (output / (name+'.log')).open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        (directory / 'command.json').write_text(json.dumps(command))
        for phase, tick in [('early', 0), ('middle', 8192), ('late', 24576)]:
            save = directory / ('initial.game' if tick == 0 else f'checkpoint-{tick}.game')
            if not save.exists(): continue  # finished games have no later checkpoint
            scenario = dict(id=name+'-'+phase, group=mix, control=(size == 6), start_tick=tick, args=['--load-game', str(save), '--ticks', str(tick+8192)], fixture_sha256={str(save): hashlib.sha256(save.read_bytes()).hexdigest()})
            windows.append(scenario)
            if tick == 0:
                completed.append(scenario | {'id': name+'-completion', 'args': ['--load-game', str(save), '--ticks', '90000']})
        print(name, flush=True)
    meta = dict(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    for name, scenarios in [('windows', windows), ('completion', completed)]:
        (output / (name+'.json')).write_text(json.dumps(meta | {'scenarios': scenarios}, indent=2))


if __name__ == '__main__':
    main()
