#!/usr/bin/env python3
"""Reduce artifacts/m0/<label> to hashes + timing-stripped result.json, for later comparison."""
import hashlib, json, os, sys

label = sys.argv[1]
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'm0', label)
files = ['run4/game.replay.checksums', 'run2/game.replay.checksums', 'testgames.replay.checksums',
         'run4/game.replay', 'run2/game.replay', 'testgames.replay', 'playback/r.replay.checksums']

def strip(x):
    if isinstance(x, dict):
        return {k: strip(v) for k, v in x.items() if not (k.endswith('_ns') or k == 'seconds')}
    if isinstance(x, list):
        return [strip(v) for v in x]
    return x

out = {}
for f in files:
    p = os.path.join(root, f)
    if not os.path.exists(p):
        out[f] = None
        continue
    data = open(p, 'rb').read()
    # Checksum sidecars: also hash past the 20-byte header (playback header tick count differs).
    out[f] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
              'body_sha256': hashlib.sha256(data[20:]).hexdigest()}
for r in ['run4', 'run2']:
    p = os.path.join(root, r, 'result.json')
    out[r + '/result.json (timing stripped)'] = hashlib.sha256(
        json.dumps(strip(json.load(open(p))), sort_keys=True).encode()).hexdigest() if os.path.exists(p) else None
for r in ['run4', 'run2']:
    p = os.path.join(root, r + '.log')
    out[r + '.log tail'] = open(p, errors='replace').read()[-300:] if os.path.exists(p) else None
json.dump(out, open(os.path.join(os.path.dirname(root), '..', label + '-hashes.json'), 'w'), indent=1)
print(json.dumps({k: (v['bytes'] if isinstance(v, dict) else v) for k, v in out.items() if 'log' not in k}, indent=1))
