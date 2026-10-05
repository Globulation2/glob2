"""Capture selected source pairs at equal presentation sizes without clipping."""
import json
from pathlib import Path
from PIL import Image, ImageDraw
root=Path.cwd()
frames={f['id']:f for f in json.loads((root/'data/highres/v1/manifest.json').read_text())['frames']}
selected=['inn0b0','hosp1b0','pool1b0','defencetower1b1','ressource19','ressource40','magiceffect0','water0']
rows=[]
for key in selected:
 f=frames[key]
 size=(max(l['logical_width']*4 for l in f['layers']),max(l['logical_height']*4 for l in f['layers']))
 scale=min(1,480/size[0],320/size[1])
 display=(round(size[0]*scale),round(size[1]*scale))
 rows.append((f,size,display))
sheet=Image.new('RGB',(1040,sum(size[1]+52 for f,original,size in rows)+36),'#283c31')
draw=ImageDraw.Draw(sheet)
draw.text((16,12),'Classic source (nearest enlargement)',fill='white')
draw.text((536,12),'Approved HD source (matched display size)',fill='white')
y=36
for f,size,display in rows:
 draw.line((0,y,1040,y),fill='#68796c')
 for col,folder in enumerate(['data/gfx','data/highres/v1']):
  draw.text((col*520+16,y+8),f['id'],fill='white')
  composed=Image.new('RGBA',size)
  for role in ['base','team']:
   for layer in f['layers']:
    if layer['role']!=role:continue
    with Image.open(root/folder/layer['file']) as image:image=image.convert('RGBA')
    if col==0:image=image.resize((image.width*4,image.height*4),Image.Resampling.NEAREST)
    composed.alpha_composite(image)
  if display!=size:composed=composed.resize(display,Image.Resampling.LANCZOS)
  sheet.paste(composed,(col*520+16,y+28),composed)
 y+=display[1]+52
sheet.save(root/'artifacts/pr220/source-comparison.png')
