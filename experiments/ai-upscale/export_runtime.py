#!/usr/bin/env python3
"""Export reviewed artwork into a self-contained, versioned runtime pack."""
import hashlib
import json
from pathlib import Path
import shutil
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
EXP=ROOT/'experiments/ai-upscale'
OUT=ROOT/'data/highres/v1'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    frames=json.loads((EXP/'finishing/manifest.json').read_text())
    config=json.loads((EXP/'finishing.json').read_text())
    for f in frames:
        selected=config['images'].get(f['id'],{}).get('selected',config['default'])
        assert f['selected']==selected, f"Stale finishing output for {f['id']}; rerun finishing.py"
    records=[]
    for f in frames:
        method=f['selected']; layers=[]
        if method.startswith('generator_'):
            path=EXP/next(v['path'] for v in f['variants'] if v['id']==method)
            a=np.asarray(Image.open(path).convert('RGBA')).copy()
            # Neutral extracted shadows are excluded from the hue-shift layer.
            rgb=a[:,:,:3].astype(float)
            green=(rgb[:,:,1]>rgb[:,:,0]+8)&(rgb[:,:,1]>rgb[:,:,2]+8)
            base=a.copy();team=a.copy();base[green]=0;team[~green]=0
            for name,pixels,role in [(f['id']+'.png',base,'base'),(f['id']+'r.png',team,'team')]:
                Image.fromarray(pixels).save(OUT/name)
                layers.append(dict(file=name,role=role,sha256=sha(OUT/name),source_sha256=sha(path)))
            composed=Image.alpha_composite(Image.fromarray(base),Image.fromarray(team))
            assert np.array_equal(np.asarray(composed),a)
        else:
            for name in f['layers']:
                source=EXP/('buildings/corrected' if method=='current' else 'finishing/layers/'+method)/name
                shutil.copyfile(source,OUT/name)
                layers.append(dict(file=name,role='team' if name.endswith('r.png') else 'base',sha256=sha(OUT/name),source_sha256=sha(source)))
        records.append(dict(id=f['id'],width=f['width'],height=f['height'],scale=4,layers=layers,recipe=method))
    for i in range(16):
        name=f'terrain{i}.png';source=EXP/'sr/corrected'/name;Image.open(source).convert('RGBA').save(OUT/name)
        w,h=Image.open(ROOT/'data/gfx'/name).size
        records.append(dict(id=f'terrain{i}',width=w,height=h,scale=4,recipe='terrain constrained',layers=[dict(file=name,role='base',sha256=sha(OUT/name),source_sha256=sha(source))]))
    lines=['GLOB2_HIGHRES 1']
    for f in records:
        layer={x['role']:x['file'] for x in f['layers']}
        for x in f['layers']:
            im=Image.open(OUT/x['file'])
            original=ROOT/'data/gfx'/x['file']
            expected=Image.open(original).size if original.exists() and not f['recipe'].startswith('generator_') else (f['width'],f['height'])
            assert im.size==(expected[0]*4,expected[1]*4)
            x['logical_width'],x['logical_height']=expected
            x['original_sha256']=sha(original) if original.exists() else None
        lines.append(f"{f['id']} {f['width']} {f['height']} 4 {layer.get('base','-')} {layer.get('team','-')}")
    atlas_levels=[]
    for level in range(4):
        slot=256>>level; border=64>>level; size=128>>level
        atlas=Image.new('RGBA',(slot*4,slot*4))
        for i in range(16):
            tile=Image.open(OUT/f'terrain{i}.png').convert('RGBA').resize((size,size),Image.Resampling.LANCZOS)
            padded=Image.fromarray(np.pad(np.asarray(tile),((border,border),(border,border),(0,0)),mode='edge'))
            atlas.paste(padded,((i%4)*slot,(i//4)*slot))
        name=f'terrain-atlas-mip{level}.png';atlas.save(OUT/name)
        atlas_levels.append(dict(file=name,sha256=sha(OUT/name)))
    (OUT/'frames.txt').write_text('\n'.join(lines)+'\n')
    (OUT/'manifest.json').write_text(json.dumps(dict(version=1,selection_sha256=sha(EXP/'finishing.json'),frames=records,terrain_atlas=dict(slot=256,border=64,levels=atlas_levels)),indent=2)+'\n')
    print(f'Exported {len(records)} frames, {sum(len(f["layers"]) for f in records)} layers to {OUT}')

if __name__=='__main__':main()
