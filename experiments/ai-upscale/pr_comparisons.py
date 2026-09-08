#!/usr/bin/env python3
"""Build evidence sheets: original and final frames at identical 4x dimensions."""
import json
from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[2]
PACK=ROOT/'data/highres/v1'
OUT=ROOT/'docs/high-resolution/images'
frames=json.loads((PACK/'manifest.json').read_text())['frames']

def display_size(f):
    return (max(l['logical_width'] for l in f['layers'])*4,max(l['logical_height'] for l in f['layers'])*4)

def composite(f, original):
    out=Image.new('RGBA',display_size(f))
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
    width=max(max(display_size(f)[0] for f in items)+40,350)
    heights=[display_size(f)[1]+58 for f in items]
    out=Image.new('RGB',(width*2,sum(heights)+36),'#26362d');d=ImageDraw.Draw(out)
    d.text((16,12),'ORIGINAL - enlarged 4x',fill='white')
    d.text((width+16,12),'FINAL RUNTIME ART - same dimensions',fill='white')
    y=36
    for f,h in zip(items,heights):
        d.line((0,y,width*2,y),fill='#637269')
        for j,original in enumerate([True,False]):
            d.text((j*width+16,y+8),f['id'],fill='white')
            im=composite(f,original);out.paste(im,(j*width+16,y+32),im)
        y+=h
    out.save(OUT/name)

OUT.mkdir(parents=True,exist_ok=True)
selected=['area-guard0','area-clearing0','area-forbidden3','swarm0b0','swarm0c0','warflag0','buildingsite3','inn0c0','hosp0b1','pool0b0','school1b0','racetrack0b0','racetrack1b0','defencetower1b1','ressource9','ressource13','ressource19','ressource40']
for name in selected:
    match=[f for f in frames if f['id']==name]
    if match: sheet(match,name+'.png')
pages=[];group=[];height=0
for f in frames:
    h=display_size(f)[1]+58
    if group and (len(group)==8 or height+h>2200):pages.append(group);group=[];height=0
    group.append(f);height+=h
if group:pages.append(group)
for page,items in enumerate(pages):sheet(items,f'all-{page+1:02}.png')
intro="# Final artwork comparisons\n\nLeft: original enlarged 4× with nearest-neighbor sampling. Right: final runtime artwork at identical pixel dimensions. Each pair uses the same background. Click to inspect at full size. No intermediate candidates are shown. Fog and soft masks use faithful resampling rather than generated detail.\n\n"
(ROOT/'docs/high-resolution/COMPARISONS.md').write_text(intro+'\n\n'.join(f"![Final comparison sheet {i+1}: {page[0]['id']} through {page[-1]['id']}](images/all-{i+1:02}.png)" for i,page in enumerate(pages))+'\n')
