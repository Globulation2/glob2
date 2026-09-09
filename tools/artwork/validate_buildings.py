#!/usr/bin/env python3
"""Verify original building provenance, layer registration, opacity and pack pixels."""
import hashlib,json
from pathlib import Path
from PIL import Image
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
DERIVED=ROOT/'datasrc/gfx/derived/buildings-v1'
frames=json.loads((DERIVED/'manifest.json').read_text())['frames']
assert {f['id'] for f in frames}=={'school1b0','racetrack0b0','racetrack1b0'}
for frame in frames:
    for source in frame['sources']:
        assert hashlib.sha256((ROOT/source['path']).read_bytes()).hexdigest()==source['sha256']
    assert {l['role'] for l in frame['layers']}=={'base','team'}
    for layer in frame['layers']:
        native=Image.open(ROOT/layer['native_file']).convert('RGBA')
        actual=Image.open(DERIVED/layer['file']).convert('RGBA')
        original=Image.open(ROOT/'data/gfx'/layer['file']).convert('RGBA')
        assert actual.size==(original.width*4,original.height*4)
        expected=native.resize(actual.size,Image.Resampling.LANCZOS)
        expected.putalpha(expected.getchannel('A').point(lambda x:min(x,layer['alpha_ceiling'])))
        assert actual.tobytes()==expected.tobytes(),layer['file']
        assert (ROOT/'data/highres/v1'/layer['file']).read_bytes()==(DERIVED/layer['file']).read_bytes()
        if frame['id']=='school1b0' and layer['role']=='team':
            assert actual.getchannel('A').getextrema()[1]==64,'School overlay lost its 25% opacity'
        # Ignore very faint filtering fringes when comparing silhouettes.
        a=np.asarray(original)[:,:,3]>40
        b=np.asarray(actual.resize(original.size,Image.Resampling.LANCZOS))[:,:,3]>40
        overlap=np.sum(a&b)/np.sum(a|b)
        assert overlap>.9,(layer['file'],overlap)
        print('%s: alpha overlap %.3f; ceiling %d'%(layer['file'],overlap,layer['alpha_ceiling']))
print('PASS: three originals, both layers, source hashes, logical registration, saved translucency and runtime pixels')
