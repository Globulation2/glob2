#!/usr/bin/env python3
"""Validate the exported pack independently of the model/generation environment."""
import hashlib,json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
PACK=ROOT/'data/highres/v1'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((PACK/'manifest.json').read_text())
assert m['version']==1
assert len(m['frames'])>=410
assert len({f['id'] for f in m['frames']})==len(m['frames'])
for f in m['frames']:
    logical=ROOT/'data/gfx'/(f['id']+'.png')
    if not logical.exists():logical=ROOT/'data/gfx'/(f['id']+'r.png')
    assert Image.open(logical).size==(f['width'],f['height']),f['id']
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
    im=Image.open(p);assert im.size==(4096>>level,4352>>level)
    # Check extruded tile borders independently at every supported mip level.
    slot=256>>level;border=64>>level;size=128>>level
    for i in range(272):
        x=(i%16)*slot;y=(i//16)*slot
        for j in range(size):
            assert im.getpixel((x,y+border+j))==im.getpixel((x+border,y+border+j))
            assert im.getpixel((x+slot-1,y+border+j))==im.getpixel((x+border+size-1,y+border+j))
print(f'PASS: {len(m["frames"])} frames; coverage, layer dimensions, RGBA, hashes, locked pool/school, four independently padded atlas levels')

for level,l in enumerate(m['resource_atlas']['levels']):
    p=PACK/l['file'];assert digest(p)==l['sha256']
    atlas=Image.open(p);assert atlas.size==(2048>>level,2304>>level)
    for i in range(65):
        tile=Image.open(PACK/f'ressource{i}.png')
        original=Image.open(ROOT/'data/gfx'/f'ressource{i}.png').convert('RGBA')
        if i >= 10:
            assert tile.getchannel('A').tobytes()==original.getchannel('A').resize(tile.size,Image.Resampling.BILINEAR).tobytes()
        else:
            record=next(f for f in m['frames'] if f['id']==f'ressource{i}')
            assert record['recipe'].startswith('recovered original tree:')
            native=Image.open(ROOT/record['sources'][1]['path']).convert('RGBA')
            assert tile.tobytes()==native.resize(tile.size,Image.Resampling.LANCZOS).tobytes()
        tile=tile.resize((tile.width>>level,tile.height>>level),Image.Resampling.LANCZOS)
        x=(i%8)*(256>>level)+(32>>level);y=(i//8)*(256>>level)+(32>>level)
        assert atlas.crop((x,y,x+tile.width,y+tile.height)).tobytes()==tile.tobytes()
        for j in range(tile.height):
            assert atlas.getpixel((x-1,y+j))==tile.getpixel((0,j))
            assert atlas.getpixel((x+tile.width,y+j))==tile.getpixel((tile.width-1,j))
print('PASS: all 65 resource frames, original native tree alpha / constrained other alpha, isolated resource atlas mip levels')

import re
expected={p.stem.removesuffix('r') for p in (ROOT/'data/gfx').glob('*.png') if re.fullmatch(r'(terrain|ressource|water|cloud|black|shade|area-clearing|area-forbidden|area-guard|bullet|explosion|magiceffect|particle)\d+r?\.png',p.name)}
assert expected <= {f['id'] for f in m['frames']}
print('PASS: complete non-unit world frame coverage')

# Inspect the exported atlas pixels, independently of the generation path.
from connected_terrain import topology
import numpy as np
topo=topology();joins=0
for level,record in enumerate(m['terrain_atlas']['levels']):
    atlas=np.asarray(Image.open(PACK/record['file']))
    slot=256>>level;border=64>>level;size=128>>level
    tiles={i:atlas[(i//16)*slot+border:(i//16)*slot+border+size,(i%16)*slot+border:(i%16)*slot+border+size] for i in topo}
    corners={}
    for i,a in tiles.items():
        c=topo[i]
        for symbol,pixel in zip(c,[a[0,0],a[0,-1],a[-1,0],a[-1,-1]]):
            if symbol in corners:assert np.array_equal(corners[symbol],pixel),(level,i,symbol)
            else:corners[symbol]=pixel
        for j,b in tiles.items():
            cb=topo[j]
            if (c[1],c[3])==(cb[0],cb[2]):
                assert np.array_equal(a[:,-1],b[:,0]),(level,i,j,'horizontal');joins+=1
            if (c[2],c[3])==(cb[0],cb[1]):
                assert np.array_equal(a[-1],b[0]),(level,i,j,'vertical');joins+=1
print(f'PASS: {joins} exported atlas joins and all corner junctions, RGBA, four mip levels')

water=Image.open(PACK/'water0.png')
for level in range(4):
    a=np.asarray(water.resize((water.width>>level,water.height>>level),Image.Resampling.BOX))
    assert np.array_equal(a[0],a[-1]),('water vertical',level)
    assert np.array_equal(a[:,0],a[:,-1]),('water horizontal',level)
assert water.getchannel('A').getextrema()==(255,255)
print('PASS: periodic opaque water; opposing edges agree through four mip levels')
