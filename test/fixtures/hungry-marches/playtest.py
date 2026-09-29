#!/usr/bin/env python3
"""Replayable rotation games with compressed logs/saves; run from repository root.

Each completed result is kept beside its exact command. Failed or incomplete runs
are not counted as games. Repeating an identical command resumes completed jobs.
"""
import argparse
import concurrent.futures
import gzip
import json
import os
import pathlib
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', required=True)
parser.add_argument('--out', required=True)
parser.add_argument('--jobs', type=int, default=4)
parser.add_argument('--ticks', type=int, default=45000)
mode = parser.add_mutually_exclusive_group()
mode.add_argument('--reference', action='store_true')
mode.add_argument('--compact', action='store_true')
mode.add_argument('--duel-mirror', action='store_true')
mode.add_argument('--crowded', action='store_true')
options = parser.parse_args()
root = pathlib.Path.cwd()
binary = str((root / options.binary).resolve())
out = (root / options.out).resolve()
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def run(arguments, folder):
    folder.mkdir(parents=True, exist_ok=True)
    command = [binary, *arguments, '--output-dir', str(folder)]
    command_path = folder / 'command.json'
    result_path = folder / 'result.json'
    saved_path = folder / ('final.game.gz' if '--run-game' in arguments else 'map-r0.map')
    if command_path.exists() and result_path.exists():
        try:
            prior = json.loads(result_path.read_text())
            if (json.loads(command_path.read_text()) == command
                    and prior.get('status') == 'completed' and saved_path.exists()):
                return 0
        except (ValueError, OSError):
            pass
    command_path.write_text(json.dumps(command, indent=2) + '\n')
    with gzip.open(folder / 'run.log.gz', 'wb') as log:
        process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, env=env)
        shutil.copyfileobj(process.stdout, log)
        code = process.wait()
    for path in folder.glob('*.game'):
        with path.open('rb') as source, gzip.open(str(path) + '.gz', 'wb') as target:
            shutil.copyfileobj(source, target)
        path.unlink()
    if code == 0:
        try:
            result = json.loads(result_path.read_text())
            if result.get('status') != 'completed' or not saved_path.exists():
                return 1
        except (ValueError, OSError):
            return 1
    return code


method = 38 if options.reference else 69
cases = [(101, 8, 8, 4), (202, 8, 8, 4), (303, 8, 8, 4), (404, 8, 8, 4)]
if options.reference:
    cases = [(101, 8, 8, 4)]
elif options.compact:
    cases = [(202, 7, 7, 2), (606, 7, 8, 4)]
elif options.duel_mirror:
    cases = [(202, 7, 7, 2), (707, 7, 7, 2)]
elif options.crowded:
    cases = [(7, 8, 8, 12), (101, 8, 8, 12)]

work = []
for seed, width, height, teams in cases:
    rotations = 1 if options.crowded else teams
    folder = out / f'map-{seed}'
    arguments = ['--generate-map', '--generator', str(method), '--map-seed', str(seed),
                 '--param', f'width={width}', '--param', f'height={height}',
                 '--param', f'teams={teams}', '--write-map', 'true',
                 '--rotations', str(rotations)]
    code = run(arguments, folder)
    if code:
        raise SystemExit(f'Map {seed}: exit {code}; see {folder}')
    if options.crowded:
        # Nicowar's existing Echo iterator overruns a full twelve-team roster.
        players = (['cortex', 'cabino', 'maxima'] * 4)[:teams]
    elif options.duel_mirror:
        players = ['nicowar'] * teams
    elif teams == 2:
        players = ['nicowar', 'maxima']
    else:
        players = (['nicowar', 'cortex', 'cabino', 'maxima'] * 3)[:teams]
    for rotation in range(rotations):
        target = out / f'game-{seed}-r{rotation}'
        arguments = ['--run-game', '--map-file', str(folder / f'map-r{rotation}.map'),
                     '--game-seed', '19', '--ticks', str(options.ticks),
                     '--telemetry', 'team-timeline', '--save', 'final']
        for ai in players:
            arguments += ['--player', ai]
        work.append((arguments, target))

with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
    for job, code in zip(work, pool.map(lambda job: run(*job), work)):
        print(job[1].name, code, flush=True)
        if code:
            raise SystemExit(f'Game failed: {job[1]}')
