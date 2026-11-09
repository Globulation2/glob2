#!/usr/bin/env python3
"""Generate one generator across map shapes and colony counts, three seeds each, in parallel, and
print the cells that did not all succeed with their refusal messages. Run from the repository root:

  python3 .agents/skills/glob2-map-design/scripts/sweep_shapes.py glacis [--set key=value ...]

A successful map generate prints no "Map command" failure line; a refusal prints
"[validation]: <message>".
"""
import argparse, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tools.map_generation_study import default_binary, default_jobs, child_threads

SHAPES = ((64, 64), (128, 128), (256, 256), (512, 512), (512, 256), (256, 512), (128, 512), (256, 128))
TEAMS = (1, 2, 3, 4, 6, 8, 12)
SEEDS = (1, 2, 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('generator')
    ap.add_argument('--set', action='append', default=[], help='generator control KEY=VALUE')
    ap.add_argument('--binary', help='native client (default: the newest build, perf builds included)')
    ap.add_argument('--jobs', type=int, help='concurrent maps (default: one per logical CPU)')
    a = ap.parse_args()
    binary, jobs = a.binary or default_binary(), a.jobs or default_jobs()
    threads = child_threads(jobs)
    extra = [v for kv in a.set for v in ('--set', kv)]
    cells = [(w, h, teams, seed) for w, h in SHAPES for teams in TEAMS for seed in SEEDS]
    with tempfile.TemporaryDirectory() as out:
        def run(cell):
            w, h, teams, seed = cell
            command = [binary, 'map', 'generate', a.generator, '--seed', str(seed), '--width', str(w),
                       '--height', str(h), '--teams', str(teams), '--compute-threads', str(threads),
                       *extra, '--output', f'{out}/{w}x{h}-{teams}-{seed}.map']
            result = subprocess.run(command, capture_output=True, text=True)
            failure = [line for line in (result.stdout + result.stderr).splitlines() if 'Map command' in line]
            return failure[0].split(']: ', 1)[1] if failure and ']:' in failure[0] else None
        # Largest maps first, so the slowest generations do not trail at the end.
        order = sorted(range(len(cells)), key=lambda i: -cells[i][0] * cells[i][1])
        with ThreadPoolExecutor(jobs) as pool:
            done = dict(zip(order, pool.map(lambda i: run(cells[i]), order)))
    for w, h in SHAPES:
        for teams in TEAMS:
            refusals = [done[cells.index((w, h, teams, seed))] for seed in SEEDS]
            failures = [r for r in refusals if r is not None]
            if failures:
                print(f'{w}x{h} {teams} colonies: {len(SEEDS) - len(failures)}/{len(SEEDS)}'
                      + ''.join(f' | {r}' for r in failures))


if __name__ == '__main__':
    main()
