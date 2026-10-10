"""Complete JavaScript traces, worker equivalence and multiple saved continuations."""
import argparse
import gzip
import hashlib
import json
import subprocess
import struct
from pathlib import Path
from check_telemetry_simulation import detailed_ticks

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'test/fixtures/javascript'


def complete_ticks(data):
    """Include the aggregate checksum (map, scripts and counters), not just entities."""
    detailed = detailed_ticks(data)  # also validates all framing and lengths
    position, records = 20, {}
    for tick, fields in detailed.items():
        stored_tick, checksum = struct.unpack_from('<II', data, position)
        assert stored_tick == tick
        records[tick] = data[position:position + 8 + len(fields)]
        position += 8 + len(fields)
    assert position == len(data)
    return records


def run(binary, output, saved, workers, interval=None):
    output.mkdir(parents=True, exist_ok=False)
    command = [str(binary), 'game', 'run', '--load-game', str(saved), '--ticks', '256',
               '--compute-threads', str(workers), '--telemetry', 'checksums',
               '--write-replay', '--save', 'final', '--output-dir', str(output)]
    if interval:
        command += ['--save', 'every:' + str(interval)]
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (output / 'engine.log').open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    return (output / 'game.replay.checksums').read_bytes()


def compare_payloads(whole, resumed):
    whole, resumed = gzip.decompress(whole), gzip.decompress(resumed)
    # MapHeader SHA1 is overwritten on each save and depends on save history.
    # Find it using the header's encoded map-name length rather than a fixed
    # offset: realistic and original fixtures have different names.
    name_length = struct.unpack_from('>I', whole, 0)[0]
    resumed_length = struct.unpack_from('>I', resumed, 0)[0]
    assert name_length == resumed_length
    sha1 = 4 + name_length + 4 * 4 + 1  # name, major/minor, team count, map offset, saved
    assert whole[:sha1] == resumed[:sha1] and whole[sha1 + 20:] == resumed[sha1 + 20:]


def save_header(path):
    """Read the modern fixture's MapHeader and GameHeader player count."""
    data = gzip.decompress(path.read_bytes())
    name_length = struct.unpack_from('>I', data)[0]
    major, minor, teams = struct.unpack_from('>3I', data, 4 + name_length)
    assert minor >= 73, 'fixture predates fixed-size BaseTeam headers'
    # Version134 appends the required-terrain experiment keys before BaseTeams.
    game_header = 4 + name_length + 16 + 1 + 20
    if minor >= 134:
        count = struct.unpack_from('>I', data, game_header)[0]
        game_header += 4
        assert count <= 256, 'invalid required terrain experiment count'
        for _ in range(count):
            length = struct.unpack_from('>I', data, game_header)[0]
            game_header += 4 + length
            assert game_header <= len(data), 'truncated terrain experiment key'
    if minor >= 140:
        # Resource experiment declarations precede their required enabled keys.
        # Metadata is bounded by the native catalog transport contract.
        count = struct.unpack_from('>I', data, game_header)[0]
        game_header += 4
        assert count <= 64, 'invalid resource experiment declaration count'
        for _ in range(count):
            for limit in (128, 512, 4096):
                length = struct.unpack_from('>I', data, game_header)[0]
                assert 0 < length <= limit, 'invalid resource experiment metadata length'
                game_header += 4 + length
                assert game_header <= len(data), 'truncated resource experiment metadata'
        count = struct.unpack_from('>I', data, game_header)[0]
        game_header += 4
        assert count <= 64, 'invalid required resource experiment count'
        for _ in range(count):
            length = struct.unpack_from('>I', data, game_header)[0]
            assert 0 < length <= 128, 'invalid required resource experiment key length'
            game_header += 4 + length
            assert game_header <= len(data), 'truncated resource experiment key'
    game_header += 20 * teams
    # Version 143 inserts uint8 aiOrderDelay after latency and order rate;
    # version 148 follows it with uint8 buildingGradientDelay.
    players_offset = game_header + 5 + (1 if minor >= 143 else 0) + (1 if minor >= 148 else 0)
    players = struct.unpack_from('>I', data, players_offset)[0]
    assert 0 < teams <= 32 and 0 < players <= 32
    return major, minor, teams, players


def continuation_matches(ticks, records, initial, checkpoint, boundary):
    """Keep aggregate coverage while accounting only for serialized header version."""
    before, after = save_header(initial), save_header(checkpoint)
    assert before[0] == after[0], 'saved major version changed'
    assert after[1] >= before[1], 'saved minor version moved backwards'
    assert before[2:] == after[2:], 'saved team/player counts changed'
    # MapHeader::checkSum rotates major ^ minor ^ teams once. Game::checkSum
    # rotates that contribution once per team/player and four more times.
    delta = before[0] ^ before[1] ^ after[0] ^ after[1]
    rotations = (before[2] + before[3] + 5) % 32
    if rotations:
        delta = ((delta >> rotations) | (delta << (32 - rotations))) & 0xffffffff
    if set(records) != set(range(boundary, 256)):
        return False
    for tick, actual in records.items():
        expected = ticks[tick]
        if actual[:4] != expected[:4] or actual[8:] != expected[8:]:
            return False
        checksum = struct.unpack_from('<I', expected, 4)[0] ^ delta
        if struct.unpack_from('<I', actual, 4)[0] != checksum:
            return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', choices=('profile1', 'realistic-profile1', 'all'), default='all')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'fixtures': {},
                'checks': [], 'excludedSaveMetadata': 'MapHeader SHA1 only (depends on save history)',
                'continuationChecksumAdjustment': 'MapHeader format-version contribution only'}
    names = ('profile1', 'realistic-profile1') if args.fixture == 'all' else (args.fixture,)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    for name in names:
        released = gzip.decompress((FIXTURE / (name + '-256.checksums.gz')).read_bytes())
        expanded = gzip.decompress((FIXTURE / (name + '-256-teams16.checksums.gz')).read_bytes())
        released_ticks, expanded_ticks = complete_ticks(released), complete_ticks(expanded)
        assert released_ticks.keys() == expanded_ticks.keys()
        assert all(record[8:] == expanded_ticks[tick][8:]
                   for tick, record in released_ticks.items()), name + ': released entity records differ'
        # Preserve historical migration evidence independently of the current
        # simulation trace. Still load the original version-125 save below.
        trace = FIXTURE / (name + '-256-resources.checksums.gz')
        expected = gzip.decompress(trace.read_bytes())
        ticks = complete_ticks(expected)
        assert set(ticks) == set(range(256)), name + ': incomplete resource trace'
        initial = FIXTURE / (name + '-initial.game.gz')
        manifest['fixtures'][name] = {'initialSha256': hashlib.sha256(initial.read_bytes()).hexdigest(),
                                     'trace': str(trace.relative_to(ROOT)),
                                     'releasedTraceSha256': hashlib.sha256(released).hexdigest(),
                                     'teams16TraceSha256': hashlib.sha256(expanded).hexdigest(),
                                     'traceSha256': hashlib.sha256(expected).hexdigest()}
        interval = 32 if name == 'realistic-profile1' else 128
        baseline = output / name / 'workers1'
        actual = run(binary, baseline, initial, 1, interval)
        assert actual == expected, name + ': complete frozen trace differs'
        parallel = output / name / 'workers4'
        assert run(binary, parallel, initial, 4, interval) == expected
        for filename in ('game.replay', 'game.replay.checksums', 'final.game.gz'):
            assert (baseline / filename).read_bytes() == (parallel / filename).read_bytes(), filename
        # Include either side of the 32-tick scenario callback/AI cadence, using
        # several saved boundaries rather than only the halfway checkpoint.
        boundaries = (32, 64, 96, 128, 224) if name == 'realistic-profile1' else (128,)
        for boundary in boundaries:
            tail = output / name / ('resumed-' + str(boundary))
            resumed = run(binary, tail, baseline / f'checkpoint-{boundary}.game.gz', 4)
            records = complete_ticks(resumed)
            assert continuation_matches(ticks, records, initial,
                                        baseline / f'checkpoint-{boundary}.game.gz', boundary), \
                f'{name}: continuation differs at boundary {boundary}'
            compare_payloads((baseline / 'final.game.gz').read_bytes(), (tail / 'final.game.gz').read_bytes())
        manifest['checks'].append({'fixture': name, 'ticks': 256, 'workers': [1, 4], 'resumeBoundaries': list(boundaries)})
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'PASS {name}: complete tick checksums, exact 1/4-worker replay/save output, '
              f'{len(boundaries)} saved continuations', flush=True)


if __name__ == '__main__':
    main()
