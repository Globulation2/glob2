#!/usr/bin/env python3
"""Record what one glob2 build simulates, for comparing two builds with sim_diff.py.

    python3 tools/sim_fingerprint.py build/linux/client/perf/src/glob2 --out base.json.gz
    python3 tools/sim_diff.py base.json.gz cand.json.gz

The bundle holds, per scenario, the build's own output for fixed inputs:

- games: each saved game in content/games run for --ticks ticks headlessly. Per
  tick it keeps the aggregate checksum and, per team, the team checksum and
  digests of its unit and building records (the checksum sidecar).
- match: the FourSquares1 relay match record replayed by `match verify`, its
  per-tick state checksums. A record from another sim version is still replayed
  in full; only its verdict is "unverifiable".
- generators: every registered generator's map fingerprint on the golden
  request set (MapGeneratorGoldenTest --print), keyed by request.

Nothing here is committed: CI fingerprints the pull request's merge base and the
pull request with the same inputs, and sim_diff.py reports the first difference.
Running one build twice must give identical bundles. Scenario inputs always come
from this tool's checkout; each build runs from its own --checkout, so it reads
its own content/data (the file manager searches the working directory first).
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 1
MATCH_RECORD = 'test/fixtures/multiplayer/FourSquares1.g2mr'
MATCH_MAP = 'content/maps/FourSquares1.map.gz'


def headless_environment() -> dict:
    env = dict(os.environ)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    return env


def parse_sidecar(data: bytes) -> list:
    """Per tick: [tick, total, [[team, units digest, buildings digest], ...]]."""
    if data[:4] != b'GCS1':
        raise ValueError('invalid checksum sidecar signature')
    teams, _, count, _ = struct.unpack_from('<4I', data, 4)
    position, ticks = 20, []
    for _ in range(count):
        tick, total = struct.unpack_from('<II', data, position)
        position += 8
        per_team = []
        for _ in range(teams):
            team = struct.unpack_from('<I', data, position)[0]
            position += 4
            digests = []
            for _ in range(2):  # units, then buildings
                entities = struct.unpack_from('<I', data, position)[0]
                start = position
                position += 4
                for _ in range(entities):
                    fields = struct.unpack_from('<I', data, position + 6)[0]
                    position += 10 + 4 * fields
                digests.append(hashlib.sha256(data[start:position]).hexdigest()[:16])
            per_team.append([team, *digests])
        ticks.append([tick, total, per_team])
    if position != len(data):
        raise ValueError('checksum sidecar has extra or truncated data')
    return ticks


def run_game(binary: Path, saved: Path, ticks: int, work: Path, checkout: Path) -> list:
    output = work / saved.name.split('.')[0]
    command = [str(binary), 'game', 'run', '--load-game', str(saved), '--ticks', str(ticks),
               '--compute-threads', '1', '--telemetry', 'checksums', '--write-replay',
               '--output-dir', str(output)]
    log = work / (output.name + '.log')
    with log.open('w') as stream:
        subprocess.run(command, cwd=checkout, stdout=stream, stderr=subprocess.STDOUT,
                       env=headless_environment(), check=True)
    return parse_sidecar((output / 'game.replay.checksums').read_bytes())


def run_match(binary: Path, work: Path, checkout: Path) -> list:
    output = work / 'match'
    command = [str(binary), 'match', 'verify', str(ROOT / MATCH_RECORD), '--map-file', str(ROOT / MATCH_MAP),
               '--output-dir', str(output), '--compute-threads', '1']
    with (work / 'match.log').open('w') as stream:
        # A record from another sim version still replays; the exit status reports the verdict.
        subprocess.run(command, cwd=checkout, stdout=stream, stderr=subprocess.STDOUT, env=headless_environment())
    trace = output / 'checksums.txt'
    if not trace.exists():
        raise RuntimeError(f'match verify wrote no trace; see {work / "match.log"}')
    rows = []
    for line in trace.read_text().splitlines():
        if line and not line.startswith('#'):
            tick, checksum = line.split()
            rows.append([int(tick), int(checksum, 16)])
    return rows


def run_generators(binary_dir: Path, work: Path, checkout: Path) -> dict:
    program = binary_dir / ('MapGeneratorGoldenTest' + ('.exe' if os.name == 'nt' else ''))
    if not program.exists():
        raise RuntimeError(f'{program} is missing; build the map-generator-golden-test target')
    result = subprocess.run([str(program), str(work / 'generator-profile'), '--print'], cwd=checkout,
                            capture_output=True, text=True, env=headless_environment(), check=True)
    rows = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        # platform id revision wDec hDec teams seed status hash
        if len(fields) != 9 or line.startswith('#'):
            continue
        _, generator, _, w, h, teams, seed, status, value = fields
        rows[f'{generator} {w}x{h} teams={teams} seed={seed}'] = f'{status} {value}'
    if not rows:
        raise RuntimeError('MapGeneratorGoldenTest --print produced no rows')
    return rows


def fingerprint(binary: Path, ticks: int, parts: set[str], checkout: Path = ROOT) -> dict:
    bundle = {'schema': SCHEMA, 'ticks': ticks, 'scenarios': {}}
    with tempfile.TemporaryDirectory(prefix='glob2-sim-fingerprint-') as temporary:
        work = Path(temporary)
        if 'games' in parts:
            for saved in sorted((ROOT / 'content/games').glob('*.game.gz')):
                name = 'game/' + saved.name.split('.')[0]
                print(f'{name}: {ticks} ticks', file=sys.stderr, flush=True)
                bundle['scenarios'][name] = {'kind': 'game', 'ticks': run_game(binary, saved, ticks, work, checkout)}
        if 'match' in parts:
            print('match/FourSquares1', file=sys.stderr, flush=True)
            bundle['scenarios']['match/FourSquares1'] = {'kind': 'match', 'ticks': run_match(binary, work, checkout)}
        if 'generators' in parts:
            print('generators', file=sys.stderr, flush=True)
            bundle['scenarios']['generators'] = {'kind': 'generators',
                                                 'maps': run_generators(binary.parent, work, checkout)}
    return bundle


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('binary', type=Path, help='the glob2 executable of the build to fingerprint')
    parser.add_argument('--out', type=Path, required=True, help='bundle path (.json.gz)')
    parser.add_argument('--ticks', type=int, default=600, help='ticks to run each saved game')
    parser.add_argument('--checkout', type=Path, default=ROOT,
                        help="the build's own source checkout, to run it from (default: this one)")
    parser.add_argument('--parts', default='games,match,generators',
                        help='comma-separated subset of games, match, generators')
    args = parser.parse_args(argv)
    parts = set(args.parts.split(','))
    unknown = parts - {'games', 'match', 'generators'}
    if unknown:
        parser.error(f'unknown parts: {sorted(unknown)}')
    bundle = fingerprint(args.binary.resolve(), args.ticks, parts, args.checkout.resolve())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with gzip.open(args.out, 'wt') as stream:
        json.dump(bundle, stream, sort_keys=True, separators=(',', ':'))
    print(f'{len(bundle["scenarios"])} scenarios -> {args.out}', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
