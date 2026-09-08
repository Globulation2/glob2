#!/usr/bin/env python3
"""Build evidence sheets: original and final frames at identical 4x dimensions."""
import json
from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[2]
PACK=ROOT/'data/highres/v1'
OUT=ROOT/'docs/high-resolution/images'
frames=json.loads((PACK/'manifest.json').read_text())['frames']

def composite(f, original):
    out=Image.new('RGBA',(f['width']*4,f['height']*4))
    for role in ['base','team']:
        for layer in f['layers']:
            if layer['role']!=role: continue
            path=(ROOT/'data/gfx' if original else PACK)/layer['file']
            if not path.exists(): continue
            im=Image.open(path).convert('RGBA')
            if original: im=im.resize((im.width*4,im.height*4),Image.Resampling.NEAREST)
            out.alpha_composite(im,(0,0))
    return out

def sheet(items,name):
    width=max(max(f['width']*4 for f in items)+40,350)
    heights=[f['height']*4+58 for f in items]
    out=Image.new('RGB',(width*2,sum(heights)+36),'#26362d');d=ImageDraw.Draw(out)
    d.text((16,12),'ORIGINAL - enlarged 4x',fill='white')
    d.text((width+16,12),'FINAL UPSCALE - same dimensions',fill='white')
    y=36
    for f,h in zip(items,heights):
        d.line((0,y,width*2,y),fill='#637269')
        for j,original in enumerate([True,False]):
            d.text((j*width+16,y+8),f['id'],fill='white')
            im=composite(f,original);out.paste(im,(j*width+16,y+32),im)
        y+=h
    out.save(OUT/name)

OUT.mkdir(parents=True,exist_ok=True)
selected=['swarm0b0','inn0c0','hosp0b1','pool0b0','school1b0','defencetower1b1']
for name in selected:
    match=[f for f in frames if f['id']==name]
    if match: sheet(match,name+'.png')
for page in range(0,len(frames),8):sheet(frames[page:page+8],f'all-{page//8+1:02}.png')
