#!/usr/bin/env python3
"""Prepare the selected generated water for the engine's scrolling repeat pass."""
import hashlib,json
import numpy as np
from PIL import Image
from connected_terrain import EXP,periodic

def generate():
    source=EXP/'materials/water-quiet-v1.png'
    target=EXP/'world/corrected/water0.png'
    original=Image.open(target).convert('RGBA')
    a=np.asarray(Image.open(source).convert('RGB').resize(original.size,Image.Resampling.LANCZOS)).astype(float)
    reference=np.asarray(original)[...,:3]
    # Lower ripple contrast and chroma; keep the original mean luminance.
    a=.55*(a-a.mean(axis=(0,1)))+reference.mean(axis=(0,1))
    luminance=np.sum(a*np.array([.2126,.7152,.0722]),axis=2,keepdims=True)
    a=.8*a+.2*luminance
    a=periodic(a).round().clip(0,255).astype('uint8')
    # Pair an eight-texel collar across opposite edges. The supported BOX
    # mip footprints then agree through level 3 (50% zoom) as well as level 0.
    for k in range(8):
        edge=((a[k].astype(float)+a[-1-k])/2).round().astype('uint8')
        a[k]=edge;a[-1-k]=edge
    for k in range(8):
        edge=((a[:,k].astype(float)+a[:,-1-k])/2).round().astype('uint8')
        a[:,k]=edge;a[:,-1-k]=edge
    result=Image.fromarray(a).convert('RGBA')
    result.putalpha(original.getchannel('A'))
    output=EXP/'materials/water0.png';result.save(output)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    (EXP/'materials/water-manifest.json').write_text(json.dumps({
        'recipe':'quiet ripples; periodic material v3',
        'sources':{str(p.relative_to(EXP)):sha(p) for p in [source,target,EXP/'materials/water-quiet-v1.txt']},
        'output_sha256':sha(output)},indent=2)+'\n')

if __name__=='__main__':generate()
