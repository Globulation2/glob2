#!/usr/bin/env python3
"""Validate the exported pack independently of the model/generation environment."""
import hashlib,json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
PACK=ROOT/'data/highres/v1'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((PACK/'manifest.json').read_text())
assert m['version']==1 and len(m['frames'])==89
assert len({f['id'] for f in m['frames']})==89
for f in m['frames']:
    layers={l['role']:l for l in f['layers']}
    assert layers.keys()<= {'base','team'}
    for l in layers.values():
        path=PACK/l['file'];im=Image.open(path)
        assert digest(path)==l['sha256'],path
        assert im.mode=='RGBA',path
        assert im.size==(l['logical_width']*4,l['logical_height']*4),path
    for role,suffix in [('base','.png'),('team','r.png')]:
        if (ROOT/'data/gfx'/(f['id']+suffix)).exists():assert role in layers,f['id']
for name in ['pool0b0','school1b0']:
    for f in m['frames']:
        if f['id']==name:
            for l in f['layers']:assert digest(PACK/l['file'])==digest(ROOT/'experiments/ai-upscale/buildings/corrected'/l['file'])
for level,l in enumerate(m['terrain_atlas']['levels']):
    p=PACK/l['file'];assert digest(p)==l['sha256']
    im=Image.open(p);assert im.size==(1024>>level,1024>>level)
    # Check extruded tile borders independently at every supported mip level.
    slot=256>>level;border=64>>level;size=128>>level
    for i in range(16):
        x=(i%4)*slot;y=(i//4)*slot
        for j in range(size):
            assert im.getpixel((x,y+border+j))==im.getpixel((x+border,y+border+j))
            assert im.getpixel((x+slot-1,y+border+j))==im.getpixel((x+border+size-1,y+border+j))
print('PASS: 89 frames; coverage, layer dimensions, RGBA, hashes, locked pool/school, four independently padded atlas levels')
