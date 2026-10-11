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
    """Read bounded format-73..153 MapHeader framing and the player-count prefix.

    Unit definitions in format 153 follow the GameHeader players and resource
    declarations; they do not add MapHeader fields or change this prefix.
    """
    data = gzip.decompress(path.read_bytes())
    position = 0

    def skip(size, field):
        nonlocal position
        assert 0 <= size <= len(data) - position, 'truncated ' + field
        position += size

    def word(field):
        nonlocal position
        assert len(data) - position >= 4, 'truncated ' + field
        value = struct.unpack_from('>I', data, position)[0]
        position += 4
        return value

    def byte(field):
        nonlocal position
        assert position < len(data), 'truncated ' + field
        value = data[position]
        position += 1
        return value

    def text(limit, field, allow_empty=False):
        length = word(field + ' length')
        assert (allow_empty or length > 0) and length <= limit, 'invalid ' + field + ' length'
        skip(length, field)

    def keys(field):
        count = word(field + ' count')
        assert count <= 64, 'invalid ' + field + ' count'
        for _ in range(count):
            text(128, field + ' key')

    text(1024 * 1024, 'map name', allow_empty=True)
    major, minor, teams = word('major version'), word('minor version'), word('team count')
    assert major == 0 and 73 <= minor <= 153, 'unsupported fixture header version'
    assert 0 < teams <= 32, 'invalid team count'
    skip(4, 'map offset')
    assert byte('saved-game flag') <= 1, 'invalid saved-game flag'
    skip(20, 'map SHA1')
    if minor >= 134:
        keys('required terrain experiments')
    if minor >= 140:
        count = word('resource experiment declaration count')
        assert count <= 64, 'invalid resource experiment declaration count'
        for _ in range(count):
            for limit, field in ((128, 'key'), (512, 'label'), (4096, 'help')):
                text(limit, 'resource experiment ' + field)
        keys('required resource experiments')
    skip(20 * teams, 'BaseTeam headers')
    skip(5, 'game latency/order rate')
    if minor >= 143:
        assert byte('AI delay') <= 8, 'invalid AI delay'
    if minor >= 148:
        assert 1 <= byte('building gradient delay') <= 8, 'invalid building gradient delay'
    players = word('player count')
    assert 0 < players <= 32, 'invalid player count'
    return major, minor, teams, players


def header_checksum_delta(before, after, checksum_format=153):
    """Version-only XOR for a specific executed checksum protocol, not save format."""
    assert before[0] == after[0], 'saved major version changed'
    assert after[1] >= before[1], 'saved minor version moved backwards'
    assert before[2:] == after[2:], 'saved team/player counts changed'
    # MapHeader::checkSum rotates major ^ minor ^ teams once. Game::checkSum
    # rotates that contribution once per team/player and four more times.
    # The current unit-catalog checksum adds one final rotation. Both original
    # and resumed states run that protocol even when the input save is older.
    assert checksum_format in (152, 153), 'unsupported checksum protocol'
    delta = before[0] ^ before[1] ^ after[0] ^ after[1]
    rotations = (before[2] + before[3] + 5 + (checksum_format >= 153)) % 32
    if rotations:
        delta = ((delta >> rotations) | (delta << (32 - rotations))) & 0xffffffff
    return delta


def continuation_matches(ticks, records, initial, checkpoint, boundary):
    """Keep aggregate coverage while accounting only for serialized header version."""
    delta = header_checksum_delta(save_header(initial), save_header(checkpoint))
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
        trace = FIXTURE / (name + '-256-units.checksums.gz')
        expected = gzip.decompress(trace.read_bytes())
        ticks = complete_ticks(expected)
        assert set(ticks) == set(range(256)), name + ': incomplete unit-era trace'
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
