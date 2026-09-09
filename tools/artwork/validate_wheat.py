#!/usr/bin/env python3
"""Check authentic wheat sources, growth coverage and logical silhouette registration."""
import hashlib,json
from pathlib import Path
import numpy as np
from scipy import ndimage
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
DERIVED=ROOT/'datasrc/gfx/derived/wheat-v1'
frames=json.loads((DERIVED/'manifest.json').read_text())['frames']
assert {f['id'] for f in frames}=={'ressource%d'%i for i in [10,11,12,13,15,16,17,18]}
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
    # Tiny seed sprites are sensitive to a single antialiased edge pixel.
    # Require matching components, a <=1 pixel boundary deviation and a
    # subpixel center, instead of demanding tree-sized mask overlap.
    assert overlap>.85,(name,overlap)
    assert ndimage.label(a)[1]==ndimage.label(b)[1],name
    assert ndimage.distance_transform_edt(~a)[b].max()<=1,name
    assert ndimage.distance_transform_edt(~b)[a].max()<=1,name
    assert np.max(np.abs(np.subtract(ndimage.center_of_mass(a),ndimage.center_of_mass(b))))<.5,name
    print('%s: silhouette overlap %.3f'%(name,overlap))
print('PASS: eight growth/variant frames, original source hashes, dimensions and registration')
