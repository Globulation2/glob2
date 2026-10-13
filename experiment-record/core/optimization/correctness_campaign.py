#!/usr/bin/env python3
"""Plan-only release conformance campaign; --run explicitly launches serial games.
Diagnostic tracing/replay IO makes these cases unsuitable for performance claims.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
import mmap
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ART = Path(__file__).resolve().parent
MASK = [12, 13, 14, 28, 29, 30]
MAX_REPLAY = 512 * 1024 * 1024

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for part in iter(lambda: f.read(1024 * 1024), b''):
            h.update(part)
    return h.hexdigest()

def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(value, indent=2) + '\n')
    tmp.replace(path)

def require(ok, why):
    if not ok:
        raise ValueError(why)

def save_header(path):
    # MapHeader::save and Map::load: network-order prefix and explicit mapOffset.
    with gzip.open(path, 'rb') as f:
        data = f.read(MAX_REPLAY + 1)
    require(len(data) <= MAX_REPLAY, 'save exceeds bounded reader')
    n = struct.unpack_from('>I', data)[0]
    require(n <= 4096 and 4+n+17 <= len(data), 'invalid map name length')
    major, minor, teams, offset = struct.unpack_from('>4I', data, 4+n)
    saved = data[20+n]
    require(saved == 1 and data[offset:offset+4] == b'MapB', 'invalid saved map offset/signature')
    wd, hd = struct.unpack_from('>2I', data, offset+4)
    require(5 <= wd <= 9 and 5 <= hd <= 9, 'invalid saved dimensions')
    return dict(major=major, minor=minor, teams=teams, width=1 << wd, height=1 << hd,
                map_offset=offset, gzip_sha256=sha(path))

def ait1(path):
    # Bounded structural EOF validator; does not retain a Python value graph.
    require(path.stat().st_size <= MAX_REPLAY, 'replay exceeds bounded reader')
    with path.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        def candidate(at):
            pos = at
            budget = 16*1024*1024
            def take(n):
                nonlocal pos
                require(n >= 0 and pos+n <= len(data), 'truncated AIT1')
                p = pos
                pos += n
                return p
            def u():
                return struct.unpack_from('>I', data, take(4))[0]
            def boolean():
                v = data[take(1)]
                require(v in (0, 1), 'noncanonical AIT1 bool')
                return v
            def spend(n):
                nonlocal budget
                budget -= n
                require(budget >= 0, 'AIT1 logical budget exceeded')
            def text():
                n = u()
                require(n <= 4096, 'AIT1 text limit')
                spend(n)
                take(n)
            require(u() == 0x41495431 and u() == 1, 'AIT1 version')
            until, truncated, count = u(), boolean(), u()
            require(count <= 65536 and 8*count <= len(data)-pos, 'AIT1 frame bound')
            previous = -1
            row_count = values = 0
            for _ in range(count):
                spend(8)
                tick, rows = u(), u()
                require(previous < tick <= until and rows <= 32, 'AIT1 frame order/count')
                previous = tick
                row_count += rows
                for _ in range(rows):
                    spend(32)
                    require(u() < 16 and u() < 16, 'AIT1 team/player')
                    text()
                    boolean()
                    fields = u()
                    require(fields <= 4228, 'AIT1 value bound')
                    spend(24*fields)
                    values += fields
                    for _ in range(fields):
                        for _ in range(4):
                            text()
                        u()
            require(pos == len(data), 'AIT1 trailing bytes')
            h = hashlib.sha256()
            for p in range(at, len(data), 1024*1024):
                h.update(data[p:min(p+1024*1024, len(data))])
            return dict(offset=at, bytes=len(data)-at, sha256=h.hexdigest(), frames=count,
                        rows=row_count, values=values, until=until, truncated=bool(truncated))
        good = []
        pos = 0
        for _ in range(129):
            at = data.find(b'AIT1', pos)
            if at < 0:
                break
            pos = at+1
            try:
                good.append(candidate(at))
            except (ValueError, struct.error):
                pass
        else:
            raise ValueError('AIT1 magic candidate limit')
        require(len(good) == 1, f'expected one valid EOF AIT1 tail, found {len(good)}')
        return good[0]

def trace(path):
    # Imported canonical detailed-record parser validates complete EOF/counts.
    with path.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        teams, players, count, flags = struct.unpack_from('<4I', data, 4)
        first = last = None
        seen = 0
        for tick, _ in CONT.records(data):
            first = tick if first is None else first
            last = tick
            seen += 1
        require(seen == count and count > 0, 'empty/inconsistent GCS trace')
        return dict(sha256=sha(path), teams=teams, players=players, count=count,
                    first_tick=first, last_tick=last, flags=flags)

def world_trace(path, first, end):
    # EngineRun emits decimal tick/full-world checksum before incrementing tick.
    count = 0
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for line in stream:
            require(len(line) <= 64, 'oversized world checksum line')
            fields = line.split()
            require(len(fields) == 2 and all(v.isdigit() for v in fields),
                    'invalid decimal world checksum record')
            tick, checksum = map(int, fields)
            require(tick == first+count and 0 <= checksum <= 0xffffffff,
                    'nonconsecutive/invalid world checksum record')
            count += 1
            require(count <= end-first, 'extra world checksum records')
            h.update(line)
    require(count == end-first, 'incomplete world checksum coverage')
    return dict(sha256=h.hexdigest(), count=count, first_tick=first, last_tick=end-1,
                scope='per-tick full world/resource/pending proposal checksum; decimal source format')

def clean_env(directory):
    env = {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_')}
    # Isolated settings and writable data; no inherited benchmark/allocator knobs.
    env.update(GLOB2_USER_DIR=str(directory/'profile'),
               GLOB2_USER_DATA_DIR=str(directory/'profile'), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy')
    return env

def execute(binary, directory, args):
    require(not directory.exists(), f'fresh directory required: {directory}')
    directory.mkdir(parents=True)
    command = ['taskset', '-c', ','.join(map(str, MASK)), str(binary), 'game', 'run',
               '--profile', 'optimization-correctness', '--output-dir', str(directory)] + args
    record = dict(command=command, binary_sha256=sha(binary), affinity=MASK,
                  environment_overrides=clean_env(directory), scope='correctness; no timing claims')
    # Do not record unrelated credentials inherited in the process environment.
    record['environment_overrides'] = {k: v for k, v in clean_env(directory).items()
                                       if k.startswith('GLOB2_') or k.startswith('SDL_')}
    write(directory/'invocation.json', record)
    with (directory/'stdout.log').open('wb') as out, (directory/'stderr.log').open('wb') as err:
        result = subprocess.run(command, cwd=binary.parents[5], env=clean_env(directory),
                                stdout=out, stderr=err, timeout=3600)
    record['returncode'] = result.returncode
    write(directory/'invocation.json', record)
    require(result.returncode == 0, f'engine failed: {directory}; inspect retained logs')
    report = json.loads((directory/'result.json').read_text())
    require(report['status'] == 'completed', f'job failed: {directory}')
    return report

def validate_result(result, scenario, seed):
    require(result['game_seed'] == seed, 'game seed changed')
    require(len(result['teams']) == len(scenario['ais']), 'team count changed')
    require([(p['player'], p['team'], p['ai']) for p in result['players']] ==
            [(i, i, ai) for i, ai in enumerate(scenario['ais'])], 'controller setup changed')

def run_case(binary, directory, fixture, scenario, seed, count, gpu, ticks, save):
    result = execute(binary, directory, ['--load-game', str(fixture), '--ticks', str(ticks),
                     '--compute-threads', str(count), '--gpu', gpu, '--telemetry', 'checksums',
                     '--write-replay', '--save', save])
    validate_result(result, scenario, seed)
    if result['ticks'] != ticks or result['termination'] != 'tick_cap':
        write(directory/'early-ending.json', dict(requested_tick=ticks, actual_tick=result['ticks'],
              termination=result['termination'], action='root must choose a paired window explicitly'))
        raise ValueError(f'early ending/classification required: {directory}')
    final_header = save_header(directory/'final.game.gz')
    require((final_header['width'], final_header['height'], final_header['teams']) ==
            (scenario['width'], scenario['height'], len(scenario['ais'])), 'final save geometry changed')
    record = dict(final_header=final_header, result={k: result[k] for k in ('ticks', 'initialChecksum', 'finalChecksum',
                                               'termination', 'winning_teams') if k in result},
                  gcs=trace(directory/'game.replay.checksums'), ait1=ait1(directory/'game.replay'),
                  replay_sha256=sha(directory/'game.replay'),
                  final_gzip_sha256=sha(directory/'final.game.gz'))
    # EngineRun writes state at tick t before syncStep increments to t+1.
    require(record['gcs']['last_tick'] == ticks-1 and
            record['gcs']['count'] == ticks-record['gcs']['first_tick'], 'GCS missing tick coverage')
    record['world'] = world_trace(directory/'world.checksums', record['gcs']['first_tick'], ticks)
    write(directory/'validated.json', record)
    return record

def same(actual, expected, label):
    # Exact aggregate+detailed stream, AIT1 and whole replay bytes across matched starts.
    for key in ('gcs', 'world', 'ait1', 'replay_sha256', 'result'):
        require(actual[key] == expected[key], f'{label}: genuine {key} divergence')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--resume', action='store_true', help='Skip sealed passed fixtures only')
    parser.add_argument('--campaign-name', default='correctness', help='Separate discovery evidence directory')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--scheduler-metadata', type=Path, help='Optional built scheduler JSON with state/binary/binary_sha256')
    parser.add_argument('--only-fixture', help='ID-SEED; bounded discovery/diagnosis')
    args = parser.parse_args()
    plan = json.loads((ART/'acceptance-plan.json').read_text())
    config = json.loads((ART/'campaign.json').read_text())
    maps = {m['id']: m for m in json.loads((ART/'map-preparation.json').read_text())}
    variants = json.loads((ART/'variant-builds.json').read_text())
    binaries = {'baseline': Path(config['baseline_binary']),
                'combined': ROOT/'build/linux/client/release/src/glob2'}
    binaries.update({name: Path(v['root'])/'build/linux/client/release/src/glob2'
                     for name, v in variants['variants'].items()})
    if args.scheduler_metadata:
        scheduler = json.loads(args.scheduler_metadata.read_text())
        require(scheduler['state'] == 'complete', 'optional scheduler must already be built')
        binaries['scheduler'] = Path(scheduler['binary'])
        require(sha(binaries['scheduler']) == scheduler['binary_sha256'], 'scheduler hash changed')
    design = dict(fixtures=20, matched_runs_per_fixture=14 + (2 if args.scheduler_metadata else 0),
                  continuation_runs_per_fixture=6 + bool(args.scheduler_metadata), ticks=2048, affinity=MASK,
                  baseline_and_combined_counts=[1, 3, 6, 'auto'], individual_counts=[3],
                  gpu='off all; on baseline/combined/snapshot and optional scheduler at3',
                  notes=['No engine launches without --run.', 'All inputs immutable and hashed.',
                         'Detailed continuation ignores aggregate save/header checksums.',
                         'No performance conclusions from trace/replay cases.'])
    write(ART/'correctness-plan.json', design)
    if not args.run:
        print(json.dumps(design, indent=2))
        return
    require(variants['state'] == 'complete', 'all one-factor builds must finish first')
    require(sha(binaries['baseline']) == config['baseline_binary_sha256'], 'baseline binary changed')
    for name, binary in binaries.items():
        require(binary.is_file(), f'missing {name} binary')
        if name in variants['variants']:
            require(sha(binary) == variants['variants'][name]['binary_sha256'], f'{name} binary changed')
    write(ART/'correctness-binaries.json', {name: dict(path=str(binary), sha256=sha(binary))
                                           for name, binary in binaries.items()})
    require(args.campaign_name not in ('', '.', '..') and '/' not in args.campaign_name, 'invalid campaign name')
    campaign = ART/args.campaign_name
    sealed = dict(script_sha256=sha(Path(__file__)),
                  binaries={name: sha(binary) for name, binary in binaries.items()},
                  acceptance_plan_sha256=sha(ART/'acceptance-plan.json'),
                  map_preparation_sha256=sha(ART/'map-preparation.json'))
    for scenario in plan['scenario_design']:
        for seed in plan['new_game_seeds']:
            key = f"{scenario['id']}-{seed}"
            if args.only_fixture and key != args.only_fixture:
                continue
            folder = campaign/key
            mapinfo = maps[scenario['id']]
            require(sha(mapinfo['path']) == mapinfo['sha256'], 'frozen map hash changed')
            identity = dict(sealed=sealed, map_sha256=mapinfo['sha256'], scenario=scenario, seed=seed)
            passed = folder/'passed.json'
            if args.resume and passed.exists():
                saved = json.loads(passed.read_text())
                require(saved['identity'] == identity, f'resume identity changed: {key}')
                require(sha(folder/'fixture/initial.game.gz') == saved['fixture_sha256'], 'passed fixture changed')
                print('SKIP sealed PASS', key, flush=True)
                continue
            fixture_dir = folder/'fixture'
            result = execute(binaries['baseline'], fixture_dir,
                             ['--map-file', mapinfo['path'], '--game-seed', str(seed),
                              '--ticks', '1', '--compute-threads', '1', '--gpu', 'off',
                              '--telemetry', 'checksums', '--save', 'initial'] +
                             [item for ai in scenario['ais'] for item in ('--player', ai)])
            validate_result(result, scenario, seed)
            fixture = fixture_dir/'initial.game.gz'
            header = save_header(fixture)
            require((header['width'], header['height'], header['teams']) ==
                    (scenario['width'], scenario['height'], len(scenario['ais'])), 'fixture geometry changed')
            initial_trace = trace(fixture_dir/'game.replay.checksums')
            require(result['ticks'] == 1 and initial_trace['first_tick'] in (0, 1), 'fixture initial tick invalid')
            write(folder/'fixture.json', dict(path=str(fixture), header=header, start_tick=0,
                  start_tick_evidence='save occurs before first simulation tick; loaded trace checked below',
                  preparation_trace=initial_trace, scenario=scenario, seed=seed))
            if args.prepare_only:
                continue
            baseline = {}
            for gpu in ('off', 'on'):
                for name, binary in binaries.items():
                    counts = [1, 3, 6, 'auto'] if name in ('baseline', 'combined') else [3]
                    if gpu == 'on':
                        counts = [3] if name in ('baseline', 'combined', 'snapshot', 'scheduler') else []
                    for count in counts:
                        label = f'{name}-t{count}-gpu{gpu}'
                        actual = run_case(binary, folder/label, fixture, scenario, seed,
                                          count, gpu, 2048, 'final')
                        require(actual['gcs']['first_tick'] == initial_trace['first_tick'], 'loaded start tick changed')
                        if gpu not in baseline:
                            baseline[gpu] = actual
                            if gpu == 'on':
                                same(actual, baseline['off'], 'GPUon vs GPUoff baseline arithmetic')
                        else:
                            same(actual, baseline[gpu], label)
            # Continue each variant from its t3 final and compare with baseline
            # uninterrupted 4096. Entity records exclude header-dependent aggregate CRC.
            require(sha(fixture) == header['gzip_sha256'], 'initial fixture changed')
            full_dir = folder/'baseline-full4096'
            run_case(binaries['baseline'], full_dir, fixture, scenario, seed, 3, 'off', 4096, 'final')
            for name, binary in binaries.items():
                final = folder/f'{name}-t3-gpuoff'/'final.game.gz'
                resumed = folder/f'{name}-resumed4096'
                run_case(binary, resumed, final, scenario, seed, 3, 'off', 4096, 'final')
                matched = CONT.compare(full_dir/'game.replay.checksums', resumed/'game.replay.checksums')
                write(resumed/'continuation.json', dict(exact_detailed_records=matched,
                      aggregate_excluded='save format/header included in aggregate checksum', passed=True))
            write(folder/'passed.json', dict(passed=True, fixture_sha256=sha(fixture), identity=identity))
            print('PASS', key, flush=True)
    write(campaign/'complete.json', dict(passed=True, selected_fixture=args.only_fixture))

spec = importlib.util.spec_from_file_location('save_continuation', ROOT/'test/compare_save_continuation.py')
CONT = importlib.util.module_from_spec(spec)
spec.loader.exec_module(CONT)
if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        write(ART/'correctness-failure.json', dict(error=str(error), exception=type(error).__name__))
        raise
