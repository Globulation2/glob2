#!/usr/bin/env python3
"""Validate original provenance, native export registration and atomic layers."""
import hashlib
import json
from pathlib import Path
import tempfile
import numpy as np
from PIL import Image
import runtime_overrides

ROOT = Path(__file__).resolve().parents[2]
DERIVED = ROOT / 'datasrc/gfx/derived/recovered-v1'
PACK = ROOT / 'data/highres/v1'
records = json.loads((DERIVED/'manifest.json').read_text())['frames']
pack = {f['id']: f for f in json.loads((PACK/'manifest.json').read_text())['frames']}
for frame in records:
    assert pack[frame['id']]['recipe'] == frame['recipe']
    for src in frame['sources']:
        assert hashlib.sha256((ROOT/src['path']).read_bytes()).hexdigest() == src['sha256']
    for layer in frame['layers']:
        image = Image.open(DERIVED/layer['file']).convert('RGBA')
        assert image.size == (frame['width']*4,frame['height']*4)
        assert (PACK/layer['file']).read_bytes() == (DERIVED/layer['file']).read_bytes()
        a=np.asarray(image)
        assert a[0,0,3] == 0, frame['id']
        assert a[:,:,3].max() <= (144 if layer['role']=='base' and frame['id'].startswith(('swarm','warflag','explorationflag','clearingflag')) else 255)
    role='team' if any(l['role']=='team' for l in frame['layers']) else 'base'
    suffix='r' if role=='team' else ''
    name=frame['id']+suffix+'.png'
    classic=Image.open(ROOT/'data/gfx'/name).convert('RGBA')
    new=Image.open(DERIVED/name).resize(classic.size,Image.Resampling.LANCZOS)
    ca=np.asarray(classic)[:,:,3]>80;na=np.asarray(new)[:,:,3]>80
    overlap=np.sum(ca&na)/np.sum(ca|na)
    # Native masks recover finer edges; ensure the matching silhouette still
    # occupies the same logical footprint (not exact classic pixel replication).
    assert overlap>.74,(frame['id'],overlap)
    print('%s: silhouette overlap %.3f; logical canvas %s' % (frame['id'],overlap,classic.size))

# A missing paired team layer must reject the replacement before touching output.
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory); source=root/'derived';source.mkdir();output=root/'output';output.mkdir()
    frame=json.loads(json.dumps(records[0]))
    frame['layers']=[l for l in frame['layers'] if l['role']=='base']
    (source/'manifest.json').write_text(json.dumps(dict(version=1,frames=[frame])))
    prior=runtime_overrides.DERIVED;runtime_overrides.DERIVED=source
    try:
        try: runtime_overrides.apply([json.loads(json.dumps(pack[frame['id']]))],output)
        except AssertionError: pass
        else: raise AssertionError('Incomplete base/team combination accepted')
        assert not list(output.iterdir())
    finally: runtime_overrides.DERIVED=prior
print('PASS: 10 original replacements, source hashes, transparent backgrounds, registration, atomic base/team rejection')
