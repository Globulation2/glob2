#!/usr/bin/env python3
"""Validate the exported pack independently of the model/generation environment."""
import hashlib,json,re
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
PACK=ROOT/'data/highres/v1'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((PACK/'manifest.json').read_text())
assert m['version']==1
assert m['frames']
assert len({f['id'] for f in m['frames']})==len(m['frames'])
for f in m['frames']:
    logical=ROOT/'data/gfx'/(f['id']+'.png')
    if not logical.exists():logical=ROOT/'data/gfx'/(f['id']+'r.png')
    assert Image.open(logical).size==(f['width'],f['height']),f['id']
    layers={l['role']:l for l in f['layers']}
    assert layers.keys()<= {'base','team'}
    # Unit HD textures render onto a fixed 128x128 canvas regardless of native
    # size (see render.py's UNIT_HD_PIXEL_SIZE); every other category is still
    # a literal 4x of its own logical size.
    is_unit=re.fullmatch(r'unit\d+',f['id'])
    for l in layers.values():
        path=PACK/l['file'];im=Image.open(path)
        assert digest(path)==l['sha256'],path
        assert im.mode=='RGBA',path
        expected=(128,128) if is_unit else (l['logical_width']*4,l['logical_height']*4)
        assert im.size==expected,path
    for role,suffix in [('base','.png'),('team','r.png')]:
        if (ROOT/'data/gfx'/(f['id']+suffix)).exists():assert role in layers,f['id']
print('PASS manifest hashes, paired layers and original logical dimensions')
