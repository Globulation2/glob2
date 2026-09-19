#!/usr/bin/env python3
"""Check retained evidence without requiring a game build."""
from pathlib import Path
import collections
import gzip
import hashlib
import json

root = Path(__file__).resolve().parent
for name, expected in [('ablation', (654, 51)), ('random', (1719, 2281)), ('shapes', (135, 441))]:
    rows = [json.loads(line) for line in gzip.open(root / (name + '.jsonl.gz'), 'rt')]
    counts = collections.Counter(row['category'] for row in rows)
    assert counts == {'completed': expected[0], 'refused': expected[1]}, counts
    for row in rows:
        status = row['native']['status'] if name == 'shapes' else row['native_status']
        assert status == ('completed' if row['category'] == 'completed' else 'invalid_request')
        if name == 'shapes':
            assert (row['category'] == 'completed') == row['expected_supported']
    print(name, dict(counts))
rows = [json.loads(line) for line in (root / 'profile-world-comparison.jsonl').read_text().splitlines()]
assert len(rows) == 96 and all(row['before'] == row['after'] and row['identical'] for row in rows)
for platform in ('macos', 'linux'):
    data = gzip.open(root / (platform + '.checksums.gz'), 'rb').read()
    assert hashlib.sha256(data).hexdigest() == 'b3221954eb854209b0a8c6602ffd9b38fa4c0344cd9d2b74df60750a3184f364'
summary = json.loads((root / 'games/games-summary.json').read_text())
for name in sorted({row['game'] for row in summary}):
    result = json.loads((root / 'games' / (name + '-result.json')).read_text())
    rows = json.load(gzip.open(root / 'games' / (name + '-measurements.json.gz'), 'rt'))
    finals = [row for row in rows if row['final'] == 1]
    assert result['status'] == 'completed'
    assert len(finals) == 4 and {row['team'] for row in finals} == set(range(4))
    assert all(row['tick'] == result['ticks'] for row in finals)
    print(name, result['ticks'], 'complete final counters')
print('PASS retained parameter, topology-preservation, compatibility and game evidence')
