#!/usr/bin/env python3
"""Capture and replay point-to-point search corpora (field/astar/AStar.h).

    python3 tools/astar_benchmark.py capture --binary build/.../src/glob2 --ticks 1500
    python3 tools/astar_benchmark.py run [--opt 2|3] [--repeat 5] [--only point|goals|claims]
    python3 tools/astar_benchmark.py run --verify
    python3 tools/astar_benchmark.py profile --tool callgrind|cachegrind [--variant new]

`capture` runs the content/games saves headlessly with GLOB2_ASTAR_CAPTURE set and
writes artifacts/astar/corpus.bin: every query with the entry costs of the window
it searched. `run` builds tools/astar_benchmark.cpp standalone (g++, -O2 by
default, no -march) and prints ns/query and expansions/query for the unified
search, its reference and the replaced searches ("legacy": the old unit search
ported to the captured entries, and the old claim search extracted from git).
`profile` runs one variant under valgrind and annotates the result.
"""
from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'artifacts' / 'astar'
CORPUS = WORK / 'corpus.bin'
OLD_CLAIM_SEARCH = 'src/map/claims/ClaimRouteSearch.h'
GAMES = ('gd-small-2ai', 'gd-archipelago', 'gd-large-4ai', 'gd-bigarena-long', 'G2', 'cross-replay')
# New games from maps (saves made before resource claims never route claims):
# map, players, ticks, seed. Long enough for armies to meet.
MATCHES = (
    ('SmallForTwo', ('castor', 'maxima'), 20000, 19),
    ('SmallForTwo', ('maxima', 'maxima'), 20000, 23),
    ('FourSquares1', ('castor', 'cortex', 'maxima', 'numbi'), 20000, 713),
    ('FourSquares1', ('maxima', 'maxima', 'castor', 'castor'), 20000, 41),
)


def git(*args: str) -> str:
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


def baseline_claim_search(directory: Path) -> bool:
    """Write the replaced claim search from the last revision that had it."""
    deleted = git('log', '-1', '--format=%H', '--diff-filter=D', '--', OLD_CLAIM_SEARCH).strip()
    revision = f'{deleted}^' if deleted else 'HEAD'
    try:
        text = git('show', f'{revision}:{OLD_CLAIM_SEARCH}')
    except subprocess.CalledProcessError:
        return False
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'ClaimRouteSearch.h').write_text(text)
    return True


def build(opt: str, extra: list[str]) -> Path:
    WORK.mkdir(parents=True, exist_ok=True)
    baseline = WORK / 'baseline'
    binary = WORK / f'astar_benchmark-O{opt}'
    flags = [f'-O{opt}', '-g', '-DNDEBUG', '-std=c++20', '-fno-fast-math', '-ffp-contract=off',
             f'-I{ROOT / "src"}', *extra]
    if baseline_claim_search(baseline):
        flags += [f'-I{baseline}', '-DGLOB2_ASTAR_BASELINE_CLAIMS']
    subprocess.run(['g++', *flags, str(ROOT / 'tools' / 'astar_benchmark.cpp'), '-o', str(binary)], check=True)
    return binary


def capture(arguments) -> None:
    WORK.mkdir(parents=True, exist_ok=True)
    corpus = Path(arguments.out)
    corpus.unlink(missing_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               GLOB2_ASTAR_CAPTURE=str(corpus), GLOB2_ASTAR_CAPTURE_LIMIT=str(arguments.limit))
    for game in arguments.games:
        saved = ROOT / 'content' / 'games' / f'{game}.game.gz'
        print(f'{game}: {arguments.ticks} ticks', file=sys.stderr, flush=True)
        subprocess.run([str(arguments.binary), 'game', 'run', '--load-game', str(saved), '--ticks', str(arguments.ticks),
                        '--compute-threads', '1', '--output-dir', str(WORK / 'runs' / game)],
                       cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
    if not arguments.no_matches:
        for name, players, ticks, seed in MATCHES:
            print(f'{name}: {" vs ".join(players)}, {ticks} ticks', file=sys.stderr, flush=True)
            output = WORK / 'runs' / f'{name}-{seed}'
            shutil.rmtree(output, ignore_errors=True)
            subprocess.run([str(arguments.binary), 'game', 'run', '--map-file', str(ROOT / 'content' / 'maps' / f'{name}.map.gz'),
                            '--game-seed', str(seed), *[a for p in players for a in ('--player', p)],
                            '--ticks', str(ticks), '--win-probability', '0', '--compute-threads', '1',
                            '--output-dir', str(output)],
                           cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
    size = corpus.stat().st_size if corpus.exists() else 0
    print(f'{corpus}: {size} bytes', file=sys.stderr)


def run(arguments) -> None:
    binary = build(arguments.opt, arguments.cxxflags)
    base = [str(binary), str(arguments.corpus), '--repeat', str(arguments.repeat), '--only', arguments.only,
            *shlex.split(arguments.knobs)]
    if arguments.verify:
        subprocess.run([*base, '--verify'], check=True)
        return
    for variant in arguments.variants:
        subprocess.run([*base, '--variant', variant], check=True)


def profile(arguments) -> None:
    binary = build(arguments.opt, arguments.cxxflags)
    out = WORK / f'{arguments.tool}.{arguments.variant}.out'
    tool = ['valgrind', f'--tool={arguments.tool}', f'--{arguments.tool}-out-file={out}']
    if arguments.tool == 'callgrind':
        tool += ['--dump-instr=yes', '--collect-jumps=yes']
    else:
        tool += ['--cache-sim=yes', '--branch-sim=yes']
    subprocess.run([*tool, str(binary), str(arguments.corpus), '--repeat', '1', '--only', arguments.only,
                    '--variant', arguments.variant, *shlex.split(arguments.knobs)], check=True)
    annotate = 'callgrind_annotate' if arguments.tool == 'callgrind' else 'cg_annotate'
    subprocess.run([annotate, '--auto=yes', str(out)] if arguments.tool == 'callgrind' else [annotate, str(out)])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    c = commands.add_parser('capture')
    c.add_argument('--binary', type=Path, required=True)
    c.add_argument('--ticks', type=int, default=1500)
    c.add_argument('--limit', type=int, default=400000)
    c.add_argument('--games', nargs='*', default=list(GAMES))
    c.add_argument('--no-matches', action='store_true', help='only the saved games')
    c.add_argument('--out', default=str(CORPUS))
    for name in ('run', 'profile'):
        p = commands.add_parser(name)
        p.add_argument('--corpus', type=Path, default=CORPUS)
        p.add_argument('--opt', default='2')
        p.add_argument('--repeat', type=int, default=5)
        p.add_argument('--only', default='all')
        p.add_argument('--cxxflags', nargs='*', default=[])
        p.add_argument('--knobs', default='', help='extra benchmark flags, quoted: --knobs "--kernel sse2"')
        if name == 'run':
            p.add_argument('--variants', nargs='*', default=['legacy', 'new', 'reference'])
            p.add_argument('--verify', action='store_true')
        else:
            p.add_argument('--tool', choices=('callgrind', 'cachegrind'), default='callgrind')
            p.add_argument('--variant', default='new')
    arguments = parser.parse_args()
    {'capture': capture, 'run': run, 'profile': profile}[arguments.command](arguments)


if __name__ == '__main__':
    main()
