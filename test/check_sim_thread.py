"""Simulation-equivalence check for the simulation-thread work.

Runs the same scenarios with a baseline and a candidate executable (optionally with
different extra engine arguments) and requires byte-identical per-tick checksum
sidecars, replays and final saves. Scenarios cover new games (RNG seeding), generated
maps, save continuation (RNG restore) and a legacy save fixture.

    python3 test/check_sim_thread.py CANDIDATE --baseline BASELINE [--output DIR]
"""
import argparse
import os
import tempfile
from pathlib import Path

from benchmark_parallel_compute import execute
from check_telemetry_simulation import detailed_ticks

ROOT = Path(__file__).resolve().parents[1]
OUTPUTS = ('game.replay.checksums', 'game.replay', 'final.game')


def scenarios(work, baseline):
    generated = work / 'generated-fixture'
    execute(baseline, ['--generator', '15', '--map-seed', '4242', '--param', 'width=7',
                       '--param', 'height=7', '--param', 'teams=4', '--game-seed', '19',
                       '--player', 'maxima', '--player', 'cortex', '--player', 'nicowar',
                       '--player', 'maxima', '--ticks', '1', '--save', 'initial'], generated)
    yield 'new-game', ['--map-file', str(ROOT / 'maps/FourSquares1.map.gz'), '--game-seed', '123',
                       '--player', 'econo', '--player', 'nicowar', '--player', 'castor',
                       '--player', 'numbi', '--ticks', '1500']
    yield 'new-game-maxima', ['--map-file', str(ROOT / 'maps/balanced.map.gz'), '--game-seed', '7',
                              '--player', 'maxima', '--player', 'cortex', '--player', 'warrush',
                              '--player', 'cabino', '--ticks', '1500']
    yield 'generated-load', ['--load-game', str(generated / 'initial.game'), '--ticks', '1500']
    yield 'legacy-v121', ['--load-game', str(ROOT / 'test/fixtures/echo/v121-shared-gradient-256.game.gz'),
                          '--ticks', '768']


def run(binary, args, extra, output):
    execute(binary, args + extra + ['--telemetry', 'checksums', '--replay', 'true', '--save', 'final'], output)
    return output


def read(output, name):
    path = output / name
    if not path.exists():
        path = Path(str(path) + '.gz')
    return path.read_bytes()


def compare(reference, repeat, candidate):
    """Exact comparison, except replay bytes that differ between two baseline runs.

    Some replay headers carry bytes that vary run to run (pre-existing; e.g. an
    unset map SHA1 for maps loaded without one). Those positions are reported and
    excluded; every other byte, the checksum sidecar and the final save must match.
    """
    problems, noise = [], 0
    for name in OUTPUTS:
        a, b, c = read(reference, name), read(repeat, name), read(candidate, name)
        if name == 'game.replay' and len(a) == len(b) == len(c):
            # A varying byte marks its whole 4-byte neighbourhood: two runs can agree on
            # some bytes of a varying field by chance.
            varying = {j for i in range(len(a)) if a[i] != b[i] for j in range(i - 3, i + 4)}
            noise = len(varying)
            if any(a[i] != c[i] for i in range(len(a)) if i not in varying):
                problems.append(name)
        elif a != c:
            problems.append(name)
    return problems, noise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate-args', default='', help='extra engine arguments for the candidate')
    parser.add_argument('--candidate-env', action='append', default=[],
                        help='KEY=VALUE environment for the candidate only (e.g. GLOB2_SIM_THREAD=1)')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    binary, baseline = args.binary.resolve(), args.baseline.resolve()
    temporary = tempfile.TemporaryDirectory(prefix='glob2-simthread-') if not args.output else None
    work = Path(temporary.name) if temporary else args.output.resolve()
    work.mkdir(parents=True, exist_ok=True)
    extra = args.candidate_args.split()
    candidate_env = dict(item.split('=', 1) for item in args.candidate_env)
    failures = []
    def check(label, scenario):
        reference = run(baseline, scenario, [], work / f'{label}-baseline')
        repeat = run(baseline, scenario, [], work / f'{label}-baseline-repeat')
        saved = {k: os.environ.get(k) for k in candidate_env}
        os.environ.update(candidate_env)
        try:
            candidate = run(binary, scenario, extra, work / f'{label}-candidate')
        finally:
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        problems, noise = compare(reference, repeat, candidate)
        if problems:
            a = detailed_ticks(read(reference, 'game.replay.checksums'))
            b = detailed_ticks(read(candidate, 'game.replay.checksums'))
            first = next((t for t in sorted(a) if a[t] != b.get(t)), None)
            failures.append(f'{label}: {", ".join(problems)} differ (first differing tick: {first})')
        note = f' ({noise} run-varying replay bytes excluded)' if noise else ''
        print(f'{label}: {"DIFFERS " + ", ".join(problems) if problems else "ok"}{note}', flush=True)

    for label, scenario in scenarios(work, baseline):
        check(label, scenario)
    # Save continuation: both executables resume the baseline's saved game (restoring
    # its synchronized RNG state) and must stay identical.
    check('resume', ['--load-game', str(work / 'new-game-baseline/final.game'), '--ticks', '1800'])
    if failures:
        raise SystemExit('FAIL\n' + '\n'.join(failures))
    print('PASS simulation equivalence: checksum sidecars, replay bytes and final saves match')


if __name__ == '__main__':
    main()
