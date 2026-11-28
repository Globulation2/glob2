#!/usr/bin/env python3
"""Make or verify the release check's near-victory save (docs/releases/release-check.md).

The release check (src/app/ReleaseCheck.h) plays content/data/release-check/victory.game.gz
to its victory screen. The save is the AI match below, a few hundred ticks before team 0
eliminates team 1:

    glob2 game run --map-file content/maps/SmallForTwo.map.gz --game-seed 1 \\
        --player warrush --player numbi --win-condition death --win-condition opponents \\
        --win-probability 0 --ticks 50000 --save every:250 --output-dir DIR

A simulation change can move that ending. --verify plays the shipped save headless and
fails unless team 0 wins within --within ticks; without it, the script plays the match
again and replaces the save with the last checkpoint at least --before ticks before the win.

    python3 tools/release/make_release_check_save.py --glob2 build/.../src/glob2 --verify
    python3 tools/release/make_release_check_save.py --glob2 build/.../src/glob2
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SAVE = ROOT / 'content/data/release-check/victory.game.gz'
RECORD = SAVE.with_name('victory.json')  # {"tick": saved tick, "ends": the tick it was won}
MATCH = ['--map-file', 'content/maps/SmallForTwo.map.gz', '--game-seed', '1', '--player', 'warrush',
         '--player', 'numbi']
CONDITIONS = ['--win-condition', 'death', '--win-condition', 'opponents', '--win-probability', '0']


def run(glob2, arguments, output):
    subprocess.run([str(glob2), 'game', 'run', *arguments, '--output-dir', str(output)], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return json.loads((output / 'result.json').read_text())


def won_by_team_zero(result):
    return result.get('status') == 'completed' and result.get('winning_teams') == [0]


def verify(glob2, within, work):
    saved = json.loads(RECORD.read_text())['tick']
    result = run(glob2, ['--load-game', str(SAVE), '--ticks', str(saved + within)], work / 'verify')
    teams = {team['team']: team for team in result.get('teams', [])}
    loss = teams.get(1, {}).get('eliminated_tick', -1)
    if not won_by_team_zero(result) or result.get('ticks', 0) <= 0:
        raise SystemExit(f'The release check save no longer ends in a win: {result.get("termination")}, '
                         f'winners {result.get("winning_teams")}; regenerate it with this script')
    print(f'Release check save: team 0 wins at tick {result["ticks"]} (team 1 out at {loss})')
    return result['ticks']


def make(glob2, before, work):
    result = run(glob2, MATCH + CONDITIONS + ['--ticks', '50000', '--save', 'every:250'], work / 'match')
    if not won_by_team_zero(result):
        raise SystemExit('The match no longer ends with team 0 winning; choose another seed or players')
    end = result['ticks']
    checkpoints = sorted(((int(path.stem.split('-')[1].split('.')[0]), path)
                          for path in (work / 'match').glob('checkpoint-*.game.gz')), reverse=True)
    for tick, path in checkpoints:
        if tick <= end - before:
            SAVE.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, SAVE)
            RECORD.write_text(json.dumps({'tick': tick, 'ends': end}) + '\n')
            print(f'Release check save: tick {tick}, {end - tick} ticks before the win at {end}')
            return
    raise SystemExit('No checkpoint far enough before the win')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--glob2', type=Path, required=True)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--before', type=int, default=200, help='ticks between the save and the win')
    parser.add_argument('--within', type=int, default=2000, help='--verify: the win must come this soon')
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory(prefix='glob2-release-check-save-') as work:
        if args.verify:
            verify(args.glob2.resolve(), args.within, Path(work))
            return 0
        make(args.glob2.resolve(), args.before, Path(work))
    return 0


if __name__ == '__main__':
    sys.exit(main())
