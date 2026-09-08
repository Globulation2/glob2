#!/usr/bin/env python3
"""Construct a compatible corner-based terrain tileset from shared HD materials."""
from pathlib import Path
import re,json,hashlib
import numpy as np
from PIL import Image,ImageDraw
from scipy.ndimage import gaussian_filter
ROOT=Path(__file__).resolve().parents[2]
EXP=ROOT/'experiments/ai-upscale'
OUT=EXP/'connected-terrain'
N=128

def smooth(x):
    x=np.clip(x,0,1);return x*x*(3-2*x)

def topology():
    source=(ROOT/'src/map/MapTerrain.cpp').read_text()
    entries=re.findall(r'\{\s*(\d+),\s*(\d+)\s*\},?\s*// ([HSE]), ([HSE]), ([HSE]), ([HSE])',source)
    result={}
    for start,count,*corners in entries:
        if set(corners)=={'H','S'} or set(corners)=={'S','E'} or len(set(corners))==1:
            for i in range(int(start),int(start)+int(count)):result[i]=corners
    assert len(result)==272
    return result

def material(index):
    source=EXP/'materials/grass-blades-v1.png' if index==0 else EXP/'world/corrected'/f'terrain{index}.png'
    a=np.asarray(Image.open(source).convert('RGB').resize((N,N),Image.Resampling.LANCZOS)).astype(float)
    if index==0:
        # Keep the source game's green palette while retaining generated blades.
        reference=np.asarray(Image.open(EXP/'sr/corrected/terrain0.png').convert('RGB')).astype(float)
        a=a-a.mean(axis=(0,1))+reference.mean(axis=(0,1))
    return periodic(a)

def periodic(a):
    # Remove the low-frequency boundary discontinuity with a periodic/smooth
    # decomposition. Retain the original grain instead of mirroring motifs.
    boundary=np.zeros_like(a)
    boundary[0]=a[-1]-a[0];boundary[-1]=-boundary[0]
    jump=a[:,-1]-a[:,0];boundary[:,0]+=jump;boundary[:,-1]-=jump
    yy,xx=np.indices(a.shape[:2])
    denom=2*np.cos(2*np.pi*xx/a.shape[1])+2*np.cos(2*np.pi*yy/a.shape[0])-4
    denom[0,0]=1
    correction=np.fft.fft2(boundary,axes=(0,1))/denom[...,None]
    correction[0,0]=0
    return np.clip(a-np.fft.ifft2(correction,axes=(0,1)).real,0,255)

def match_edges(tiles,topo):
    # Stitch each legal edge class together, including its alpha. Only the
    # boundary samples are changed; no flat bands or reflected texture blocks.
    for axis in [0,1]:
        groups={}
        for i,c in topo.items():
            a=tiles[i]
            for key,edge in ([(tuple(c[:2]),a[0]),(tuple(c[2:]),a[-1])] if axis==0 else [((c[0],c[2]),a[:,0]),((c[1],c[3]),a[:,-1])]):
                groups.setdefault(key,[]).append(edge)
        for values in groups.values():
            mean=np.mean(values,axis=0).round().astype('uint8')
            for edge in values:edge[1:-1]=mean[1:-1]
    corners={}
    for i,c in topo.items():
        a=tiles[i]
        for symbol,pixel in zip(c,[a[0,0],a[0,-1],a[-1,0],a[-1,-1]]):corners.setdefault(symbol,[]).append(pixel)
    for values in corners.values():
        mean=np.mean(values,axis=0).round().astype('uint8')
        for pixel in values:pixel[:]=mean
    return tiles

def mip_tiles(level):
    size=N>>level;topo=topology()
    tiles={i:np.array(Image.open(OUT/f'terrain{i}.png').resize((size,size),Image.Resampling.BOX)) for i in topo}
    return match_edges(tiles,topo)

def generate():
    OUT.mkdir(exist_ok=True)
    topo=topology();grass=material(0);sand=material(128)
    y,x=np.indices((N,N),dtype=float);u=smooth(x/(N-1));v=smooth(y/(N-1))
    edge=np.minimum.reduce([x,y,N-1-x,N-1-y]);interior=smooth((edge-16)/24)
    rng=np.random.default_rng(712)
    q=gaussian_filter(rng.normal(size=(64,64)),2.3)
    q=q/(q.std()+1e-9)
    top=np.concatenate([q,q[:,::-1]],axis=1)
    shared_noise=np.concatenate([top,top[::-1]],axis=0)
    records=[];tiles={}
    for index,c in sorted(topo.items()):
        rng=np.random.default_rng(index)
        noise=gaussian_filter(rng.normal(size=(N,N)),5,mode='wrap')
        noise=noise/(noise.std()+1e-9)
        rough=gaussian_filter(rng.normal(size=(N,N)),2.4,mode='wrap')
        rough=np.clip(rough/(rough.std()+1e-9),-2,2)
        def mask(symbol):
            tl,tr,bl,br=[float(a==symbol) for a in c]
            field=(1-v)*((1-u)*tl+u*tr)+v*((1-u)*bl+u*br)
            # Shared fine edge variation gives every legal join the same rugged
            # profile; frame-specific roughness fades out before the edges.
            field=field+.09*np.clip(shared_noise,-2,2)*4*field*(1-field)+.03*rough*interior*4*field*(1-field)
            return smooth((field-.47)/.06)
        if 'H' in c:
            g=mask('H');rgb=grass*g[...,None]+sand*(1-g[...,None]);alpha=np.full((N,N),255.)
        else:
            rgb=sand.copy();alpha=mask('S')*255
        rgb=rgb+np.clip(noise,-2,2)[...,None]*interior[...,None]*1.5
        a=np.dstack([rgb,alpha]).round().clip(0,255).astype('uint8')
        tiles[index]=a
    match_edges(tiles,topo)
    for index,c in sorted(topo.items()):
        Image.fromarray(tiles[index]).save(OUT/f'terrain{index}.png')
        records.append({'id':f'terrain{index}','corners':c,'sha256':hashlib.sha256((OUT/f'terrain{index}.png').read_bytes()).hexdigest()})
    (OUT/'manifest.json').write_text(json.dumps({'recipe':'shared-material rugged corner masks v2; generated grass blades','seed':'frame index','sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [EXP/'sr/corrected/terrain0.png',EXP/'materials/grass-blades-v1.png',EXP/'materials/grass-blades-v1.txt',EXP/'world/corrected/terrain128.png',ROOT/'src/map/MapTerrain.cpp']},'frames':records},indent=2)+'\n')
    validate(topo)

def validate(topo):
    pairs=0
    for level in range(4):
        size=N>>level
        tiles=mip_tiles(level)
        for i,a in tiles.items():
            ca=topo[i]
            for j,b in tiles.items():
                cb=topo[j]
                if (ca[1],ca[3])==(cb[0],cb[2]):
                    active=(a[:,-1,3]>0)|(b[:,0,3]>0)
                    assert np.array_equal(a[:,-1][active],b[:,0][active]),(i,j,level,'horizontal')
                    pairs+=1
                if (ca[2],ca[3])==(cb[0],cb[1]):
                    active=(a[-1,:,3]>0)|(b[0,:,3]>0)
                    assert np.array_equal(a[-1,:][active],b[0,:][active]),(i,j,level,'vertical')
                    pairs+=1
    print(f'PASS: {pairs} compatible directed joins across four mip levels')

def preview():
    topo=topology();groups={}
    for i,c in topo.items():groups.setdefault(tuple(c),[]).append(i)
    boundaries=[3,3,4,5,5,4,3,2,2]
    corners=[['H' if x<b else 'S' if x<b+2 else 'E' for x in range(10)] for b in boundaries]
    water_sources=[ROOT/'data/gfx/water0.png',EXP/'materials/water0.png']
    panels=[Image.open(p).convert('RGBA').resize((2048,2048),Image.Resampling.NEAREST).crop((0,0,9*N,8*N)) for p in water_sources]
    rng=np.random.default_rng(172)
    for y in range(8):
        for x in range(9):
            c=(corners[y][x],corners[y][x+1],corners[y+1][x],corners[y+1][x+1])
            index=int(rng.choice(groups[c]))
            if index>=256:continue # The engine draws water separately below terrain.
            old=Image.open(ROOT/'data/gfx'/f'terrain{index}.png').convert('RGBA').resize((N,N),Image.Resampling.NEAREST)
            new=Image.open(OUT/f'terrain{index}.png').convert('RGBA')
            for panel,tile in zip(panels,[old,new]):panel.alpha_composite(tile,(x*N,y*N))
    out=Image.new('RGB',(18*N,8*N+32),'#26362d');d=ImageDraw.Draw(out)
    for k,(panel,label) in enumerate(zip(panels,['ORIGINAL — enlarged 4x','CONNECTED TERRAIN — same resolution'])):
        out.paste(panel,(k*9*N,32));d.text((k*9*N+12,10),label,fill='white')
    out.save(ROOT/'docs/high-resolution/images/terrain-connected.png')

if __name__=='__main__':
    generate();preview()
