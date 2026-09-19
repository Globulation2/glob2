"""Verify retained run counts, byte hashes and cross-platform replay agreement."""
import gzip
import hashlib
import json
import pathlib
import struct

root = pathlib.Path(__file__).resolve().parent
records = [json.loads(line) for line in gzip.open(root / 'requests-and-results.jsonl.gz', 'rt')]
assert len(records) == 3320
assert all(row['outcome']['success'] for row in records)
assert len(json.loads(gzip.decompress((root / 'game-results.json.gz').read_bytes()))) == 20
comparison = json.loads((root / 'compatibility.json').read_text())
blobs = {}
for platform in ['macos', 'linux']:
    for name in ['game.replay', 'game.replay.checksums']:
        data = gzip.decompress((root / f'{platform}-{name}.gz').read_bytes())
        assert hashlib.sha256(data).hexdigest() == comparison[name][platform + '_sha256']
        blobs[platform, name] = data
assert blobs['macos', 'game.replay.checksums'] == blobs['linux', 'game.replay.checksums']
offset = comparison['replay_orders']['stream_offset']
orders = blobs['macos', 'game.replay'][offset:]
assert orders == blobs['linux', 'game.replay'][offset:]
assert hashlib.sha256(orders).hexdigest() == comparison['replay_orders']['sha256']
position = ticks = count = 0
while position < len(orders):
    delta, size = struct.unpack_from('>II', orders, position)
    position += 8 + size + 1 + 4  # order payload, sender, checksum
    ticks += delta
    count += 1
assert position == len(orders) and ticks == 4096 and count == 133
print('PASS 3320 requests, 20 games, matching replay orders and per-tick checksum sidecars')
