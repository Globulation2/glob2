"""Validate the original textured swarm conversion and provenance, without Blender."""
import hashlib
import json
import math
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
folder = root / 'data/models3d'
metadata = json.loads((folder / 'swarm.json').read_text())
source = (root / metadata['source']).read_bytes()
assert source[:4] == b'glTF'
assert hashlib.sha256(source).hexdigest() == metadata['source_sha256']
data = (folder / 'swarm.g3t').read_bytes()
assert data[:4] == b'G3T1'
count, = struct.unpack_from('<I', data, 4)
assert count == metadata['triangles'] * 3
assert 190000 <= count // 3 <= 210000
assert len(data) == 8 + count * 32
vertices = list(struct.iter_unpack('<8f', data[8:]))
for v in vertices:
    assert all(math.isfinite(value) for value in v)
    assert .99 < sum(n*n for n in v[3:6]) < 1.01
    assert all(-.001 <= uv <= 1.001 for uv in v[6:8])
faces = set()
for i in range(0, count, 3):
    a, b, c = vertices[i:i+3]
    key = tuple(sorted((a[:3], b[:3], c[:3])))
    assert key not in faces, 'Overlapping duplicate triangles cause surface speckling'
    faces.add(key)
    ab = [b[k]-a[k] for k in range(3)]
    ac = [c[k]-a[k] for k in range(3)]
    cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
    assert sum(x*x for x in cross) > 1e-18
low = [min(v[k] for v in vertices) for k in range(3)]
high = [max(v[k] for v in vertices) for k in range(3)]
assert abs(low[2]) < 1e-6 and high[2] > 1
assert all(abs(low[k]+high[k]) < 1e-6 for k in range(2))
assert abs(max(high[k]-low[k] for k in range(2))-1) < 1e-6
for expected, actual in zip(metadata['bounds'], [low, high]):
    assert all(abs(a-b) < 1e-6 for a, b in zip(expected, actual))
texture = (folder / metadata['texture']).read_bytes()
assert texture[:8] == b'\x89PNG\r\n\x1a\n'
assert struct.unpack_from('>II', texture, 16) == (2048, 2048)
assert hashlib.sha256(texture).hexdigest() == metadata['texture_sha256']
print(f'Swarm: {count//3:,} triangles, valid UVs/normals, centered ground base, source hash and 2048px texture verified')
