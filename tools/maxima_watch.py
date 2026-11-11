#!/usr/bin/env python3
"""Tweak-compile-watch loop for AI Maxima.

One command rebuilds the -O2 perf client (incremental), plays a seeded match
headless with Maxima in seat 0 (a full game takes seconds), prints a
scoreboard, then opens the game in the replay viewer: timeline scrubber,
speed control, fog toggle and per-player view.

    python3 tools/maxima_watch.py                       # Maxima vs Nicowar on SmallForTwo
    python3 tools/maxima_watch.py --map FourSquares1 --vs nicowar,castor,maxima
    python3 tools/maxima_watch.py --param tactics.min_force=8   # strategy tweak, no C++ edit
    python3 tools/maxima_watch.py --seeds 1-8           # batch: scoreboard only, no window
    python3 tools/maxima_watch.py --no-build --no-watch

Strategy numbers live in content/data/maxima/*.strategy and are read at game
start, so editing those (or passing --param, which applies to every Maxima
seat) needs no recompile; use --no-build to skip the build. The same seed and
code give the same game, so after a tweak you can rewatch it and compare.

It uses the perf build: games play out in seconds and the replay window
loads promptly, which the -O0 dev client does not. Every run is kept under
artifacts/maxima-watch/.
"""
import argparse
import datetime
import json
import os
import platform
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = 'mingw' if os.name == 'nt' else platform.system().lower()
GLOB2 = ROOT / 'build' / TOOLCHAIN / 'client' / 'perf' / 'src' / ('glob2' + ('.exe' if os.name == 'nt' else ''))
OUT_ROOT = ROOT / 'artifacts' / 'maxima-watch'


def run(cmd, quiet=False, **kw):
    if not quiet:
        print('+', ' '.join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, **kw)


def seeds(text):
    out = []
    for part in text.split(','):
        lo, _, hi = part.partition('-')
        out += range(int(lo), int(hi or lo) + 1)
    return out


def play(map_file, players, params, seed, ticks, out):
    cmd = [GLOB2, 'game', 'run', '--map-file', map_file, '--game-seed', seed,
           '--ticks', ticks, '--write-replay', '--output-dir', out]
    for player in players:
        cmd += ['--player', player]
    for seat, player in enumerate(players):
        if player == 'maxima':
            cmd += [a for kv in params for a in ('--ai-param', f'{seat}:{kv}')]
    log = out.with_suffix('.log')
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(log, 'w') as f:
        if run(cmd, quiet=True, stdout=f, stderr=subprocess.STDOUT).returncode:
            raise SystemExit(f'game failed (seed {seed}); see {log}')
    return json.loads((out / 'result.json').read_text())


def scoreboard(seed, players, result):
    print(f'\nseed {seed}: {result["termination"]} at tick {result["ticks"]}; '
          f'winners: {result["winning_teams"] or "none"}')
    print(f'  {"seat":<14}{"outcome":<10}{"win%":>5}{"units":>7}{"work":>6}{"expl":>6}{"warr":>6}{"bldg":>6}')
    for seat, team in enumerate(result['teams']):
        win = (team.get('win_probability') or {}).get('permille', 0) / 10
        print(f'  {seat}={players[seat]:<12}{str(team["outcome"]):<10}{win:>5.0f}{team["units"]:>7}'
              f'{team["workers"]:>6}{team["explorers"]:>6}{team["warriors"]:>6}{team["buildings"]:>6}')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--map', default='SmallForTwo', help='name in content/maps, or a .map.gz path')
    p.add_argument('--vs', default='nicowar',
                   help='opponent AI for every other seat, or a comma list for seats 1..N')
    p.add_argument('--seed', type=int, default=1, help='game seed')
    p.add_argument('--seeds', help='batch of seeds, e.g. 1-8 or 1,4,9; implies --no-watch')
    p.add_argument('--ticks', type=int, default=30000, help='tick cap (games usually end earlier)')
    p.add_argument('--param', action='append', default=[], help='Maxima strategy KEY=VALUE (repeatable)')
    p.add_argument('--no-build', action='store_true')
    p.add_argument('--no-watch', action='store_true', help='print the result without opening a window')
    p.add_argument('--window-size', default='1600x1000')
    args = p.parse_args()

    if not args.no_build:
        if run([sys.executable, 'tools/dev_build.py', 'perf=1'], stdout=subprocess.DEVNULL).returncode:
            raise SystemExit('build failed; rerun: python3 tools/dev_build.py perf=1')

    map_file = Path(args.map)
    if not map_file.exists():
        map_file = ROOT / 'content' / 'maps' / (args.map + '.map.gz')
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    out = OUT_ROOT / f'{stamp}-{map_file.name.split(".")[0]}'
    out.mkdir(parents=True, exist_ok=True)

    opponents = args.vs.split(',')
    if len(opponents) == 1:
        run([GLOB2, 'map', 'preview', map_file, '--report-file', out / 'map.json'],
            quiet=True, check=True, stdout=subprocess.DEVNULL)
        slots = json.loads((out / 'map.json').read_text())['map']['player_slots']
        opponents = opponents * (slots - 1)
    players = ['maxima'] + opponents
    print(f'seats: {", ".join(f"{i}={p}" for i, p in enumerate(players))}'
          + (f'; Maxima params: {" ".join(args.param)}' if args.param else ''))

    batch = seeds(args.seeds) if args.seeds else [args.seed]
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda s: play(map_file, players, args.param, s, args.ticks,
                                               out / f's{s}'), batch))
    for seed, result in zip(batch, results):
        scoreboard(seed, players, result)
    if len(batch) > 1:
        wins = sum(0 in r['winning_teams'] for r in results)
        print(f'\nMaxima won {wins}/{len(batch)}')

    replay = out / f's{batch[0]}' / 'game.replay'
    print(f'\nreplay: {GLOB2.relative_to(ROOT)} replay {replay.relative_to(ROOT)}')
    if not args.no_watch and not args.seeds:
        run([GLOB2, 'replay', replay, '--no-fullscreen', '--resizable', '--window-size', args.window_size],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == '__main__':
    main()
