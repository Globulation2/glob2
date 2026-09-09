#!/usr/bin/env python3
"""Check authentic tree sources, growth coverage and logical silhouette registration."""
import hashlib,json
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
DERIVED=ROOT/'datasrc/gfx/derived/trees-v1'
frames=json.loads((DERIVED/'manifest.json').read_text())['frames']
assert {f['id'] for f in frames}=={'ressource%d'%i for i in range(10)}
for frame in frames:
    for source in frame['sources']:
        assert hashlib.sha256((ROOT/source['path']).read_bytes()).hexdigest()==source['sha256']
    name=frame['id']+'.png'
    new=Image.open(DERIVED/name).convert('RGBA')
    classic=Image.open(ROOT/'data/gfx'/name).convert('RGBA')
    assert new.size==(classic.width*4,classic.height*4)
    a=np.asarray(classic)[:,:,3]>80
    b=np.asarray(new.resize(classic.size,Image.Resampling.LANCZOS))[:,:,3]>80
    overlap=np.sum(a&b)/np.sum(a|b)
    assert overlap>.9,(name,overlap)
    print('%s: silhouette overlap %.3f'%(name,overlap))
print('PASS: ten growth/variant frames, original source hashes, dimensions and registration')
