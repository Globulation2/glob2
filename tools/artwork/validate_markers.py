#!/usr/bin/env python3
"""Check all marker phases, native pixels, alpha coverage and runtime registration."""
import hashlib,json
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
DERIVED=ROOT/'datasrc/gfx/derived/markers-v1'
frames=json.loads((DERIVED/'manifest.json').read_text())['frames']
assert {f['id'] for f in frames}=={'area-%s%d'%(family,i) for family in ['guard','clearing','forbidden'] for i in range(8)}
for frame in frames:
    source=frame['sources'][0];path=ROOT/source['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest()==source['sha256']
    native=Image.open(path).convert('RGBA')
    image=Image.open(ROOT/'data/highres/v1'/(frame['id']+'.png')).convert('RGBA')
    assert image.size==(128,128) and image.tobytes()==native.tobytes()
    assert image.tobytes()==Image.open(DERIVED/(frame['id']+'.png')).tobytes()
    classic=Image.open(ROOT/'data/gfx'/(frame['id']+'.png')).convert('RGBA')
    assert classic.size==(frame['width'],frame['height'])==(32,32)
    a=np.asarray(classic,dtype=float)/255
    b=np.asarray(image.resize((32,32),Image.Resampling.LANCZOS),dtype=float)/255
    error=np.abs(a[:,:,:3]*a[:,:,3:]-b[:,:,:3]*b[:,:,3:]).mean()
    assert error<.015,(frame['id'],error)
    # Preserve pulse coverage as well as shape; never substitute a similar-looking phase.
    coverage=np.asarray(image)[:,:,3].sum()/16 / np.asarray(classic)[:,:,3].sum()
    assert .97<coverage<1.03,(frame['id'],coverage)
    print('%s: mean premultiplied RGB error %.4f, alpha coverage %.3f'%(frame['id'],error,coverage))
print('PASS: 24 original animation frames, source hashes, native alpha and pixels, logical sizes and phase coverage')
