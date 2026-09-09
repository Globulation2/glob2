#!/usr/bin/env python3
"""Validate all five original papyrus stages and their logical registration."""
import hashlib, json
from pathlib import Path
import numpy as np
from scipy import ndimage
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
frames=json.loads((ROOT/'datasrc/gfx/derived/papyrus-v1/manifest.json').read_text())['frames']
assert {f['id'] for f in frames}=={'ressource%d'%i for i in range(20,25)}
for frame in frames:
    for source in frame['sources']:
        assert hashlib.sha256((ROOT/source['path']).read_bytes()).hexdigest()==source['sha256']
    assert frame['sources'][0]['selected_layer']==int(frame['id'][9:])-20
    native=Image.open(ROOT/frame['sources'][1]['path']).convert('RGBA')
    runtime=Image.open(ROOT/'data/highres/v1'/(frame['id']+'.png')).convert('RGBA')
    assert runtime.size==(128,128) and native.size==(199,199)
    assert runtime.tobytes()==native.resize(runtime.size,Image.Resampling.LANCZOS).tobytes()
    a=np.asarray(Image.open(ROOT/'data/gfx'/(frame['id']+'.png')).convert('RGBA'))/255.
    b=np.asarray(runtime.resize((32,32),Image.Resampling.LANCZOS))/255.
    x=a[:,:,3]>.3;y=b[:,:,3]>.3
    overlap=(x&y).sum()/(x|y).sum()
    center=np.max(np.abs(np.subtract(ndimage.center_of_mass(x),ndimage.center_of_mass(y))))
    error=np.abs(a[:,:,:3]*a[:,:,3:]-b[:,:,:3]*b[:,:,3:]).mean()
    # Fine leaves change at subpixel boundaries with the original downsampling.
    # Check both registration and appearance rather than requiring identical masks.
    assert overlap>.83 and center<.75 and error<.025,(frame['id'],overlap,center,error)
    print('%s: silhouette overlap %.3f, center delta %.3f logical px, premultiplied RGB error %.4f'%(frame['id'],overlap,center,error))
print('PASS all five original stages, source hashes, native pixels, alpha and logical registration')
